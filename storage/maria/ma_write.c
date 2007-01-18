/* Copyright (C) 2006 MySQL AB & MySQL Finland AB & TCX DataKonsult AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

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

#define MAX_POINTER_LENGTH 8

	/* Functions declared in this file */

static int w_search(MARIA_HA *info,MARIA_KEYDEF *keyinfo,
		    uint comp_flag, byte *key,
		    uint key_length, my_off_t pos, byte *father_buff,
		    byte *father_keypos, my_off_t father_page,
		    my_bool insert_last);
static int _ma_balance_page(MARIA_HA *info,MARIA_KEYDEF *keyinfo,byte *key,
			    byte *curr_buff,byte *father_buff,
			    byte *father_keypos,my_off_t father_page);
static byte *_ma_find_last_pos(MARIA_KEYDEF *keyinfo, byte *page,
				byte *key, uint *return_key_length,
				byte **after_key);
int _ma_ck_write_tree(register MARIA_HA *info, uint keynr,byte *key,
		      uint key_length);
int _ma_ck_write_btree(register MARIA_HA *info, uint keynr,byte *key,
		       uint key_length);


MARIA_RECORD_POS _ma_write_init_default(MARIA_HA *info,
                                        const byte *record
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

int maria_write(MARIA_HA *info, byte *record)
{
  MARIA_SHARE *share=info->s;
  uint i;
  int save_errno;
  MARIA_RECORD_POS filepos;
  byte *buff;
  my_bool lock_tree= share->concurrent_insert;
  my_bool fatal_error;
  DBUG_ENTER("maria_write");
  DBUG_PRINT("enter",("index_file: %d  data_file: %d",
                      info->s->kfile,info->dfile));

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
        if (_ma_ft_add(info,i,(char*) buff,record,filepos))
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
    info->state->checksum+= info->cur_row.checksum;
  }
  if (share->base.auto_key)
    set_if_bigger(info->s->state.auto_increment,
                  ma_retrieve_auto_increment(info, record));
  info->update= (HA_STATE_CHANGED | HA_STATE_AKTIV | HA_STATE_WRITTEN |
		 HA_STATE_ROW_CHANGED);
  info->state->records++;
  info->cur_row.lastpos= filepos;
  VOID(_ma_writeinfo(info, WRITEINFO_UPDATE_KEYFILE));
  if (info->invalidator != 0)
  {
    DBUG_PRINT("info", ("invalidator... '%s' (update)", info->filename));
    (*info->invalidator)(info->filename);
    info->invalidator=0;
  }
  allow_break();				/* Allow SIGHUP & SIGINT */
  DBUG_RETURN(0);

err:
  save_errno= my_errno;
  fatal_error= 0;
  if (my_errno == HA_ERR_FOUND_DUPP_KEY ||
      my_errno == HA_ERR_RECORD_FILE_FULL ||
      my_errno == HA_ERR_NULL_IN_SPATIAL)
  {
    if (info->bulk_insert)
    {
      uint j;
      for (j=0 ; j < share->base.keys ; j++)
        maria_flush_bulk_insert(info, j);
    }
    info->errkey= (int) i;
    while ( i-- > 0)
    {
      if (maria_is_key_active(share->state.key_map, i))
      {
	bool local_lock_tree= (lock_tree &&
			       !(info->bulk_insert &&
				 is_tree_inited(&info->bulk_insert[i])));
	if (local_lock_tree)
	  rw_wrlock(&share->key_root_lock[i]);
	if (share->keyinfo[i].flag & HA_FULLTEXT)
        {
          if (_ma_ft_del(info,i,(char*) buff,record,filepos))
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
  DBUG_PRINT("error", ("got error: %d", save_errno));
  VOID(_ma_writeinfo(info,WRITEINFO_UPDATE_KEYFILE));
  allow_break();			/* Allow SIGHUP & SIGINT */
  DBUG_RETURN(my_errno=save_errno);
} /* maria_write */


	/* Write one key to btree */

int _ma_ck_write(MARIA_HA *info, uint keynr, byte *key, uint key_length)
{
  DBUG_ENTER("_ma_ck_write");

  if (info->bulk_insert && is_tree_inited(&info->bulk_insert[keynr]))
  {
    DBUG_RETURN(_ma_ck_write_tree(info, keynr, key, key_length));
  }
  else
  {
    DBUG_RETURN(_ma_ck_write_btree(info, keynr, key, key_length));
  }
} /* _ma_ck_write */


/**********************************************************************
 *                Normal insert code                                  *
 **********************************************************************/

int _ma_ck_write_btree(register MARIA_HA *info, uint keynr, byte *key,
		       uint key_length)
{
  int error;
  uint comp_flag;
  MARIA_KEYDEF *keyinfo=info->s->keyinfo+keynr;
  my_off_t  *root=&info->s->state.key_root[keynr];
  DBUG_ENTER("_ma_ck_write_btree");

  if (keyinfo->flag & HA_SORT_ALLOWS_SAME)
    comp_flag=SEARCH_BIGGER;			/* Put after same key */
  else if (keyinfo->flag & (HA_NOSAME|HA_FULLTEXT))
  {
    comp_flag=SEARCH_FIND | SEARCH_UPDATE;	/* No duplicates */
    if (keyinfo->flag & HA_NULL_ARE_EQUAL)
      comp_flag|= SEARCH_NULL_ARE_EQUAL;
  }
  else
    comp_flag=SEARCH_SAME;			/* Keys in rec-pos order */

  error= _ma_ck_real_write_btree(info, keyinfo, key, key_length,
                                root, comp_flag);
  if (info->ft1_to_ft2)
  {
    if (!error)
      error= _ma_ft_convert_to_ft2(info, keynr, key);
    delete_dynamic(info->ft1_to_ft2);
    my_free((gptr)info->ft1_to_ft2, MYF(0));
    info->ft1_to_ft2=0;
  }
  DBUG_RETURN(error);
} /* _ma_ck_write_btree */


int _ma_ck_real_write_btree(MARIA_HA *info, MARIA_KEYDEF *keyinfo,
                            byte *key, uint key_length, my_off_t *root,
                            uint comp_flag)
{
  int error;
  DBUG_ENTER("_ma_ck_real_write_btree");
  /* key_length parameter is used only if comp_flag is SEARCH_FIND */
  if (*root == HA_OFFSET_ERROR ||
      (error=w_search(info, keyinfo, comp_flag, key, key_length,
		      *root, (byte*) 0, (byte*) 0,
		      (my_off_t) 0, 1)) > 0)
    error= _ma_enlarge_root(info,keyinfo,key,root);
  DBUG_RETURN(error);
} /* _ma_ck_real_write_btree */


	/* Make a new root with key as only pointer */

int _ma_enlarge_root(MARIA_HA *info, MARIA_KEYDEF *keyinfo, byte *key,
                     my_off_t *root)
{
  uint t_length,nod_flag;
  MARIA_KEY_PARAM s_temp;
  MARIA_SHARE *share=info->s;
  DBUG_ENTER("_ma_enlarge_root");

  nod_flag= (*root != HA_OFFSET_ERROR) ?  share->base.key_reflength : 0;
  _ma_kpointer(info,info->buff+2,*root); /* if nod */
  t_length=(*keyinfo->pack_key)(keyinfo,nod_flag,(byte*) 0,
				(byte*) 0, (byte*) 0, key,&s_temp);
  maria_putint(info->buff,t_length+2+nod_flag,nod_flag);
  (*keyinfo->store_key)(keyinfo,info->buff+2+nod_flag,&s_temp);
  info->keybuff_used=info->page_changed=1;		/* info->buff is used */
  if ((*root= _ma_new(info,keyinfo,DFLT_INIT_HITS)) == HA_OFFSET_ERROR ||
      _ma_write_keypage(info,keyinfo,*root,DFLT_INIT_HITS,info->buff))
    DBUG_RETURN(-1);
  DBUG_RETURN(0);
} /* _ma_enlarge_root */


	/*
	  Search after a position for a key and store it there
	  Returns -1 = error
		   0  = ok
		   1  = key should be stored in higher tree
	*/

static int w_search(register MARIA_HA *info, register MARIA_KEYDEF *keyinfo,
		    uint comp_flag, byte *key, uint key_length, my_off_t page,
		    byte *father_buff, byte *father_keypos,
		    my_off_t father_page, my_bool insert_last)
{
  int error,flag;
  uint nod_flag, search_key_length;
  byte *temp_buff,*keypos;
  byte keybuff[HA_MAX_KEY_BUFF];
  my_bool was_last_key;
  my_off_t next_page, dup_key_pos;
  DBUG_ENTER("w_search");
  DBUG_PRINT("enter",("page: %ld",page));

  search_key_length= (comp_flag & SEARCH_FIND) ? key_length : USE_WHOLE_KEY;
  if (!(temp_buff= (byte*) my_alloca((uint) keyinfo->block_length+
				      HA_MAX_KEY_BUFF*2)))
    DBUG_RETURN(-1);
  if (!_ma_fetch_keypage(info,keyinfo,page,DFLT_INIT_HITS,temp_buff,0))
    goto err;

  flag=(*keyinfo->bin_search)(info,keyinfo,temp_buff,key,search_key_length,
			      comp_flag, &keypos, keybuff, &was_last_key);
  nod_flag= _ma_test_if_nod(temp_buff);
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
        keyinfo=&info->s->ft2_keyinfo;
        get_key_full_length_rdonly(off, key);
        key+=off;
        keypos-=keyinfo->keylength+nod_flag; /* we'll modify key entry 'in vivo' */
        error= _ma_ck_real_write_btree(info, keyinfo, key, 0,
                                      &root, comp_flag);
        _ma_dpointer(info, keypos+HA_FT_WLEN, root);
        subkeys--; /* should there be underflow protection ? */
        DBUG_ASSERT(subkeys < 0);
        ft_intXstore(keypos, subkeys);
        if (!error)
          error= _ma_write_keypage(info,keyinfo,page,DFLT_INIT_HITS,temp_buff);
        my_afree((byte*) temp_buff);
        DBUG_RETURN(error);
      }
    }
    else /* not HA_FULLTEXT, normal HA_NOSAME key */
    {
      info->dup_key_pos= dup_key_pos;
      my_afree((byte*) temp_buff);
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
      (error=w_search(info, keyinfo, comp_flag, key, key_length, next_page,
		      temp_buff, keypos, page, insert_last)) >0)
  {
    error= _ma_insert(info,keyinfo,key,temp_buff,keypos,keybuff,father_buff,
		     father_keypos,father_page, insert_last);
    if (_ma_write_keypage(info,keyinfo,page,DFLT_INIT_HITS,temp_buff))
      goto err;
  }
  my_afree((byte*) temp_buff);
  DBUG_RETURN(error);
err:
  my_afree((byte*) temp_buff);
  DBUG_PRINT("exit",("Error: %d",my_errno));
  DBUG_RETURN (-1);
} /* w_search */


/*
  Insert new key.

  SYNOPSIS
    _ma_insert()
    info                        Open table information.
    keyinfo                     Key definition information.
    key                         New key.
    anc_buff                    Key page (beginning).
    key_pos                     Position in key page where to insert.
    key_buff                    Copy of previous key.
    father_buff                 parent key page for balancing.
    father_key_pos              position in parent key page for balancing.
    father_page                 position of parent key page in file.
    insert_last                 If to append at end of page.

  DESCRIPTION
    Insert new key at right of key_pos.

  RETURN
    2           if key contains key to upper level.
    0           OK.
    < 0         Error.
*/

int _ma_insert(register MARIA_HA *info, register MARIA_KEYDEF *keyinfo,
	       byte *key, byte *anc_buff, byte *key_pos, byte *key_buff,
               byte *father_buff, byte *father_key_pos, my_off_t father_page,
	       my_bool insert_last)
{
  uint a_length,nod_flag;
  int t_length;
  byte *endpos, *prev_key;
  MARIA_KEY_PARAM s_temp;
  DBUG_ENTER("_ma_insert");
  DBUG_PRINT("enter",("key_pos: 0x%lx", (ulong) key_pos));
  DBUG_EXECUTE("key", _ma_print_key(DBUG_FILE,keyinfo->seg,key,
                                    USE_WHOLE_KEY););

  nod_flag=_ma_test_if_nod(anc_buff);
  a_length=maria_getint(anc_buff);
  endpos= anc_buff+ a_length;
  prev_key=(key_pos == anc_buff+2+nod_flag ? (byte*) 0 : key_buff);
  t_length=(*keyinfo->pack_key)(keyinfo,nod_flag,
				(key_pos == endpos ? (byte*) 0 : key_pos),
				prev_key, prev_key,
				key,&s_temp);
#ifndef DBUG_OFF
  if (key_pos != anc_buff+2+nod_flag && (keyinfo->flag &
					 (HA_BINARY_PACK_KEY | HA_PACK_KEY)))
  {
    DBUG_DUMP("prev_key",(byte*) key_buff, _ma_keylength(keyinfo,key_buff));
  }
  if (keyinfo->flag & HA_PACK_KEY)
  {
    DBUG_PRINT("test",("t_length: %d  ref_len: %d",
		       t_length,s_temp.ref_length));
    DBUG_PRINT("test",("n_ref_len: %d  n_length: %d  key_pos: 0x%lx",
		       s_temp.n_ref_length,s_temp.n_length,s_temp.key));
  }
#endif
  if (t_length > 0)
  {
    if (t_length >= keyinfo->maxlength*2+MAX_POINTER_LENGTH)
    {
      maria_print_error(info->s, HA_ERR_CRASHED);
      my_errno=HA_ERR_CRASHED;
      DBUG_RETURN(-1);
    }
    bmove_upp((byte*) endpos+t_length,(byte*) endpos,(uint) (endpos-key_pos));
  }
  else
  {
    if (-t_length >= keyinfo->maxlength*2+MAX_POINTER_LENGTH)
    {
      maria_print_error(info->s, HA_ERR_CRASHED);
      my_errno=HA_ERR_CRASHED;
      DBUG_RETURN(-1);
    }
    bmove(key_pos,key_pos-t_length,(uint) (endpos-key_pos)+t_length);
  }
  (*keyinfo->store_key)(keyinfo,key_pos,&s_temp);
  a_length+=t_length;
  maria_putint(anc_buff,a_length,nod_flag);
  if (a_length <= keyinfo->block_length)
  {
    if (keyinfo->block_length - a_length < 32 &&
        keyinfo->flag & HA_FULLTEXT && key_pos == endpos &&
        info->s->base.key_reflength <= info->s->base.rec_reflength &&
        info->s->options & (HA_OPTION_PACK_RECORD | HA_OPTION_COMPRESS_RECORD))
    {
      /*
        Normal word. One-level tree. Page is almost full.
        Let's consider converting.
        We'll compare 'key' and the first key at anc_buff
       */
      byte *a=key, *b=anc_buff+2+nod_flag;
      uint alen, blen, ft2len=info->s->ft2_keyinfo.keylength;
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
        /* yup. converting */
        info->ft1_to_ft2=(DYNAMIC_ARRAY *)
          my_malloc(sizeof(DYNAMIC_ARRAY), MYF(MY_WME));
        my_init_dynamic_array(info->ft1_to_ft2, ft2len, 300, 50);

        /*
          now, adding all keys from the page to dynarray
          if the page is a leaf (if not keys will be deleted later)
        */
        if (!nod_flag)
        {
          /* let's leave the first key on the page, though, because
             we cannot easily dispatch an empty page here */
          b+=blen+ft2len+2;
          for (a=anc_buff+a_length ; b < a ; b+=ft2len+2)
            insert_dynamic(info->ft1_to_ft2, (char*) b);

          /* fixing the page's length - it contains only one key now */
          maria_putint(anc_buff,2+blen+ft2len+2,0);
        }
        /* the rest will be done when we're back from recursion */
      }
    }
    DBUG_RETURN(0);				/* There is room on page */
  }
  /* Page is full */
  if (nod_flag)
    insert_last=0;
  if (!(keyinfo->flag & (HA_VAR_LENGTH_KEY | HA_BINARY_PACK_KEY)) &&
      father_buff && !insert_last)
    DBUG_RETURN(_ma_balance_page(info,keyinfo,key,anc_buff,father_buff,
				 father_key_pos,father_page));
  DBUG_RETURN(_ma_split_page(info,keyinfo,key,anc_buff,key_buff, insert_last));
} /* _ma_insert */


	/* split a full page in two and assign emerging item to key */

int _ma_split_page(register MARIA_HA *info, register MARIA_KEYDEF *keyinfo,
		   byte *key, byte *buff, byte *key_buff,
		   my_bool insert_last_key)
{
  uint length,a_length,key_ref_length,t_length,nod_flag,key_length;
  byte *key_pos,*pos, *after_key;
  my_off_t new_pos;
  MARIA_KEY_PARAM s_temp;
  DBUG_ENTER("maria_split_page");
  DBUG_DUMP("buff",(byte*) buff,maria_getint(buff));

  if (info->s->keyinfo+info->lastinx == keyinfo)
    info->page_changed=1;			/* Info->buff is used */
  info->keybuff_used=1;
  nod_flag=_ma_test_if_nod(buff);
  key_ref_length=2+nod_flag;
  if (insert_last_key)
    key_pos= _ma_find_last_pos(keyinfo,buff,key_buff, &key_length, &after_key);
  else
    key_pos= _ma_find_half_pos(nod_flag,keyinfo,buff,key_buff, &key_length,
			      &after_key);
  if (!key_pos)
    DBUG_RETURN(-1);

  length=(uint) (key_pos-buff);
  a_length=maria_getint(buff);
  maria_putint(buff,length,nod_flag);

  key_pos=after_key;
  if (nod_flag)
  {
    DBUG_PRINT("test",("Splitting nod"));
    pos=key_pos-nod_flag;
    memcpy((byte*) info->buff+2,(byte*) pos,(size_t) nod_flag);
  }

	/* Move middle item to key and pointer to new page */
  if ((new_pos= _ma_new(info,keyinfo,DFLT_INIT_HITS)) == HA_OFFSET_ERROR)
    DBUG_RETURN(-1);
  _ma_kpointer(info, _ma_move_key(keyinfo,key,key_buff),new_pos);

	/* Store new page */
  if (!(*keyinfo->get_key)(keyinfo,nod_flag,&key_pos,key_buff))
    DBUG_RETURN(-1);

  t_length=(*keyinfo->pack_key)(keyinfo,nod_flag,(byte *) 0,
				(byte*) 0, (byte*) 0,
				key_buff, &s_temp);
  length=(uint) ((buff+a_length)-key_pos);
  memcpy((byte*) info->buff+key_ref_length+t_length,(byte*) key_pos,
	 (size_t) length);
  (*keyinfo->store_key)(keyinfo,info->buff+key_ref_length,&s_temp);
  maria_putint(info->buff,length+t_length+key_ref_length,nod_flag);

  if (_ma_write_keypage(info,keyinfo,new_pos,DFLT_INIT_HITS,info->buff))
    DBUG_RETURN(-1);
  DBUG_DUMP("key",(byte*) key, _ma_keylength(keyinfo,key));
  DBUG_RETURN(2);				/* Middle key up */
} /* _ma_split_page */


	/*
	  Calculate how to much to move to split a page in two
	  Returns pointer to start of key.
	  key will contain the key.
	  return_key_length will contain the length of key
	  after_key will contain the position to where the next key starts
	*/

byte *_ma_find_half_pos(uint nod_flag, MARIA_KEYDEF *keyinfo, byte *page,
			 byte *key, uint *return_key_length,
			 byte **after_key)
{
  uint keys,length,key_ref_length;
  byte *end,*lastpos;
  DBUG_ENTER("_ma_find_half_pos");

  key_ref_length=2+nod_flag;
  length=maria_getint(page)-key_ref_length;
  page+=key_ref_length;
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
                     lastpos, page, end));
  DBUG_RETURN(lastpos);
} /* _ma_find_half_pos */


/*
  Split buffer at last key
  Returns pointer to the start of the key before the last key
  key will contain the last key
*/

static byte *_ma_find_last_pos(MARIA_KEYDEF *keyinfo, byte *page,
				byte *key, uint *return_key_length,
				byte **after_key)
{
  uint keys,length,last_length,key_ref_length;
  byte *end,*lastpos,*prevpos;
  byte key_buff[HA_MAX_KEY_BUFF];
  DBUG_ENTER("_ma_find_last_pos");

  key_ref_length=2;
  length=maria_getint(page)-key_ref_length;
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
                     prevpos, page, end));
  DBUG_RETURN(prevpos);
} /* _ma_find_last_pos */


	/* Balance page with not packed keys with page on right/left */
	/* returns 0 if balance was done */

static int _ma_balance_page(register MARIA_HA *info, MARIA_KEYDEF *keyinfo,
			    byte *key, byte *curr_buff, byte *father_buff,
			    byte *father_key_pos, my_off_t father_page)
{
  my_bool right;
  uint k_length,father_length,father_keylength,nod_flag,curr_keylength,
       right_length,left_length,new_right_length,new_left_length,extra_length,
       length,keys;
  byte *pos,*buff,*extra_buff;
  my_off_t next_page,new_pos;
  byte tmp_part_key[HA_MAX_KEY_BUFF];
  DBUG_ENTER("_ma_balance_page");

  k_length=keyinfo->keylength;
  father_length=maria_getint(father_buff);
  father_keylength=k_length+info->s->base.key_reflength;
  nod_flag=_ma_test_if_nod(curr_buff);
  curr_keylength=k_length+nod_flag;
  info->page_changed=1;

  if ((father_key_pos != father_buff+father_length &&
       (info->state->records & 1)) ||
      father_key_pos == father_buff+2+info->s->base.key_reflength)
  {
    right=1;
    next_page= _ma_kpos(info->s->base.key_reflength,
			father_key_pos+father_keylength);
    buff=info->buff;
    DBUG_PRINT("test",("use right page: %lu",next_page));
  }
  else
  {
    right=0;
    father_key_pos-=father_keylength;
    next_page= _ma_kpos(info->s->base.key_reflength,father_key_pos);
					/* Fix that curr_buff is to left */
    buff=curr_buff; curr_buff=info->buff;
    DBUG_PRINT("test",("use left page: %lu",next_page));
  }					/* father_key_pos ptr to parting key */

  if (!_ma_fetch_keypage(info,keyinfo,next_page,DFLT_INIT_HITS,info->buff,0))
    goto err;
  DBUG_DUMP("next",(byte*) info->buff,maria_getint(info->buff));

	/* Test if there is room to share keys */

  left_length=maria_getint(curr_buff);
  right_length=maria_getint(buff);
  keys=(left_length+right_length-4-nod_flag*2)/curr_keylength;

  if ((right ? right_length : left_length) + curr_keylength <=
      keyinfo->block_length)
  {						/* Merge buffs */
    new_left_length=2+nod_flag+(keys/2)*curr_keylength;
    new_right_length=2+nod_flag+((keys+1)/2)*curr_keylength;
    maria_putint(curr_buff,new_left_length,nod_flag);
    maria_putint(buff,new_right_length,nod_flag);

    if (left_length < new_left_length)
    {						/* Move keys buff -> leaf */
      pos=curr_buff+left_length;
      memcpy((byte*) pos,(byte*) father_key_pos, (size_t) k_length);
      memcpy((byte*) pos+k_length, (byte*) buff+2,
	     (size_t) (length=new_left_length - left_length - k_length));
      pos=buff+2+length;
      memcpy((byte*) father_key_pos,(byte*) pos,(size_t) k_length);
      bmove((byte*) buff+2,(byte*) pos+k_length,new_right_length);
    }
    else
    {						/* Move keys -> buff */

      bmove_upp((byte*) buff+new_right_length,(byte*) buff+right_length,
		right_length-2);
      length=new_right_length-right_length-k_length;
      memcpy((byte*) buff+2+length,father_key_pos,(size_t) k_length);
      pos=curr_buff+new_left_length;
      memcpy((byte*) father_key_pos,(byte*) pos,(size_t) k_length);
      memcpy((byte*) buff+2,(byte*) pos+k_length,(size_t) length);
    }

    if (_ma_write_keypage(info,keyinfo,next_page,DFLT_INIT_HITS,info->buff) ||
	_ma_write_keypage(info,keyinfo,father_page,DFLT_INIT_HITS,father_buff))
      goto err;
    DBUG_RETURN(0);
  }

	/* curr_buff[] and buff[] are full, lets split and make new nod */

  extra_buff=info->buff+info->s->base.max_key_block_length;
  new_left_length=new_right_length=2+nod_flag+(keys+1)/3*curr_keylength;
  if (keys == 5)				/* Too few keys to balance */
    new_left_length-=curr_keylength;
  extra_length=nod_flag+left_length+right_length-
    new_left_length-new_right_length-curr_keylength;
  DBUG_PRINT("info",("left_length: %d  right_length: %d  new_left_length: %d  new_right_length: %d  extra_length: %d",
		     left_length, right_length,
		     new_left_length, new_right_length,
		     extra_length));
  maria_putint(curr_buff,new_left_length,nod_flag);
  maria_putint(buff,new_right_length,nod_flag);
  maria_putint(extra_buff,extra_length+2,nod_flag);

  /* move first largest keys to new page  */
  pos=buff+right_length-extra_length;
  memcpy((byte*) extra_buff+2,pos,(size_t) extra_length);
  /* Save new parting key */
  memcpy(tmp_part_key, pos-k_length,k_length);
  /* Make place for new keys */
  bmove_upp((byte*) buff+new_right_length,(byte*) pos-k_length,
	    right_length-extra_length-k_length-2);
  /* Copy keys from left page */
  pos= curr_buff+new_left_length;
  memcpy((byte*) buff+2,(byte*) pos+k_length,
	 (size_t) (length=left_length-new_left_length-k_length));
  /* Copy old parting key */
  memcpy((byte*) buff+2+length,father_key_pos,(size_t) k_length);

  /* Move new parting keys up to caller */
  memcpy((byte*) (right ? key : father_key_pos),pos,(size_t) k_length);
  memcpy((byte*) (right ? father_key_pos : key),tmp_part_key, k_length);

  if ((new_pos= _ma_new(info,keyinfo,DFLT_INIT_HITS)) == HA_OFFSET_ERROR)
    goto err;
  _ma_kpointer(info,key+k_length,new_pos);
  if (_ma_write_keypage(info,keyinfo,(right ? new_pos : next_page),
			DFLT_INIT_HITS,info->buff) ||
      _ma_write_keypage(info,keyinfo,(right ? next_page : new_pos),
                        DFLT_INIT_HITS,extra_buff))
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


int _ma_ck_write_tree(register MARIA_HA *info, uint keynr, byte *key,
		      uint key_length)
{
  int error;
  DBUG_ENTER("_ma_ck_write_tree");

  error= tree_insert(&info->bulk_insert[keynr], key,
         key_length + info->s->rec_reflength,
         info->bulk_insert[keynr].custom_arg) ? 0 : HA_ERR_OUT_OF_MEM ;

  DBUG_RETURN(error);
} /* _ma_ck_write_tree */


/* typeof(_ma_keys_compare)=qsort_cmp2 */

static int keys_compare(bulk_insert_param *param, byte *key1, byte *key2)
{
  uint not_used[2];
  return ha_key_cmp(param->info->s->keyinfo[param->keynr].seg,
                    (uchar*) key1, (uchar*) key2, USE_WHOLE_KEY, SEARCH_SAME,
                    not_used);
}


static int keys_free(byte *key, TREE_FREE mode, bulk_insert_param *param)
{
  /*
    Probably I can use info->lastkey here, but I'm not sure,
    and to be safe I'd better use local lastkey.
  */
  byte lastkey[HA_MAX_KEY_BUFF];
  uint keylen;
  MARIA_KEYDEF *keyinfo;

  switch (mode) {
  case free_init:
    if (param->info->s->concurrent_insert)
    {
      rw_wrlock(&param->info->s->key_root_lock[param->keynr]);
      param->info->s->keyinfo[param->keynr].version++;
    }
    return 0;
  case free_free:
    keyinfo=param->info->s->keyinfo+param->keynr;
    keylen= _ma_keylength(keyinfo, key);
    memcpy(lastkey, key, keylen);
    return _ma_ck_write_btree(param->info,param->keynr,lastkey,
			      keylen - param->info->s->rec_reflength);
  case free_end:
    if (param->info->s->concurrent_insert)
      rw_unlock(&param->info->s->key_root_lock[param->keynr]);
    return 0;
  }
  return -1;
}


int maria_init_bulk_insert(MARIA_HA *info, ulong cache_size, ha_rows rows)
{
  MARIA_SHARE *share=info->s;
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
    cache_size=rows;
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
  if (info->bulk_insert)
  {
    uint i;
    for (i=0 ; i < info->s->base.keys ; i++)
    {
      if (is_tree_inited(& info->bulk_insert[i]))
      {
        delete_tree(& info->bulk_insert[i]);
      }
    }
    my_free((void *)info->bulk_insert, MYF(0));
    info->bulk_insert=0;
  }
}
