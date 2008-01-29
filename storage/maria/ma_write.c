/* Copyright (C) 2006 MySQL AB & MySQL Finland AB & TCX DataKonsult AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

/* Write a row to a MARIA table */

#include "ma_fulltext.h"
#include "ma_rt_index.h"
#include "trnman.h"
#include "ma_key_recover.h"
#include "ma_blockrec.h"

#define MAX_POINTER_LENGTH 8

	/* Functions declared in this file */

static int w_search(register MARIA_HA *info, register MARIA_KEYDEF *keyinfo,
		    uint comp_flag, uchar *key, uint key_length, my_off_t page,
		    my_off_t father_page, uchar *father_buff,
                    MARIA_PINNED_PAGE *father_page_link, uchar *father_keypos,
		    my_bool insert_last);
static int _ma_balance_page(MARIA_HA *info,MARIA_KEYDEF *keyinfo,
                            uchar *key, uchar *curr_buff, my_off_t page,
                            my_off_t father_page, uchar *father_buff,
                            uchar *father_keypos,
                            MARIA_KEY_PARAM *s_temp);
static uchar *_ma_find_last_pos(MARIA_HA *info, MARIA_KEYDEF *keyinfo,
                                uchar *page, uchar *key,
                                uint *return_key_length, uchar **after_key);
static int _ma_ck_write_tree(register MARIA_HA *info, uint keynr,uchar *key,
                             uint key_length);
static int _ma_ck_write_btree(register MARIA_HA *info, uint keynr,uchar *key,
                              uint key_length);
static int _ma_ck_write_btree_with_log(MARIA_HA *info, MARIA_KEYDEF *keyinfo,
                                       uchar *key, uint key_length,
                                       my_off_t *root, uint comp_flag);
static my_bool _ma_log_new(MARIA_HA *info, my_off_t page, uchar *buff,
                           uint page_length, uint key_nr, my_bool root_page);
static my_bool _ma_log_change(MARIA_HA *info, my_off_t page, uchar *buff,
                              uchar *key_pos, uint length);
static my_bool _ma_log_split(MARIA_HA *info, my_off_t page, uchar *buff,
                             uint org_length, uint new_length,
                             uchar *key_pos,
                             uint key_length, int move_length,
                             enum en_key_op prefix_or_suffix,
                             uchar *data, uint data_length,
                             uint changed_length);
static my_bool _ma_log_del_prefix(MARIA_HA *info, my_off_t page, uchar *buff,
                                  uint org_length, uint new_length,
                                  uchar *key_pos, uint key_length,
                                  int move_length);
static my_bool _ma_log_key_middle(MARIA_HA *info, my_off_t page, uchar *buff,
                                  uint new_length,
                                  uint data_added_first,
                                  uint data_changed_first,
                                  uint data_deleted_last,
                                  uchar *key_pos,
                                  uint key_length, int move_length);

/*
  @brief Default handler for returing position to new row
*/

MARIA_RECORD_POS _ma_write_init_default(MARIA_HA *info,
                                        const uchar *record
                                        __attribute__((unused)))
{
  return ((info->s->state.dellink != HA_OFFSET_ERROR &&
           !info->append_insert_at_end) ?
          info->s->state.dellink :
          info->state->data_file_length);
}

my_bool _ma_write_abort_default(MARIA_HA *info __attribute__((unused)))
{
  return 0;
}


/* Write new record to a table */

int maria_write(MARIA_HA *info, uchar *record)
{
  MARIA_SHARE *share= info->s;
  uint i;
  int save_errno;
  MARIA_RECORD_POS filepos;
  uchar *buff;
  my_bool lock_tree= share->concurrent_insert;
  my_bool fatal_error;
  DBUG_ENTER("maria_write");
  DBUG_PRINT("enter",("index_file: %d  data_file: %d",
                      share->kfile.file, info->dfile.file));

  DBUG_EXECUTE_IF("maria_pretend_crashed_table_on_usage",
                  maria_print_error(info->s, HA_ERR_CRASHED);
                  DBUG_RETURN(my_errno= HA_ERR_CRASHED););
  if (share->options & HA_OPTION_READ_ONLY_DATA)
  {
    DBUG_RETURN(my_errno=EACCES);
  }
  if (_ma_readinfo(info,F_WRLCK,1))
    DBUG_RETURN(my_errno);
  dont_break();				/* Dont allow SIGHUP or SIGINT */

  if (share->base.reloc == (ha_rows) 1 &&
      share->base.records == (ha_rows) 1 &&
      info->state->records == (ha_rows) 1)
  {						/* System file */
    my_errno=HA_ERR_RECORD_FILE_FULL;
    goto err2;
  }
  if (info->state->key_file_length >= share->base.margin_key_file_length)
  {
    my_errno=HA_ERR_INDEX_FILE_FULL;
    goto err2;
  }
  if (_ma_mark_file_changed(info))
    goto err2;

  /* Calculate and check all unique constraints */
  for (i=0 ; i < share->state.header.uniques ; i++)
  {
    if (_ma_check_unique(info,share->uniqueinfo+i,record,
                         _ma_unique_hash(share->uniqueinfo+i,record),
                         HA_OFFSET_ERROR))
      goto err2;
  }

  if ((info->opt_flag & OPT_NO_ROWS))
    filepos= HA_OFFSET_ERROR;
  else
  {
    /*
      This may either calculate a record or, or write the record and return
      the record id
    */
    if ((filepos= (*share->write_record_init)(info, record)) ==
        HA_OFFSET_ERROR)
      goto err2;
  }

  /* Write all keys to indextree */
  buff= info->lastkey2;
  for (i=0 ; i < share->base.keys ; i++)
  {
    if (maria_is_key_active(share->state.key_map, i))
    {
      bool local_lock_tree= (lock_tree &&
			     !(info->bulk_insert &&
			       is_tree_inited(&info->bulk_insert[i])));
      if (local_lock_tree)
      {
	rw_wrlock(&share->key_root_lock[i]);
	share->keyinfo[i].version++;
      }
      if (share->keyinfo[i].flag & HA_FULLTEXT )
      {
        if (_ma_ft_add(info,i, buff,record,filepos))
        {
	  if (local_lock_tree)
	    rw_unlock(&share->key_root_lock[i]);
          DBUG_PRINT("error",("Got error: %d on write",my_errno));
          goto err;
        }
      }
      else
      {
        if (share->keyinfo[i].ck_insert(info,i,buff,
                                        _ma_make_key(info,i,buff,record,
                                                     filepos)))
        {
          if (local_lock_tree)
            rw_unlock(&share->key_root_lock[i]);
          DBUG_PRINT("error",("Got error: %d on write",my_errno));
          goto err;
        }
      }

      /* The above changed info->lastkey2. Inform maria_rnext_same(). */
      info->update&= ~HA_STATE_RNEXT_SAME;

      if (local_lock_tree)
        rw_unlock(&share->key_root_lock[i]);
    }
  }
  if (share->calc_write_checksum)
    info->cur_row.checksum= (*share->calc_write_checksum)(info,record);
  if (filepos != HA_OFFSET_ERROR)
  {
    if ((*share->write_record)(info,record))
      goto err;
    if (!share->now_transactional)
      info->state->checksum+= info->cur_row.checksum;
  }
  if (!share->now_transactional)
  {
    if (share->base.auto_key != 0)
    {
      const HA_KEYSEG *keyseg= share->keyinfo[share->base.auto_key-1].seg;
      const uchar *key= record + keyseg->start;
      set_if_bigger(share->state.auto_increment,
                    ma_retrieve_auto_increment(key, keyseg->type));
    }
    info->state->records++;
  }
  info->update= (HA_STATE_CHANGED | HA_STATE_AKTIV | HA_STATE_WRITTEN |
		 HA_STATE_ROW_CHANGED);
  share->state.changed|= STATE_NOT_MOVABLE | STATE_NOT_ZEROFILLED;

  info->cur_row.lastpos= filepos;
  VOID(_ma_writeinfo(info, WRITEINFO_UPDATE_KEYFILE));
  if (info->invalidator != 0)
  {
    DBUG_PRINT("info", ("invalidator... '%s' (update)",
                        share->open_file_name));
    (*info->invalidator)(share->open_file_name);
    info->invalidator=0;
  }

  /*
    Update status of the table. We need to do so after each row write
    for the log tables, as we want the new row to become visible to
    other threads as soon as possible. We don't lock mutex here
    (as it is required by pthread memory visibility rules) as (1) it's
    not critical to use outdated share->is_log_table value (2) locking
    mutex here for every write is too expensive.
  */
  if (share->is_log_table)
    _ma_update_status((void*) info);

  allow_break();				/* Allow SIGHUP & SIGINT */
  DBUG_RETURN(0);

err:
  save_errno= my_errno;
  fatal_error= 0;
  if (my_errno == HA_ERR_FOUND_DUPP_KEY ||
      my_errno == HA_ERR_RECORD_FILE_FULL ||
      my_errno == HA_ERR_NULL_IN_SPATIAL ||
      my_errno == HA_ERR_OUT_OF_MEM)
  {
    if (info->bulk_insert)
    {
      uint j;
      for (j=0 ; j < share->base.keys ; j++)
        maria_flush_bulk_insert(info, j);
    }
    info->errkey= (int) i;
    /*
      We delete keys in the reverse order of insertion. This is the order that
      a rollback would do and is important for CLR_ENDs generated by
      _ma_ft|ck_delete() and write_record_abort() to work (with any other
      order they would cause wrong jumps in the chain).
    */
    while ( i-- > 0)
    {
      if (maria_is_key_active(share->state.key_map, i))
      {
	bool local_lock_tree= (lock_tree &&
			       !(info->bulk_insert &&
				 is_tree_inited(&info->bulk_insert[i])));
	if (local_lock_tree)
	  rw_wrlock(&share->key_root_lock[i]);
        /**
           @todo RECOVERY BUG
           The key deletes below should generate CLR_ENDs
        */
	if (share->keyinfo[i].flag & HA_FULLTEXT)
        {
          if (_ma_ft_del(info,i,buff,record,filepos))
	  {
	    if (local_lock_tree)
	      rw_unlock(&share->key_root_lock[i]);
            break;
	  }
        }
        else
	{
	  uint key_length= _ma_make_key(info,i,buff,record,filepos);
	  if (_ma_ck_delete(info,i,buff,key_length))
	  {
	    if (local_lock_tree)
	      rw_unlock(&share->key_root_lock[i]);
	    break;
	  }
	}
	if (local_lock_tree)
	  rw_unlock(&share->key_root_lock[i]);
      }
    }
  }
  else
    fatal_error= 1;

  if ((*share->write_record_abort)(info))
    fatal_error= 1;
  if (fatal_error)
  {
    maria_print_error(info->s, HA_ERR_CRASHED);
    maria_mark_crashed(info);
  }

  info->update= (HA_STATE_CHANGED | HA_STATE_WRITTEN | HA_STATE_ROW_CHANGED);
  my_errno=save_errno;
err2:
  save_errno=my_errno;
  DBUG_ASSERT(save_errno);
  if (!save_errno)
    save_errno= HA_ERR_INTERNAL_ERROR;          /* Should never happen */
  DBUG_PRINT("error", ("got error: %d", save_errno));
  VOID(_ma_writeinfo(info,WRITEINFO_UPDATE_KEYFILE));
  allow_break();			/* Allow SIGHUP & SIGINT */
  DBUG_RETURN(my_errno=save_errno);
} /* maria_write */


	/* Write one key to btree */

int _ma_ck_write(MARIA_HA *info, uint keynr, uchar *key, uint key_length)
{
  DBUG_ENTER("_ma_ck_write");

  if (info->bulk_insert && is_tree_inited(&info->bulk_insert[keynr]))
  {
    DBUG_RETURN(_ma_ck_write_tree(info, keynr, key, key_length));
  }
  DBUG_RETURN(_ma_ck_write_btree(info, keynr, key, key_length));
} /* _ma_ck_write */


/**********************************************************************
  Insert key into btree (normal case)
**********************************************************************/

static int _ma_ck_write_btree(register MARIA_HA *info, uint keynr, uchar *key,
                              uint key_length)
{
  int error;
  MARIA_KEYDEF *keyinfo=info->s->keyinfo+keynr;
  my_off_t  *root=&info->s->state.key_root[keynr];
  DBUG_ENTER("_ma_ck_write_btree");

  error= _ma_ck_write_btree_with_log(info, keyinfo, key, key_length,
                                     root, keyinfo->write_comp_flag);
  if (info->ft1_to_ft2)
  {
    if (!error)
      error= _ma_ft_convert_to_ft2(info, keynr, key);
    delete_dynamic(info->ft1_to_ft2);
    my_free((uchar*)info->ft1_to_ft2, MYF(0));
    info->ft1_to_ft2=0;
  }
  DBUG_RETURN(error);
} /* _ma_ck_write_btree */


/**
  @brief Write a key to the b-tree

  @retval -1   error
  @retval 0    ok
*/

static int _ma_ck_write_btree_with_log(MARIA_HA *info, MARIA_KEYDEF *keyinfo,
                                       uchar *key, uint key_length,
                                       my_off_t *root, uint comp_flag)
{
  MARIA_SHARE *share= info->s;
  LSN lsn= LSN_IMPOSSIBLE;
  int error;
  my_off_t new_root= *root;
  uchar key_buff[HA_MAX_KEY_BUFF];
  DBUG_ENTER("_ma_ck_write_btree_with_log");

  if (share->now_transactional)
  {
    /* Save original value as the key may change */
    memcpy(key_buff, key, key_length + share->rec_reflength);
  }

  error= _ma_ck_real_write_btree(info, keyinfo, key, key_length, &new_root,
                                 comp_flag);
  if (!error && share->now_transactional)
  {
    uchar log_data[LSN_STORE_SIZE + FILEID_STORE_SIZE +
                   KEY_NR_STORE_SIZE];
    LEX_STRING log_array[TRANSLOG_INTERNAL_PARTS + 2];
    struct st_msg_to_write_hook_for_undo_key msg;

    /* Save if we need to write a clr record */
    info->key_write_undo_lsn[keyinfo->key_nr]= info->trn->undo_lsn;
    lsn_store(log_data, info->trn->undo_lsn);
    key_nr_store(log_data + LSN_STORE_SIZE + FILEID_STORE_SIZE,
                  keyinfo->key_nr);
    key_length+= share->rec_reflength;
    log_array[TRANSLOG_INTERNAL_PARTS + 0].str=    (char*) log_data;
    log_array[TRANSLOG_INTERNAL_PARTS + 0].length= sizeof(log_data);
    log_array[TRANSLOG_INTERNAL_PARTS + 1].str= (char*) key_buff;
    log_array[TRANSLOG_INTERNAL_PARTS + 1].length= key_length;

    msg.root= root;
    msg.value= new_root;
    msg.auto_increment= 0;
    if (share->base.auto_key == ((uint)keyinfo->key_nr + 1))
    {
      const HA_KEYSEG *keyseg= keyinfo->seg;
      uchar *to= key_buff;
      if (keyseg->flag & HA_SWAP_KEY)
      {
        /* We put key from log record to "data record" packing format... */
        uchar reversed[HA_MAX_KEY_BUFF];
        uchar *key_ptr= to;
        uchar *key_end= key_ptr + keyseg->length;
        to= reversed + keyseg->length;
        do
        {
          *--to= *key_ptr++;
        } while (key_ptr != key_end);
      }
      /* ... so that we can read it with: */
      msg.auto_increment=
        ma_retrieve_auto_increment(to, keyseg->type);
      /* and write_hook_for_undo_key_insert() will pick this. */
    }

    if (translog_write_record(&lsn, LOGREC_UNDO_KEY_INSERT,
                              info->trn, info,
                              (translog_size_t)
                              log_array[TRANSLOG_INTERNAL_PARTS + 0].length +
                              key_length,
                              TRANSLOG_INTERNAL_PARTS + 2, log_array,
                              log_data + LSN_STORE_SIZE, &msg))
      error= -1;
  }
  else
  {
    *root= new_root;
    _ma_fast_unlock_key_del(info);
  }
  _ma_unpin_all_pages_and_finalize_row(info, lsn);

  DBUG_RETURN(error);
} /* _ma_ck_write_btree_with_log */


/**
  @brief Write a key to the b-tree

  @retval -1   error
  @retval 0    ok
*/

int _ma_ck_real_write_btree(MARIA_HA *info, MARIA_KEYDEF *keyinfo,
                            uchar *key, uint key_length, my_off_t *root,
                            uint comp_flag)
{
  int error;
  DBUG_ENTER("_ma_ck_real_write_btree");

  /* key_length parameter is used only if comp_flag is SEARCH_FIND */
  if (*root == HA_OFFSET_ERROR ||
      (error= w_search(info, keyinfo, comp_flag, key, key_length,
                       *root, (my_off_t) 0, (uchar*) 0,
                       (MARIA_PINNED_PAGE *) 0, (uchar*) 0, 1)) > 0)
    error= _ma_enlarge_root(info, keyinfo, key, root);
  DBUG_RETURN(error);
} /* _ma_ck_real_write_btree */


/**
  @brief Make a new root with key as only pointer

  @retval -1   error
  @retval 0    ok
*/

int _ma_enlarge_root(MARIA_HA *info, MARIA_KEYDEF *keyinfo, const uchar *key,
                     my_off_t *root)
{
  uint t_length, nod_flag, page_length;
  MARIA_KEY_PARAM s_temp;
  MARIA_SHARE *share= info->s;
  MARIA_PINNED_PAGE tmp_page_link, *page_link= &tmp_page_link;
  int res= 0;
  DBUG_ENTER("_ma_enlarge_root");

  nod_flag= (*root != HA_OFFSET_ERROR) ?  share->base.key_reflength : 0;
  /* Store pointer to prev page if nod */
  _ma_kpointer(info, info->buff + share->keypage_header, *root);
  t_length=(*keyinfo->pack_key)(keyinfo,nod_flag,(uchar*) 0,
				(uchar*) 0, (uchar*) 0, key,&s_temp);
  page_length= share->keypage_header + t_length + nod_flag;

  bzero(info->buff, share->keypage_header);
  _ma_store_keynr(share, info->buff, keyinfo->key_nr);
  _ma_store_page_used(share, info->buff, page_length);
  if (nod_flag)
    _ma_store_keypage_flag(share, info->buff, KEYPAGE_FLAG_ISNOD);
  (*keyinfo->store_key)(keyinfo, info->buff + share->keypage_header +
                        nod_flag, &s_temp);

  /* Mark that info->buff was used */
  info->keyread_buff_used= info->page_changed= 1;
  if ((*root= _ma_new(info, PAGECACHE_PRIORITY_HIGH, &page_link)) ==
      HA_OFFSET_ERROR)
    DBUG_RETURN(-1);

  /*
    Clear unitialized part of page to avoid valgrind/purify warnings
    and to get a clean page that is easier to compress and compare with
    pages generated with redo
  */
  bzero(info->buff + page_length, share->block_size - page_length);


  if (share->now_transactional &&
      _ma_log_new(info, *root, info->buff, page_length, keyinfo->key_nr, 1))
    res= -1;
  if (_ma_write_keypage(info, keyinfo, *root, page_link->write_lock,
                        PAGECACHE_PRIORITY_HIGH, info->buff))
    res= -1;

  DBUG_RETURN(res);
} /* _ma_enlarge_root */


/*
  Search after a position for a key and store it there

  @return
  @retval -1   error
  @retval 0    ok
  @retval > 0  Key should be stored in higher tree
*/

static int w_search(register MARIA_HA *info, register MARIA_KEYDEF *keyinfo,
		    uint comp_flag, uchar *key, uint key_length, my_off_t page,
		    my_off_t father_page, uchar *father_buff,
                    MARIA_PINNED_PAGE *father_page_link, uchar *father_keypos,
		    my_bool insert_last)
{
  int error,flag;
  uint nod_flag, search_key_length;
  uchar *temp_buff,*keypos;
  uchar keybuff[HA_MAX_KEY_BUFF];
  my_bool was_last_key;
  my_off_t next_page, dup_key_pos;
  MARIA_PINNED_PAGE *page_link;
  MARIA_SHARE *share= info->s;
  DBUG_ENTER("w_search");
  DBUG_PRINT("enter",("page: %ld", (long) page));

  search_key_length= (comp_flag & SEARCH_FIND) ? key_length : USE_WHOLE_KEY;
  if (!(temp_buff= (uchar*) my_alloca((uint) keyinfo->block_length+
				      HA_MAX_KEY_BUFF*2)))
    DBUG_RETURN(-1);
  if (!_ma_fetch_keypage(info, keyinfo, page, PAGECACHE_LOCK_WRITE,
                         DFLT_INIT_HITS, temp_buff, 0, &page_link))
    goto err;

  flag=(*keyinfo->bin_search)(info,keyinfo,temp_buff,key,search_key_length,
			      comp_flag, &keypos, keybuff, &was_last_key);
  nod_flag= _ma_test_if_nod(share, temp_buff);
  if (flag == 0)
  {
    uint tmp_key_length;
	/* get position to record with duplicated key */
    tmp_key_length=(*keyinfo->get_key)(keyinfo,nod_flag,&keypos,keybuff);
    if (tmp_key_length)
      dup_key_pos= _ma_dpos(info,0,keybuff+tmp_key_length);
    else
      dup_key_pos= HA_OFFSET_ERROR;

    if (keyinfo->flag & HA_FULLTEXT)
    {
      uint off;
      int  subkeys;

      get_key_full_length_rdonly(off, keybuff);
      subkeys=ft_sintXkorr(keybuff+off);
      comp_flag=SEARCH_SAME;
      if (subkeys >= 0)
      {
        /* normal word, one-level tree structure */
        flag=(*keyinfo->bin_search)(info, keyinfo, temp_buff, key,
                                    USE_WHOLE_KEY, comp_flag,
                                    &keypos, keybuff, &was_last_key);
      }
      else
      {
        /* popular word. two-level tree. going down */
        my_off_t root=dup_key_pos;
        keyinfo= &share->ft2_keyinfo;
        get_key_full_length_rdonly(off, key);
        key+=off;
        /* we'll modify key entry 'in vivo' */
        keypos-= keyinfo->keylength + nod_flag;
        error= _ma_ck_real_write_btree(info, keyinfo, key, 0,
                                      &root, comp_flag);
        _ma_dpointer(info, keypos+HA_FT_WLEN, root);
        subkeys--; /* should there be underflow protection ? */
        DBUG_ASSERT(subkeys < 0);
        ft_intXstore(keypos, subkeys);
        if (!error)
        {
          page_link->changed= 1;
          error= _ma_write_keypage(info, keyinfo, page,
                                   PAGECACHE_LOCK_LEFT_WRITELOCKED,
                                   DFLT_INIT_HITS, temp_buff);
        }
        my_afree((uchar*) temp_buff);
        DBUG_RETURN(error);
      }
    }
    else /* not HA_FULLTEXT, normal HA_NOSAME key */
    {
      DBUG_PRINT("warning", ("Duplicate key"));
      info->dup_key_pos= dup_key_pos;
      my_afree((uchar*) temp_buff);
      my_errno=HA_ERR_FOUND_DUPP_KEY;
      DBUG_RETURN(-1);
    }
  }
  if (flag == MARIA_FOUND_WRONG_KEY)
    DBUG_RETURN(-1);
  if (!was_last_key)
    insert_last=0;
  next_page= _ma_kpos(nod_flag,keypos);
  if (next_page == HA_OFFSET_ERROR ||
      (error= w_search(info, keyinfo, comp_flag, key, key_length, next_page,
                       page, temp_buff, page_link, keypos, insert_last)) > 0)
  {
    error= _ma_insert(info, keyinfo, key, temp_buff, keypos, page, keybuff,
                      father_page, father_buff, father_page_link,
                      father_keypos, insert_last);
    page_link->changed= 1;
    if (_ma_write_keypage(info, keyinfo, page, PAGECACHE_LOCK_LEFT_WRITELOCKED,
                          DFLT_INIT_HITS,temp_buff))
      goto err;
  }
  my_afree((uchar*) temp_buff);
  DBUG_RETURN(error);
err:
  my_afree((uchar*) temp_buff);
  DBUG_PRINT("exit",("Error: %d",my_errno));
  DBUG_RETURN (-1);
} /* w_search */


/*
  Insert new key.

  SYNOPSIS
    _ma_insert()
    info                        Open table information.
    keyinfo                     Key definition information.
    key                         New key
    anc_buff                    Key page (beginning).
    key_pos                     Position in key page where to insert.
    anc_page			Page number for anc_buff
    key_buff                    Copy of previous key.
    father_buff                 parent key page for balancing.
    father_key_pos              position in parent key page for balancing.
    father_page                 position of parent key page in file.
    insert_last                 If to append at end of page.

  DESCRIPTION
    Insert new key at right of key_pos.
    Note that caller must save anc_buff

    This function writes log records for all changed pages
    (Including anc_buff and father page)

  RETURN
    < 0         Error.
    0           OK
    1           If key contains key to upper level (from balance page)
    2           If key contains key to upper level (from split space)
*/

int _ma_insert(register MARIA_HA *info, register MARIA_KEYDEF *keyinfo,
	       uchar *key, uchar *anc_buff, uchar *key_pos, my_off_t anc_page,
               uchar *key_buff, my_off_t father_page, uchar *father_buff,
               MARIA_PINNED_PAGE *father_page_link, uchar *father_key_pos,
               my_bool insert_last)
{
  uint a_length, nod_flag, org_anc_length;
  int t_length;
  uchar *endpos, *prev_key;
  MARIA_KEY_PARAM s_temp;
  MARIA_SHARE *share= info->s;
  DBUG_ENTER("_ma_insert");
  DBUG_PRINT("enter",("key_pos: 0x%lx", (ulong) key_pos));
  DBUG_EXECUTE("key", _ma_print_key(DBUG_FILE,keyinfo->seg,key,
                                    USE_WHOLE_KEY););

  _ma_get_used_and_nod(share, anc_buff, a_length, nod_flag);
  org_anc_length= a_length;
  endpos= anc_buff+ a_length;
  prev_key= (key_pos == anc_buff + share->keypage_header + nod_flag ?
             (uchar*) 0 : key_buff);
  t_length=(*keyinfo->pack_key)(keyinfo,nod_flag,
				(key_pos == endpos ? (uchar*) 0 : key_pos),
				prev_key, prev_key,
				key,&s_temp);
#ifndef DBUG_OFF
  if (key_pos != anc_buff + share->keypage_header + nod_flag &&
      (keyinfo->flag & (HA_BINARY_PACK_KEY | HA_PACK_KEY)))
  {
    DBUG_DUMP("prev_key",(uchar*) key_buff, _ma_keylength(keyinfo,key_buff));
  }
  if (keyinfo->flag & HA_PACK_KEY)
  {
    DBUG_PRINT("test",("t_length: %d  ref_len: %d",
		       t_length,s_temp.ref_length));
    DBUG_PRINT("test",("n_ref_len: %d  n_length: %d  key_pos: 0x%lx",
		       s_temp.n_ref_length, s_temp.n_length, (long) s_temp.key));
  }
#endif
  if (t_length > 0)
  {
    if (t_length >= keyinfo->maxlength*2+MAX_POINTER_LENGTH)
    {
      maria_print_error(share, HA_ERR_CRASHED);
      my_errno=HA_ERR_CRASHED;
      DBUG_RETURN(-1);
    }
    bmove_upp((uchar*) endpos+t_length,(uchar*) endpos,(uint) (endpos-key_pos));
  }
  else
  {
    if (-t_length >= keyinfo->maxlength*2+MAX_POINTER_LENGTH)
    {
      maria_print_error(share, HA_ERR_CRASHED);
      my_errno=HA_ERR_CRASHED;
      DBUG_RETURN(-1);
    }
    bmove(key_pos,key_pos-t_length,(uint) (endpos-key_pos)+t_length);
  }
  (*keyinfo->store_key)(keyinfo,key_pos,&s_temp);
  a_length+=t_length;
  _ma_store_page_used(share, anc_buff, a_length);

  /*
    Check if the new key fits totally into the the page
    (anc_buff is big enough to contain a full page + one key)
  */
  if (a_length <= (uint) keyinfo->block_length - KEYPAGE_CHECKSUM_SIZE)
  {
    if (keyinfo->block_length - KEYPAGE_CHECKSUM_SIZE - a_length < 32 &&
        (keyinfo->flag & HA_FULLTEXT) && key_pos == endpos &&
        share->base.key_reflength <= share->base.rec_reflength &&
        share->options & (HA_OPTION_PACK_RECORD | HA_OPTION_COMPRESS_RECORD))
    {
      /*
        Normal word. One-level tree. Page is almost full.
        Let's consider converting.
        We'll compare 'key' and the first key at anc_buff
      */
      const uchar *a= key, *b= anc_buff + share->keypage_header + nod_flag;
      uint alen, blen, ft2len= share->ft2_keyinfo.keylength;
      /* the very first key on the page is always unpacked */
      DBUG_ASSERT((*b & 128) == 0);
#if HA_FT_MAXLEN >= 127
      blen= mi_uint2korr(b); b+=2;
#else
      blen= *(uchar*) b++;
#endif
      get_key_length(alen,a);
      DBUG_ASSERT(info->ft1_to_ft2==0);
      if (alen == blen &&
          ha_compare_text(keyinfo->seg->charset, (uchar*) a, alen,
                          (uchar*) b, blen, 0, 0) == 0)
      {
        /* Yup. converting */
        info->ft1_to_ft2=(DYNAMIC_ARRAY *)
          my_malloc(sizeof(DYNAMIC_ARRAY), MYF(MY_WME));
        my_init_dynamic_array(info->ft1_to_ft2, ft2len, 300, 50);

        /*
          Now, adding all keys from the page to dynarray
          if the page is a leaf (if not keys will be deleted later)
        */
        if (!nod_flag)
        {
          /*
            Let's leave the first key on the page, though, because
            we cannot easily dispatch an empty page here
          */
          b+=blen+ft2len+2;
          for (a=anc_buff+a_length ; b < a ; b+=ft2len+2)
            insert_dynamic(info->ft1_to_ft2, (uchar*) b);

          /* fixing the page's length - it contains only one key now */
          _ma_store_page_used(share, anc_buff, share->keypage_header + blen +
                              ft2len + 2);
        }
        /* the rest will be done when we're back from recursion */
      }
    }
    else
    {
      if (share->now_transactional &&
          _ma_log_add(info, anc_page, anc_buff, (uint) (endpos - anc_buff),
                      key_pos, s_temp.changed_length, t_length, 0))
        DBUG_RETURN(-1);
    }
    DBUG_RETURN(0);				/* There is room on page */
  }
  /* Page is full */
  if (nod_flag)
    insert_last=0;
  if (!(keyinfo->flag & (HA_VAR_LENGTH_KEY | HA_BINARY_PACK_KEY)) &&
      father_buff && !insert_last && !info->quick_mode)
  {
    s_temp.key_pos= key_pos;
    father_page_link->changed= 1;
    DBUG_RETURN(_ma_balance_page(info, keyinfo, key, anc_buff, anc_page,
                                 father_page, father_buff, father_key_pos,
                                 &s_temp));
  }
  DBUG_RETURN(_ma_split_page(info, keyinfo, key, anc_page,
                             anc_buff, org_anc_length,
                             key_pos, s_temp.changed_length, t_length,
                             key_buff, insert_last));
} /* _ma_insert */


/**
  @brief split a full page in two and assign emerging item to key

  @fn _ma_split_page()
    info	     Maria handler
    keyinfo	     Key handler
    key		     Buffer for middle key
    split_page       Address on disk for split_buff
    split_buff	     Page buffer for page that should be split
    org_split_length Original length of split_buff before key was inserted
    inserted_key_pos Address in buffer where key was inserted
    changed_length   Number of bytes changed at 'inserted_key_pos'
    move_length	     Number of bytes buffer was moved when key was inserted
    key_buff	     Key buffer to use for temporary storage of key
    insert_last_key  If we are insert key on rightmost key page

  @note
    split_buff is not stored on disk    (caller has to do this)

  @return
  @retval 2   ok  (Middle key up from _ma_insert())
  @retval -1  error
*/

int _ma_split_page(register MARIA_HA *info, register MARIA_KEYDEF *keyinfo,
		   uchar *key, my_off_t split_page, uchar *split_buff,
                   uint org_split_length,
                   uchar *inserted_key_pos, uint changed_length,
                   int move_length,
                   uchar *key_buff, my_bool insert_last_key)
{
  uint length,a_length,key_ref_length,t_length,nod_flag,key_length;
  uint page_length, split_length;
  uchar *key_pos,*pos, *after_key, *new_buff;
  my_off_t new_pos;
  MARIA_KEY_PARAM s_temp;
  MARIA_PINNED_PAGE tmp_page_link, *page_link= &tmp_page_link;
  MARIA_SHARE *share= info->s;
  int res;
  DBUG_ENTER("_ma_split_page");

  LINT_INIT(after_key);
  DBUG_DUMP("buff", split_buff, _ma_get_page_used(share, split_buff));

  info->page_changed=1;			/* Info->buff is used */
  info->keyread_buff_used=1;
  new_buff= info->buff;
  nod_flag= _ma_test_if_nod(share, split_buff);
  key_ref_length= share->keypage_header + nod_flag;
  if (insert_last_key)
    key_pos= _ma_find_last_pos(info, keyinfo, split_buff,
                               key_buff, &key_length,
                               &after_key);
  else
    key_pos= _ma_find_half_pos(info, nod_flag, keyinfo, split_buff, key_buff,
                               &key_length, &after_key);
  if (!key_pos)
    DBUG_RETURN(-1);

  split_length= (uint) (key_pos - split_buff);
  a_length= _ma_get_page_used(share, split_buff);
  _ma_store_page_used(share, split_buff, split_length);

  key_pos=after_key;
  if (nod_flag)
  {
    DBUG_PRINT("test",("Splitting nod"));
    pos=key_pos-nod_flag;
    memcpy((uchar*) new_buff + share->keypage_header, (uchar*) pos,
           (size_t) nod_flag);
  }

  /* Move middle item to key and pointer to new page */
  if ((new_pos= _ma_new(info, PAGECACHE_PRIORITY_HIGH, &page_link)) ==
      HA_OFFSET_ERROR)
    DBUG_RETURN(-1);
  _ma_kpointer(info, _ma_move_key(keyinfo,key,key_buff),new_pos);

  /* Store new page */
  if (!(*keyinfo->get_key)(keyinfo,nod_flag,&key_pos,key_buff))
    DBUG_RETURN(-1);

  t_length=(*keyinfo->pack_key)(keyinfo,nod_flag,(uchar *) 0,
				(uchar*) 0, (uchar*) 0,
				key_buff, &s_temp);
  length=(uint) ((split_buff + a_length) - key_pos);
  memcpy((uchar*) new_buff+key_ref_length+t_length,(uchar*) key_pos,
	 (size_t) length);
  (*keyinfo->store_key)(keyinfo,new_buff+key_ref_length,&s_temp);
  page_length= length + t_length + key_ref_length;

  bzero(new_buff, share->keypage_header);
  if (nod_flag)
    _ma_store_keypage_flag(share, new_buff, KEYPAGE_FLAG_ISNOD);
  _ma_store_page_used(share, new_buff, page_length);
  /* Copy key number */
  new_buff[share->keypage_header - KEYPAGE_USED_SIZE - KEYPAGE_KEYID_SIZE -
             KEYPAGE_FLAG_SIZE]=
    split_buff[share->keypage_header - KEYPAGE_USED_SIZE -
               KEYPAGE_KEYID_SIZE - KEYPAGE_FLAG_SIZE];

  res= 2;                                       /* Middle key up */
  if (share->now_transactional &&
      _ma_log_new(info, new_pos, new_buff, page_length, keyinfo->key_nr, 0))
    res= -1;
  bzero(new_buff + page_length, share->block_size - page_length);

  if (_ma_write_keypage(info, keyinfo, new_pos, page_link->write_lock,
                        DFLT_INIT_HITS, new_buff))
    res= -1;

  /* Save changes to split pages */
  if (share->now_transactional &&
      _ma_log_split(info, split_page, split_buff, org_split_length,
                    split_length,
                    inserted_key_pos, changed_length, move_length,
                    KEY_OP_NONE, (uchar*) 0, 0, 0))
    res= -1;

  DBUG_DUMP("key",(uchar*) key, _ma_keylength(keyinfo,key));
  DBUG_RETURN(res);
} /* _ma_split_page */


/*
  Calculate how to much to move to split a page in two

  Returns pointer to start of key.
  key will contain the key.
  return_key_length will contain the length of key
  after_key will contain the position to where the next key starts
*/

uchar *_ma_find_half_pos(MARIA_HA *info, uint nod_flag, MARIA_KEYDEF *keyinfo,
                         uchar *page, uchar *key, uint *return_key_length,
			 uchar **after_key)
{
  uint keys,length,key_ref_length;
  uchar *end,*lastpos;
  MARIA_SHARE *share= info->s;
  DBUG_ENTER("_ma_find_half_pos");

  key_ref_length= share->keypage_header + nod_flag;
  length= _ma_get_page_used(share, page) - key_ref_length;
  page+= key_ref_length;                        /* Point to first key */
  if (!(keyinfo->flag &
	(HA_PACK_KEY | HA_SPACE_PACK_USED | HA_VAR_LENGTH_KEY |
	 HA_BINARY_PACK_KEY)))
  {
    key_ref_length=keyinfo->keylength+nod_flag;
    keys=length/(key_ref_length*2);
    *return_key_length=keyinfo->keylength;
    end=page+keys*key_ref_length;
    *after_key=end+key_ref_length;
    memcpy(key,end,key_ref_length);
    DBUG_RETURN(end);
  }

  end=page+length/2-key_ref_length;		/* This is aprox. half */
  *key='\0';
  do
  {
    lastpos=page;
    if (!(length=(*keyinfo->get_key)(keyinfo,nod_flag,&page,key)))
      DBUG_RETURN(0);
  } while (page < end);
  *return_key_length=length;
  *after_key=page;
  DBUG_PRINT("exit",("returns: 0x%lx  page: 0x%lx  half: 0x%lx",
                     (long) lastpos, (long) page, (long) end));
  DBUG_RETURN(lastpos);
} /* _ma_find_half_pos */


/*
  Split buffer at last key
  Returns pointer to the start of the key before the last key
  key will contain the last key
*/

static uchar *_ma_find_last_pos(MARIA_HA *info, MARIA_KEYDEF *keyinfo,
                                uchar *page,
				uchar *key, uint *return_key_length,
				uchar **after_key)
{
  uint keys,length,last_length,key_ref_length;
  uchar *end,*lastpos,*prevpos;
  uchar key_buff[HA_MAX_KEY_BUFF];
  MARIA_SHARE *share= info->s;
  DBUG_ENTER("_ma_find_last_pos");

  key_ref_length= share->keypage_header;
  length= _ma_get_page_used(share, page) - key_ref_length;
  page+=key_ref_length;
  if (!(keyinfo->flag &
	(HA_PACK_KEY | HA_SPACE_PACK_USED | HA_VAR_LENGTH_KEY |
	 HA_BINARY_PACK_KEY)))
  {
    keys=length/keyinfo->keylength-2;
    *return_key_length=length=keyinfo->keylength;
    end=page+keys*length;
    *after_key=end+length;
    memcpy(key,end,length);
    DBUG_RETURN(end);
  }

  LINT_INIT(prevpos);
  LINT_INIT(last_length);
  end=page+length-key_ref_length;
  *key='\0';
  length=0;
  lastpos=page;
  while (page < end)
  {
    prevpos=lastpos; lastpos=page;
    last_length=length;
    memcpy(key, key_buff, length);		/* previous key */
    if (!(length=(*keyinfo->get_key)(keyinfo,0,&page,key_buff)))
    {
      maria_print_error(keyinfo->share, HA_ERR_CRASHED);
      my_errno=HA_ERR_CRASHED;
      DBUG_RETURN(0);
    }
  }
  *return_key_length=last_length;
  *after_key=lastpos;
  DBUG_PRINT("exit",("returns: 0x%lx  page: 0x%lx  end: 0x%lx",
                     (long) prevpos,(long) page,(long) end));
  DBUG_RETURN(prevpos);
} /* _ma_find_last_pos */


/**
  @brief Balance page with static size keys with page on right/left

  @param key 	Middle key will be stored here

  @notes
    Father_buff will always be changed
    Caller must handle saving of curr_buff

  @return
  @retval  0   Balance was done (father buff is saved)
  @retval  1   Middle key up    (father buff is not saved)
  @retval  -1  Error
*/

static int _ma_balance_page(register MARIA_HA *info, MARIA_KEYDEF *keyinfo,
			    uchar *key, uchar *curr_buff,
                            my_off_t curr_page,
                            my_off_t father_page, uchar *father_buff,
                            uchar *father_key_pos, MARIA_KEY_PARAM *s_temp)
{
  MARIA_PINNED_PAGE *next_page_link;
  MARIA_PINNED_PAGE tmp_page_link, *new_page_link= &tmp_page_link;
  MARIA_SHARE *share= info->s;
  my_bool right;
  uint k_length,father_length,father_keylength,nod_flag,curr_keylength;
  uint right_length,left_length,new_right_length,new_left_length,extra_length;
  uint keys, tmp_length, extra_buff_length;
  uchar *pos,*buff,*extra_buff, *parting_key;
  my_off_t next_page,new_pos;
  uchar tmp_part_key[HA_MAX_KEY_BUFF];
  DBUG_ENTER("_ma_balance_page");

  k_length=keyinfo->keylength;
  father_length= _ma_get_page_used(share, father_buff);
  father_keylength= k_length + share->base.key_reflength;
  nod_flag= _ma_test_if_nod(share, curr_buff);
  curr_keylength=k_length+nod_flag;
  info->page_changed=1;

  if ((father_key_pos != father_buff+father_length &&
       (info->state->records & 1)) ||
      father_key_pos == father_buff+ share->keypage_header +
      share->base.key_reflength)
  {
    right=1;
    next_page= _ma_kpos(share->base.key_reflength,
			father_key_pos+father_keylength);
    buff=info->buff;
    DBUG_PRINT("info", ("use right page: %lu", (ulong) next_page));
  }
  else
  {
    right=0;
    father_key_pos-=father_keylength;
    next_page= _ma_kpos(share->base.key_reflength,father_key_pos);
    /* Move curr_buff so that it's on the left */
    buff= curr_buff;
    curr_buff= info->buff;
    DBUG_PRINT("info", ("use left page: %lu", (ulong) next_page));
  }					/* father_key_pos ptr to parting key */

  if (!_ma_fetch_keypage(info,keyinfo, next_page, PAGECACHE_LOCK_WRITE,
                         DFLT_INIT_HITS, info->buff, 0, &next_page_link))
    goto err;
  next_page_link->changed= 1;
  DBUG_DUMP("next", info->buff, _ma_get_page_used(share, info->buff));

  /* Test if there is room to share keys */
  left_length= _ma_get_page_used(share, curr_buff);
  right_length= _ma_get_page_used(share, buff);
  keys= ((left_length+right_length-share->keypage_header*2-nod_flag*2)/
         curr_keylength);

  if ((right ? right_length : left_length) + curr_keylength <=
      (uint) keyinfo->block_length - KEYPAGE_CHECKSUM_SIZE)
  {
    /* Enough space to hold all keys in the two buffers ; Balance bufferts */
    new_left_length= share->keypage_header+nod_flag+(keys/2)*curr_keylength;
    new_right_length=share->keypage_header+nod_flag+(((keys+1)/2)*
                                                       curr_keylength);
    _ma_store_page_used(share, curr_buff, new_left_length);
    _ma_store_page_used(share, buff, new_right_length);

    DBUG_PRINT("info", ("left_length: %u -> %u  right_length: %u -> %u",
                        left_length, new_left_length,
                        right_length, new_right_length));
    if (left_length < new_left_length)
    {
      uint length;
      DBUG_PRINT("info", ("move keys to end of buff"));

      /* Move keys buff -> curr_buff */
      pos=curr_buff+left_length;
      memcpy(pos,father_key_pos, (size_t) k_length);
      memcpy(pos+k_length, buff + share->keypage_header,
	     (size_t) (length=new_left_length - left_length - k_length));
      pos= buff + share->keypage_header + length;
      memcpy(father_key_pos, pos, (size_t) k_length);
      bmove(buff + share->keypage_header, pos + k_length, new_right_length);

      if (share->now_transactional)
      {
        if (right)
        {
          /*
            Log changes to page on left
            The original page is on the left and stored in curr_buff
            We have on the page the newly inserted key and data
            from buff added last on the page
          */
          if (_ma_log_split(info, curr_page, curr_buff,
                            left_length - s_temp->move_length,
                            new_left_length,
                            s_temp->key_pos, s_temp->changed_length,
                            s_temp->move_length,
                            KEY_OP_ADD_SUFFIX,
                            curr_buff + left_length,
                            new_left_length - left_length,
                            new_left_length - left_length+ k_length))
            goto err;
          /*
            Log changes to page on right
            This contains the original data with some keys deleted from
            start of page
          */
          if (_ma_log_prefix(info, next_page, buff, 0,
                             ((int) new_right_length - (int) right_length)))
            goto err;
        }
        else
        {
          /*
            Log changes to page on right (the original page) which is in buff
            Data is removed from start of page
            The inserted key may be in buff or moved to curr_buff
          */
          if (_ma_log_del_prefix(info, curr_page, buff,
                                 right_length - s_temp->changed_length,
                                 new_right_length,
                                 s_temp->key_pos, s_temp->changed_length,
                                 s_temp->move_length))
            goto err;
          /*
            Log changes to page on left, which has new data added last
          */
          if (_ma_log_suffix(info, next_page, curr_buff,
                             left_length, new_left_length))
            goto err;
        }
      }
    }
    else
    {
      uint length;
      DBUG_PRINT("info", ("move keys to start of buff"));

      bmove_upp(buff + new_right_length, buff + right_length,
		right_length - share->keypage_header);
      length= new_right_length -right_length - k_length;
      memcpy(buff + share->keypage_header + length, father_key_pos,
             (size_t) k_length);
      pos=curr_buff+new_left_length;
      memcpy(father_key_pos, pos, (size_t) k_length);
      memcpy(buff + share->keypage_header, pos+k_length, (size_t) length);

      if (share->now_transactional)
      {
        if (right)
        {
          /*
            Log changes to page on left
            The original page is on the left and stored in curr_buff
            The page is shortened from end and the key may be on the page
          */
          if (_ma_log_split(info, curr_page, curr_buff,
                            left_length - s_temp->move_length,
                            new_left_length,
                            s_temp->key_pos, s_temp->changed_length,
                            s_temp->move_length,
                            KEY_OP_NONE, (uchar*) 0, 0, 0))
            goto err;
          /*
            Log changes to page on right
            This contains the original data, with some data from cur_buff
            added first
          */
          if (_ma_log_prefix(info, next_page, buff,
                             (uint) (new_right_length - right_length),
                             (int) (new_right_length - right_length)))
            goto err;
        }
        else
        {
          /*
            Log changes to page on right (the original page) which is in buff
            We have on the page the newly inserted key and data
            from buff added first on the page
          */
          uint diff_length= new_right_length - right_length;
          if (_ma_log_split(info, curr_page, buff,
                            left_length - s_temp->move_length,
                            new_right_length,
                            s_temp->key_pos + diff_length,
                            s_temp->changed_length,
                            s_temp->move_length,
                            KEY_OP_ADD_PREFIX,
                            buff + share->keypage_header,
                            diff_length, diff_length + k_length))
            goto err;
          /*
            Log changes to page on left, which is shortened from end
          */
          if (_ma_log_suffix(info, next_page, curr_buff,
                             left_length, new_left_length))
            goto err;
        }
      }
    }

    /* Log changes to father (one level up) page */

    if (share->now_transactional &&
        _ma_log_change(info, father_page, father_buff, father_key_pos,
                       k_length))
      goto err;

    /*
      next_page_link->changed is marked as true above and fathers
      page_link->changed is marked as true in caller
    */
    if (_ma_write_keypage(info, keyinfo, next_page,
                          PAGECACHE_LOCK_LEFT_WRITELOCKED,
                          DFLT_INIT_HITS, info->buff) ||
        _ma_write_keypage(info, keyinfo, father_page,
                          PAGECACHE_LOCK_LEFT_WRITELOCKED, DFLT_INIT_HITS,
                          father_buff))
      goto err;
    DBUG_RETURN(0);
  }

  /* curr_buff[] and buff[] are full, lets split and make new nod */

  extra_buff= info->buff+share->base.max_key_block_length;
  new_left_length= new_right_length= (share->keypage_header + nod_flag +
                                      (keys+1) / 3 * curr_keylength);
  /*
    5 is the minum number of keys we can have here. This comes from
    the fact that each full page can store at least 2 keys and in this case
    we have a 'split' key, ie 2+2+1 = 5
  */
  if (keys == 5)				/* Too few keys to balance */
    new_left_length-=curr_keylength;
  extra_length= (nod_flag + left_length + right_length -
                 new_left_length - new_right_length - curr_keylength);
  extra_buff_length= extra_length + share->keypage_header;
  DBUG_PRINT("info",("left_length: %d  right_length: %d  new_left_length: %d  new_right_length: %d  extra_length: %d",
                     left_length, right_length,
                     new_left_length, new_right_length,
                     extra_length));
  _ma_store_page_used(share, curr_buff, new_left_length);
  _ma_store_page_used(share, buff, new_right_length);

  bzero(extra_buff, share->keypage_header);
  if (nod_flag)
    _ma_store_keypage_flag(share, extra_buff, KEYPAGE_FLAG_ISNOD);
  /* Copy key number */
  extra_buff[share->keypage_header - KEYPAGE_USED_SIZE - KEYPAGE_KEYID_SIZE -
             KEYPAGE_FLAG_SIZE]=
    buff[share->keypage_header - KEYPAGE_USED_SIZE - KEYPAGE_KEYID_SIZE -
         KEYPAGE_FLAG_SIZE];
  _ma_store_page_used(share, extra_buff, extra_buff_length);

  /* move first largest keys to new page  */
  pos=buff+right_length-extra_length;
  memcpy(extra_buff + share->keypage_header, pos, extra_length);
  /* Zero old data from buffer */
  bzero(extra_buff + extra_buff_length,
        share->block_size - extra_buff_length);

  /* Save new parting key between buff and extra_buff */
  memcpy(tmp_part_key, pos-k_length,k_length);
  /* Make place for new keys */
  bmove_upp(buff+ new_right_length, pos - k_length,
            right_length - extra_length - k_length - share->keypage_header);
  /* Copy keys from left page */
  pos= curr_buff+new_left_length;
  memcpy(buff + share->keypage_header, pos + k_length,
         (size_t) (tmp_length= left_length - new_left_length - k_length));
  /* Copy old parting key */
  parting_key= buff + share->keypage_header + tmp_length;
  memcpy(parting_key, father_key_pos, (size_t) k_length);

  /* Move new parting keys up to caller */
  memcpy((right ? key : father_key_pos),pos,(size_t) k_length);
  memcpy((right ? father_key_pos : key),tmp_part_key, k_length);

  if ((new_pos= _ma_new(info, DFLT_INIT_HITS, &new_page_link))
      == HA_OFFSET_ERROR)
    goto err;
  _ma_kpointer(info,key+k_length,new_pos);

  if (share->now_transactional)
  {
    if (right)
    {
      /*
        Page order according to key values:
        orignal_page (curr_buff), next_page (buff), extra_buff

        cur_buff is shortened,
        buff is getting new keys at start and shortened from end.
        extra_buff is new page

        Note that extra_buff (largest key parts) will be stored at the
        place of the original 'right' page (next_page) and right page (buff)
        will be stored at new_pos.

        This makes the log entries smaller as right_page contains all
        data to generate the data extra_buff
      */

      /*
        Log changes to page on left (page shortened page at end)
      */
      if (_ma_log_split(info, curr_page, curr_buff,
                        left_length - s_temp->move_length, new_left_length,
                        s_temp->key_pos, s_temp->changed_length,
                        s_temp->move_length,
                        KEY_OP_NONE, (uchar*) 0, 0, 0))
        goto err;
      /*
        Log changes to right page (stored at next page)
        This contains the last 'extra_buff' from 'buff'
      */
      if (_ma_log_prefix(info, next_page, extra_buff,
                         0, (int) (extra_buff_length - right_length)))
        goto err;

      /*
        Log changes to middle page, which is stored at the new page
        position
      */
      if (_ma_log_new(info, new_pos, buff, new_right_length,
                      keyinfo->key_nr, 0))
        goto err;
    }
    else
    {
      /*
        Log changes to page on right (the original page) which is in buff
        This contains the original data, with some data from curr_buff
        added first and shortened at end
      */
      int data_added_first= left_length - new_left_length;
      if (_ma_log_key_middle(info, curr_page, buff,
                             new_right_length,
                             data_added_first,
                             data_added_first,
                             extra_length,
                             s_temp->key_pos,
                             s_temp->changed_length,
                             s_temp->move_length))
        goto err;

      /* Log changes to page on left, which is shortened from end */
      if (_ma_log_suffix(info, next_page, curr_buff,
                         left_length, new_left_length))
        goto err;

      /* Log change to rightmost (new) page */
      if (_ma_log_new(info, new_pos, extra_buff,
                      extra_buff_length, keyinfo->key_nr, 0))
        goto err;
    }

    /* Log changes to father (one level up) page */
    if (share->now_transactional &&
        _ma_log_change(info, father_page, father_buff, father_key_pos,
                       k_length))
      goto err;
  }

  if (_ma_write_keypage(info, keyinfo, (right ? new_pos : next_page),
                        (right ? new_page_link->write_lock :
                         PAGECACHE_LOCK_LEFT_WRITELOCKED),
                        DFLT_INIT_HITS, info->buff) ||
      _ma_write_keypage(info, keyinfo, (right ? next_page : new_pos),
                        (!right ? new_page_link->write_lock :
                         PAGECACHE_LOCK_LEFT_WRITELOCKED),
                        DFLT_INIT_HITS, extra_buff))
    goto err;

  DBUG_RETURN(1);				/* Middle key up */

err:
  DBUG_RETURN(-1);
} /* _ma_balance_page */


/**********************************************************************
 *                Bulk insert code                                    *
 **********************************************************************/

typedef struct {
  MARIA_HA *info;
  uint keynr;
} bulk_insert_param;


static int _ma_ck_write_tree(register MARIA_HA *info, uint keynr, uchar *key,
                             uint key_length)
{
  int error;
  DBUG_ENTER("_ma_ck_write_tree");

  error= (tree_insert(&info->bulk_insert[keynr], key,
                      key_length + info->s->rec_reflength,
                      info->bulk_insert[keynr].custom_arg) ? 0 :
          HA_ERR_OUT_OF_MEM) ;

  DBUG_RETURN(error);
} /* _ma_ck_write_tree */


/* typeof(_ma_keys_compare)=qsort_cmp2 */

static int keys_compare(bulk_insert_param *param, uchar *key1, uchar *key2)
{
  uint not_used[2];
  return ha_key_cmp(param->info->s->keyinfo[param->keynr].seg,
                    key1, key2, USE_WHOLE_KEY, SEARCH_SAME,
                    not_used);
}


static int keys_free(uchar *key, TREE_FREE mode, bulk_insert_param *param)
{
  /*
    Probably I can use info->lastkey here, but I'm not sure,
    and to be safe I'd better use local lastkey.
  */
  MARIA_SHARE *share= param->info->s;
  uchar lastkey[HA_MAX_KEY_BUFF];
  uint keylen;
  MARIA_KEYDEF *keyinfo;

  switch (mode) {
  case free_init:
    if (share->concurrent_insert)
    {
      rw_wrlock(&share->key_root_lock[param->keynr]);
      share->keyinfo[param->keynr].version++;
    }
    return 0;
  case free_free:
    keyinfo=share->keyinfo+param->keynr;
    keylen= _ma_keylength(keyinfo, key);
    memcpy(lastkey, key, keylen);
    return _ma_ck_write_btree(param->info, param->keynr, lastkey,
                              keylen - share->rec_reflength);
  case free_end:
    if (share->concurrent_insert)
      rw_unlock(&share->key_root_lock[param->keynr]);
    return 0;
  }
  return -1;
}


int maria_init_bulk_insert(MARIA_HA *info, ulong cache_size, ha_rows rows)
{
  MARIA_SHARE *share= info->s;
  MARIA_KEYDEF *key=share->keyinfo;
  bulk_insert_param *params;
  uint i, num_keys, total_keylength;
  ulonglong key_map;
  DBUG_ENTER("_ma_init_bulk_insert");
  DBUG_PRINT("enter",("cache_size: %lu", cache_size));

  DBUG_ASSERT(!info->bulk_insert &&
	      (!rows || rows >= MARIA_MIN_ROWS_TO_USE_BULK_INSERT));

  maria_clear_all_keys_active(key_map);
  for (i=total_keylength=num_keys=0 ; i < share->base.keys ; i++)
  {
    if (! (key[i].flag & HA_NOSAME) && (share->base.auto_key != i + 1) &&
        maria_is_key_active(share->state.key_map, i))
    {
      num_keys++;
      maria_set_key_active(key_map, i);
      total_keylength+=key[i].maxlength+TREE_ELEMENT_EXTRA_SIZE;
    }
  }

  if (num_keys==0 ||
      num_keys * MARIA_MIN_SIZE_BULK_INSERT_TREE > cache_size)
    DBUG_RETURN(0);

  if (rows && rows*total_keylength < cache_size)
    cache_size= (ulong)rows;
  else
    cache_size/=total_keylength*16;

  info->bulk_insert=(TREE *)
    my_malloc((sizeof(TREE)*share->base.keys+
               sizeof(bulk_insert_param)*num_keys),MYF(0));

  if (!info->bulk_insert)
    DBUG_RETURN(HA_ERR_OUT_OF_MEM);

  params=(bulk_insert_param *)(info->bulk_insert+share->base.keys);
  for (i=0 ; i < share->base.keys ; i++)
  {
    if (maria_is_key_active(key_map, i))
    {
      params->info=info;
      params->keynr=i;
      /* Only allocate a 16'th of the buffer at a time */
      init_tree(&info->bulk_insert[i],
                cache_size * key[i].maxlength,
                cache_size * key[i].maxlength, 0,
		(qsort_cmp2)keys_compare, 0,
		(tree_element_free) keys_free, (void *)params++);
    }
    else
     info->bulk_insert[i].root=0;
  }

  DBUG_RETURN(0);
}

void maria_flush_bulk_insert(MARIA_HA *info, uint inx)
{
  if (info->bulk_insert)
  {
    if (is_tree_inited(&info->bulk_insert[inx]))
      reset_tree(&info->bulk_insert[inx]);
  }
}

void maria_end_bulk_insert(MARIA_HA *info)
{
  DBUG_ENTER("maria_end_bulk_insert");
  if (info->bulk_insert)
  {
    uint i;
    for (i=0 ; i < info->s->base.keys ; i++)
    {
      if (is_tree_inited(& info->bulk_insert[i]))
        delete_tree(&info->bulk_insert[i]);
    }
    my_free(info->bulk_insert, MYF(0));
    info->bulk_insert=0;
  }
  DBUG_VOID_RETURN;
}


/****************************************************************************
  Dedicated functions that generate log entries
****************************************************************************/

/**
  @brief Log creation of new page

  @note
    We don't have to store the page_length into the log entry as we can
    calculate this from the length of the log entry

  @retval 1   error
  @retval 0    ok
*/

static my_bool _ma_log_new(MARIA_HA *info, my_off_t page, uchar *buff,
                           uint page_length, uint key_nr, my_bool root_page)
{
  LSN lsn;
  uchar log_data[FILEID_STORE_SIZE + PAGE_STORE_SIZE * 2 + KEY_NR_STORE_SIZE
                 +1];
  LEX_STRING log_array[TRANSLOG_INTERNAL_PARTS + 2];
  MARIA_SHARE *share= info->s;
  DBUG_ENTER("_ma_log_new");
  DBUG_PRINT("enter", ("page: %lu", (ulong) page));

  DBUG_ASSERT(share->now_transactional);

  /* Store address of new root page */
  page/= share->block_size;
  page_store(log_data + FILEID_STORE_SIZE, page);

  /* Store link to next unused page */
  if (info->used_key_del == 2)
    page= 0;                                    /* key_del not changed */
  else
    page= ((share->current_key_del == HA_OFFSET_ERROR) ? IMPOSSIBLE_PAGE_NO :
           share->current_key_del / share->block_size);

  page_store(log_data + FILEID_STORE_SIZE + PAGE_STORE_SIZE, page);
  key_nr_store(log_data + FILEID_STORE_SIZE + PAGE_STORE_SIZE*2, key_nr);
  log_data[FILEID_STORE_SIZE + PAGE_STORE_SIZE*2 + KEY_NR_STORE_SIZE]=
    (uchar) root_page;

  log_array[TRANSLOG_INTERNAL_PARTS + 0].str=    (char*) log_data;
  log_array[TRANSLOG_INTERNAL_PARTS + 0].length= sizeof(log_data);

  page_length-= LSN_STORE_SIZE;
  log_array[TRANSLOG_INTERNAL_PARTS + 1].str=    buff + LSN_STORE_SIZE;
  log_array[TRANSLOG_INTERNAL_PARTS + 1].length= page_length;

  if (translog_write_record(&lsn, LOGREC_REDO_INDEX_NEW_PAGE,
                            info->trn, info,
                            (translog_size_t) (sizeof(log_data) + page_length),
                            TRANSLOG_INTERNAL_PARTS + 2, log_array,
                            log_data, NULL))
    DBUG_RETURN(1);
  DBUG_RETURN(0);
}


/**
   @brief
   Log when some part of the key page changes
*/

static my_bool _ma_log_change(MARIA_HA *info, my_off_t page, uchar *buff,
                              uchar *key_pos, uint length)
{
  LSN lsn;
  uchar log_data[FILEID_STORE_SIZE + PAGE_STORE_SIZE + 6], *log_pos;
  LEX_STRING log_array[TRANSLOG_INTERNAL_PARTS + 2];
  uint offset= (uint) (key_pos - buff);
  DBUG_ENTER("_ma_log_change");
  DBUG_PRINT("enter", ("page: %lu", (ulong) page));

  DBUG_ASSERT(info->s->now_transactional);

  /* Store address of new root page */
  page/= info->s->block_size;
  page_store(log_data + FILEID_STORE_SIZE, page);
  log_pos= log_data+ FILEID_STORE_SIZE + PAGE_STORE_SIZE;
  log_pos[0]= KEY_OP_OFFSET;
  int2store(log_pos+1, offset);
  log_pos[3]= KEY_OP_CHANGE;
  int2store(log_pos+4, length);

  log_array[TRANSLOG_INTERNAL_PARTS + 0].str=    (char*) log_data;
  log_array[TRANSLOG_INTERNAL_PARTS + 0].length= sizeof(log_data);
  log_array[TRANSLOG_INTERNAL_PARTS + 1].str=    buff + offset;
  log_array[TRANSLOG_INTERNAL_PARTS + 1].length= length;

  if (translog_write_record(&lsn, LOGREC_REDO_INDEX,
                            info->trn, info,
                            (translog_size_t) (sizeof(log_data) + length),
                            TRANSLOG_INTERNAL_PARTS + 2, log_array,
                            log_data, NULL))
    DBUG_RETURN(1);
  DBUG_RETURN(0);
}


/**
   @brief Write log entry for page splitting

   @note
     Write log entry for page that has got a key added to the page under
     one and only one of the following senarios:
     - Page is shortened from end
     - Data is added to end of page
     - Data added at front of page

   @param prefix_or_suffix  KEY_OP_NONE		Ignored
   			    KEY_OP_ADD_PREFIX   Add data to start of page
			    KEY_OP_ADD_SUFFIX   Add data to end of page

*/

static my_bool _ma_log_split(MARIA_HA *info, my_off_t page, uchar *buff,
                             uint org_length, uint new_length,
                             uchar *key_pos, uint key_length, int move_length,
                             enum en_key_op prefix_or_suffix,
                             uchar *data, uint data_length,
                             uint changed_length)
{
  LSN lsn;
  uchar log_data[FILEID_STORE_SIZE + PAGE_STORE_SIZE + 3+3+3+3+3+2];
  uchar *log_pos;
  LEX_STRING log_array[TRANSLOG_INTERNAL_PARTS + 3];
  uint offset= (uint) (key_pos - buff);
  uint translog_parts, extra_length;
  DBUG_ENTER("_ma_log_split");
  DBUG_PRINT("enter", ("page: %lu  org_length: %u  new_length: %u",
                       (ulong) page, org_length, new_length));

  log_pos= log_data + FILEID_STORE_SIZE;
  page/= info->s->block_size;
  page_store(log_pos, page);
  log_pos+= PAGE_STORE_SIZE;

  if (new_length <= offset || !key_pos)
  {
    /*
      Page was split before inserted key. Write redo entry where
      we just cut current page at page_length
    */
    uint length_offset= org_length - new_length;
    log_pos[0]= KEY_OP_DEL_SUFFIX;
    int2store(log_pos+1, length_offset);
    log_pos+= 3;
    translog_parts= 1;
    extra_length= 0;
  }
  else
  {
    /* Key was added to page which was split after the inserted key */
    uint max_key_length;

    /*
      Handle case when split happened directly after the newly inserted key.
    */
    max_key_length= new_length - offset;
    extra_length= min(key_length, max_key_length);

    if ((int) new_length < (int) (org_length + move_length + data_length))
    {
      /* Shorten page */
      uint diff= org_length + move_length + data_length - new_length;
      log_pos[0]= KEY_OP_DEL_SUFFIX;
      int2store(log_pos + 1, diff);
      log_pos+= 3;
    }
    else
    {
      DBUG_ASSERT(new_length == org_length + move_length + data_length);
    }

    log_pos[0]= KEY_OP_OFFSET;
    int2store(log_pos+1, offset);
    log_pos+= 3;

    if (move_length)
    {
      log_pos[0]= KEY_OP_SHIFT;
      int2store(log_pos+1, move_length);
      log_pos+= 3;
    }

    log_pos[0]= KEY_OP_CHANGE;
    int2store(log_pos+1, extra_length);
    log_pos+= 3;

    /* Point to original inserted key data */
    if (prefix_or_suffix == KEY_OP_ADD_PREFIX)
      key_pos+= data_length;

    translog_parts= 2;
    log_array[TRANSLOG_INTERNAL_PARTS + 1].str=    (char*) key_pos;
    log_array[TRANSLOG_INTERNAL_PARTS + 1].length= extra_length;
  }

  if (data_length)
  {
    /* Add prefix or suffix */
    log_pos[0]= prefix_or_suffix;
    int2store(log_pos+1, data_length);
    log_pos+= 3;
    if (prefix_or_suffix == KEY_OP_ADD_PREFIX)
    {
      int2store(log_pos+1, changed_length);
      log_pos+= 2;
      data_length= changed_length;
    }
    log_array[TRANSLOG_INTERNAL_PARTS + translog_parts].str=    (char*) data;
    log_array[TRANSLOG_INTERNAL_PARTS + translog_parts].length= data_length;
    translog_parts++;
    extra_length+= data_length;
  }

  log_array[TRANSLOG_INTERNAL_PARTS + 0].str=    log_data;
  log_array[TRANSLOG_INTERNAL_PARTS + 0].length= (uint) (log_pos -
                                                         log_data);
  DBUG_RETURN(translog_write_record(&lsn, LOGREC_REDO_INDEX,
                                    info->trn, info,
                                    (translog_size_t)
                                    log_array[TRANSLOG_INTERNAL_PARTS +
                                              0].length + extra_length,
                                    TRANSLOG_INTERNAL_PARTS + translog_parts,
                                    log_array, log_data, NULL));
}


/**
   @brief
   Write log entry for page that has got a key added to the page
   and page is shortened from start of page

   @fn _ma_log_del_prefix()
   @param info		Maria handler
   @param page		Page number
   @param buff		Page buffer
   @param org_length	Length of buffer when read
   @param new_length	Final length
   @param key_pos	Where on page buffer key was added. This is position
			before prefix was removed
   @param key_length    How many bytes was changed at 'key_pos'
   @param move_length   How many bytes was moved up when key was added

   @return
   @retval  0  ok
   @retval  1  error
*/

static my_bool _ma_log_del_prefix(MARIA_HA *info, my_off_t page, uchar *buff,
                                  uint org_length, uint new_length,
                                  uchar *key_pos, uint key_length,
                                  int move_length)
{
  LSN lsn;
  uchar log_data[FILEID_STORE_SIZE + PAGE_STORE_SIZE + 12], *log_pos;
  LEX_STRING log_array[TRANSLOG_INTERNAL_PARTS + 2];
  uint offset= (uint) (key_pos - buff);
  uint diff_length= org_length + move_length - new_length;
  uint translog_parts, extra_length;
  DBUG_ENTER("_ma_log_del_prefix");
  DBUG_PRINT("enter", ("page: %lu  org_length: %u  new_length: %u",
                       (ulong) page, org_length, new_length));

  DBUG_ASSERT((int) diff_length > 0);

  log_pos= log_data + FILEID_STORE_SIZE;
  page/= info->s->block_size;
  page_store(log_pos, page);
  log_pos+= PAGE_STORE_SIZE;

  translog_parts= 1;
  extra_length= 0;

  if (offset < diff_length + info->s->keypage_header)
  {
    /*
      Key is not anymore on page. Move data down, but take into account that
      the original page had grown with 'move_length bytes'
    */
    DBUG_ASSERT(offset + key_length <= diff_length + info->s->keypage_header);

    log_pos[0]= KEY_OP_DEL_PREFIX;
    int2store(log_pos+1, diff_length - move_length);
    log_pos+= 3;
  }
  else
  {
    /*
      Correct position to key, as data before key has been delete and key
      has thus been moved down
    */
    offset-= diff_length;
    key_pos-= diff_length;

    /* Move data down */
    log_pos[0]= KEY_OP_DEL_PREFIX;
    int2store(log_pos+1, diff_length);
    log_pos+= 3;

    log_pos[0]= KEY_OP_OFFSET;
    int2store(log_pos+1, offset);
    log_pos+= 3;

    if (move_length)
    {
      log_pos[0]= KEY_OP_SHIFT;
      int2store(log_pos+1, move_length);
      log_pos+= 3;
    }
    log_pos[0]= KEY_OP_CHANGE;
    int2store(log_pos+1, key_length);
    log_pos+= 3;
    log_array[TRANSLOG_INTERNAL_PARTS + 1].str=    (char*) key_pos;
    log_array[TRANSLOG_INTERNAL_PARTS + 1].length= key_length;
    translog_parts= 2;
    extra_length= key_length;
  }
  log_array[TRANSLOG_INTERNAL_PARTS + 0].str=    log_data;
  log_array[TRANSLOG_INTERNAL_PARTS + 0].length= (uint) (log_pos -
                                                         log_data);
  DBUG_RETURN(translog_write_record(&lsn, LOGREC_REDO_INDEX,
                                    info->trn, info,
                                    (translog_size_t)
                                    log_array[TRANSLOG_INTERNAL_PARTS +
                                              0].length + extra_length,
                                    TRANSLOG_INTERNAL_PARTS + translog_parts,
                                    log_array, log_data, NULL));
}


/**
   @brief
   Write log entry for page that has got data added first and
   data deleted last. Old changed key may be part of page
*/

static my_bool _ma_log_key_middle(MARIA_HA *info, my_off_t page, uchar *buff,
                                  uint new_length,
                                  uint data_added_first,
                                  uint data_changed_first,
                                  uint data_deleted_last,
                                  uchar *key_pos,
                                  uint key_length, int move_length)
{
  LSN lsn;
  uchar log_data[FILEID_STORE_SIZE + PAGE_STORE_SIZE + 3+5+3+3+3];
  uchar *log_pos;
  LEX_STRING log_array[TRANSLOG_INTERNAL_PARTS + 4];
  uint key_offset;
  uint translog_parts, extra_length;
  DBUG_ENTER("_ma_log_key_middle");
  DBUG_PRINT("enter", ("page: %lu", (ulong) page));

  /* new place of key after changes */
  key_pos+= data_added_first;
  key_offset= (uint) (key_pos - buff);
  if (key_offset < new_length)
  {
    /* key is on page; Calculate how much of the key is there */
    uint max_key_length= new_length - key_offset;
    if (max_key_length < key_length)
    {
      /* Key is last on page */
      key_length= max_key_length;
      move_length= 0;
    }
    /*
      Take into account that new data was added as part of original key
      that also needs to be removed from page
    */
    data_deleted_last+= move_length;
  }

  page/= info->s->block_size;

  /* First log changes to page */
  log_pos= log_data + FILEID_STORE_SIZE;
  page_store(log_pos, page);
  log_pos+= PAGE_STORE_SIZE;

  log_pos[0]= KEY_OP_DEL_SUFFIX;
  int2store(log_pos+1, data_deleted_last);
  log_pos+= 3;

  log_pos[0]= KEY_OP_ADD_PREFIX;
  int2store(log_pos+1, data_added_first);
  int2store(log_pos+3, data_changed_first);
  log_pos+= 5;

  log_array[TRANSLOG_INTERNAL_PARTS + 0].str=    log_data;
  log_array[TRANSLOG_INTERNAL_PARTS + 0].length= (uint) (log_pos -
                                                         log_data);
  log_array[TRANSLOG_INTERNAL_PARTS + 1].str=    ((char*) buff +
                                                  info->s->keypage_header);
  log_array[TRANSLOG_INTERNAL_PARTS + 1].length= data_changed_first;
  translog_parts= 2;
  extra_length= data_changed_first;

  /* If changed key is on page, log those changes too */

  if (key_offset < new_length)
  {
    uchar *start_log_pos= log_pos;

    log_pos[0]= KEY_OP_OFFSET;
    int2store(log_pos+1, key_offset);
    log_pos+= 3;
    if (move_length)
    {
      log_pos[0]= KEY_OP_SHIFT;
      int2store(log_pos+1, move_length);
      log_pos+= 3;
    }
    log_pos[0]= KEY_OP_CHANGE;
    int2store(log_pos+1, key_length);
    log_pos+= 3;

    log_array[TRANSLOG_INTERNAL_PARTS + 2].str=    (char*) start_log_pos;
    log_array[TRANSLOG_INTERNAL_PARTS + 2].length= (uint) (log_pos -
                                                           start_log_pos);

    log_array[TRANSLOG_INTERNAL_PARTS + 3].str=    (char*) key_pos;
    log_array[TRANSLOG_INTERNAL_PARTS + 3].length= key_length;
    translog_parts+=2;
    extra_length+= (uint) (log_array[TRANSLOG_INTERNAL_PARTS + 2].length +
                           key_length);
  }

  DBUG_RETURN(translog_write_record(&lsn, LOGREC_REDO_INDEX,
                                    info->trn, info,
                                    (translog_size_t)
                                    (log_array[TRANSLOG_INTERNAL_PARTS +
                                               0].length + extra_length),
                                    TRANSLOG_INTERNAL_PARTS + translog_parts,
                                    log_array, log_data, NULL));
}


#ifdef NOT_NEEDED

/**
   @brief
   Write log entry for page that has got data added first and
   data deleted last
*/

static my_bool _ma_log_middle(MARIA_HA *info, my_off_t page,
                              uchar *buff,
                              uint data_added_first, uint data_changed_first,
                              uint data_deleted_last)
{
  LSN lsn;
  LEX_STRING log_array[TRANSLOG_INTERNAL_PARTS + 2];
  uchar log_data[FILEID_STORE_SIZE + PAGE_STORE_SIZE + 3 + 5], *log_pos;
  DBUG_ENTER("_ma_log_middle");
  DBUG_PRINT("enter", ("page: %lu", (ulong) page));

  page/= info->s->block_size;

  log_pos= log_data + FILEID_STORE_SIZE;
  page_store(log_pos, page);
  log_pos+= PAGE_STORE_SIZE;

  log_pos[0]= KEY_OP_DEL_PREFIX;
  int2store(log_pos+1, data_deleted_last);
  log_pos+= 3;

  log_pos[0]= KEY_OP_ADD_PREFIX;
  int2store(log_pos+1, data_added_first);
  int2store(log_pos+3, data_changed_first);
  log_pos+= 5;

  log_array[TRANSLOG_INTERNAL_PARTS + 0].str=    log_data;
  log_array[TRANSLOG_INTERNAL_PARTS + 0].length= (uint) (log_pos -
                                                         log_data);

  log_array[TRANSLOG_INTERNAL_PARTS + 1].str=    ((char*) buff +
                                                  info->s->keypage_header);
  log_array[TRANSLOG_INTERNAL_PARTS + 1].length= data_changed_first;
  DBUG_RETURN(translog_write_record(&lsn, LOGREC_REDO_INDEX,
                                    info->trn, info,
                                    (translog_size_t)
                                    log_array[TRANSLOG_INTERNAL_PARTS +
                                              0].length + data_changed_first,
                                    TRANSLOG_INTERNAL_PARTS + 2,
                                    log_array, log_data, NULL));
}
#endif
