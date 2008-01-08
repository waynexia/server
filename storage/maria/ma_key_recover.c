/* Copyright (C) 2007 Michael Widenius

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

/* Redo of index */

#include "maria_def.h"
#include "ma_blockrec.h"
#include "trnman.h"
#include "ma_key_recover.h"

/****************************************************************************
  Some helper functions used both by key page loggin and block page loggin
****************************************************************************/

/**
  @brief Unpin all pinned pages

  @fn _ma_unpin_all_pages()
  @param info	   Maria handler
  @param undo_lsn  LSN for undo pages. LSN_IMPOSSIBLE if we shouldn't write
                   undo (like on duplicate key errors)

  info->pinned_pages is the list of pages to unpin. Each member of the list
  must have its 'changed' saying if the page was changed or not.

  @note
    We unpin pages in the reverse order as they where pinned; This is not
    necessary now, but may simplify things in the future.

  @return
  @retval   0   ok
  @retval   1   error (fatal disk error)
*/

void _ma_unpin_all_pages(MARIA_HA *info, LSN undo_lsn)
{
  MARIA_PINNED_PAGE *page_link= ((MARIA_PINNED_PAGE*)
                                 dynamic_array_ptr(&info->pinned_pages, 0));
  MARIA_PINNED_PAGE *pinned_page= page_link + info->pinned_pages.elements;
  DBUG_ENTER("_ma_unpin_all_pages");
  DBUG_PRINT("info", ("undo_lsn: %lu", (ulong) undo_lsn));

  if (!info->s->now_transactional)
    DBUG_ASSERT(undo_lsn == LSN_IMPOSSIBLE || maria_in_recovery);

  while (pinned_page-- != page_link)
  {
    /*
      Note this assert fails if we got a disk error or the record file
      is corrupted, which means we should have this enabled only in debug
      builds.
    */
#ifdef EXTRA_DEBUG
    DBUG_ASSERT(!pinned_page->changed ||
                undo_lsn != LSN_IMPOSSIBLE || !info->s->now_transactional);
#endif
    pagecache_unlock_by_link(info->s->pagecache, pinned_page->link,
                             pinned_page->unlock, PAGECACHE_UNPIN,
                             info->trn->rec_lsn, undo_lsn,
                             pinned_page->changed);
  }

  info->pinned_pages.elements= 0;
  DBUG_VOID_RETURN;
}


my_bool _ma_write_clr(MARIA_HA *info, LSN undo_lsn,
                      enum translog_record_type undo_type,
                      my_bool store_checksum, ha_checksum checksum,
                      LSN *res_lsn, void *extra_msg)
{
  uchar log_data[LSN_STORE_SIZE + FILEID_STORE_SIZE + CLR_TYPE_STORE_SIZE +
                 HA_CHECKSUM_STORE_SIZE+ KEY_NR_STORE_SIZE + PAGE_STORE_SIZE];
  uchar *log_pos;
  LEX_STRING log_array[TRANSLOG_INTERNAL_PARTS + 1];
  struct st_msg_to_write_hook_for_clr_end msg;
  my_bool res;
  DBUG_ENTER("_ma_write_clr");

  /* undo_lsn must be first for compression to work */
  lsn_store(log_data, undo_lsn);
  clr_type_store(log_data + LSN_STORE_SIZE + FILEID_STORE_SIZE, undo_type);
  log_pos= log_data + LSN_STORE_SIZE + FILEID_STORE_SIZE + CLR_TYPE_STORE_SIZE;

  /* Extra_msg is handled in write_hook_for_clr_end() */
  msg.undone_record_type= undo_type;
  msg.previous_undo_lsn=  undo_lsn;
  msg.extra_msg= extra_msg;
  msg.checksum_delta= 0;

  if (store_checksum)
  {
    msg.checksum_delta= checksum;
    ha_checksum_store(log_pos, checksum);
    log_pos+= HA_CHECKSUM_STORE_SIZE;
  }
  else if (undo_type == LOGREC_UNDO_KEY_INSERT_WITH_ROOT ||
           undo_type == LOGREC_UNDO_KEY_DELETE_WITH_ROOT)
  {
    /* Key root changed. Store new key root */
    struct st_msg_to_write_hook_for_undo_key *undo_msg= extra_msg;
    ulonglong page;
    key_nr_store(log_pos, undo_msg->keynr);
    page= (undo_msg->value == HA_OFFSET_ERROR ? IMPOSSIBLE_PAGE_NO :
           undo_msg->value / info->s->block_size);
    page_store(log_pos + KEY_NR_STORE_SIZE, page);
    log_pos+= KEY_NR_STORE_SIZE + PAGE_STORE_SIZE;
  }
  log_array[TRANSLOG_INTERNAL_PARTS + 0].str=    (char*) log_data;
  log_array[TRANSLOG_INTERNAL_PARTS + 0].length= (uint) (log_pos - log_data);

  res= translog_write_record(res_lsn, LOGREC_CLR_END,
                             info->trn, info, log_array[TRANSLOG_INTERNAL_PARTS
                                                        + 0].length,
                             TRANSLOG_INTERNAL_PARTS + 1, log_array,
                             log_data + LSN_STORE_SIZE, &msg);
  DBUG_RETURN(res);
}


/**
   @brief Sets transaction's undo_lsn, first_undo_lsn if needed

   @return Operation status, always 0 (success)
*/

my_bool write_hook_for_clr_end(enum translog_record_type type
                               __attribute__ ((unused)),
                               TRN *trn, MARIA_HA *tbl_info
                               __attribute__ ((unused)),
                               LSN *lsn __attribute__ ((unused)),
                               void *hook_arg)
{
  MARIA_SHARE *share= tbl_info->s;
  struct st_msg_to_write_hook_for_clr_end *msg=
    (struct st_msg_to_write_hook_for_clr_end *)hook_arg;
  DBUG_ASSERT(trn->trid != 0);
  trn->undo_lsn= msg->previous_undo_lsn;

  switch (msg->undone_record_type) {
  case LOGREC_UNDO_ROW_DELETE:
    share->state.state.records++;
    share->state.state.checksum+= msg->checksum_delta;
    break;
  case LOGREC_UNDO_ROW_INSERT:
    share->state.state.records--;
    share->state.state.checksum+= msg->checksum_delta;
    break;
  case LOGREC_UNDO_ROW_UPDATE:
    share->state.state.checksum+= msg->checksum_delta;
    break;
  case LOGREC_UNDO_KEY_INSERT_WITH_ROOT:
  case LOGREC_UNDO_KEY_DELETE_WITH_ROOT:
  {
    /* Update key root */
    struct st_msg_to_write_hook_for_undo_key *extra_msg=
      (struct st_msg_to_write_hook_for_undo_key *) msg->extra_msg;
    *extra_msg->root= extra_msg->value;
    break;
  }
  case LOGREC_UNDO_KEY_INSERT:
  case LOGREC_UNDO_KEY_DELETE:
    break;
  default:
    DBUG_ASSERT(0);
  }
  if (trn->undo_lsn == LSN_IMPOSSIBLE) /* has fully rolled back */
    trn->first_undo_lsn= LSN_WITH_FLAGS_TO_FLAGS(trn->first_undo_lsn);
  return 0;
}


/**
  @brief write hook for undo key
*/

my_bool write_hook_for_undo_key(enum translog_record_type type,
                                TRN *trn, MARIA_HA *tbl_info,
                                LSN *lsn, void *hook_arg)
{
  struct st_msg_to_write_hook_for_undo_key *msg=
    (struct st_msg_to_write_hook_for_undo_key *) hook_arg;

  *msg->root= msg->value;
  _ma_fast_unlock_key_del(tbl_info);
  return write_hook_for_undo(type, trn, tbl_info, lsn, 0);
}


/**
   Upates "auto_increment" and calls the generic UNDO_KEY hook

   @return Operation status, always 0 (success)
*/

my_bool write_hook_for_undo_key_insert(enum translog_record_type type,
                                       TRN *trn, MARIA_HA *tbl_info,
                                       LSN *lsn, void *hook_arg)
{
  struct st_msg_to_write_hook_for_undo_key *msg=
    (struct st_msg_to_write_hook_for_undo_key *) hook_arg;
  MARIA_SHARE *share= tbl_info->s;
  if (msg->auto_increment > 0)
  {
    /*
      Only reason to set it here is to have a mutex protect from checkpoint
      reading at the same time (would see a corrupted value).
    */
    DBUG_PRINT("info",("auto_inc: %lu new auto_inc: %lu",
                       (ulong)share->state.auto_increment,
                       (ulong)msg->auto_increment));
    set_if_bigger(share->state.auto_increment, msg->auto_increment);
  }
  return write_hook_for_undo_key(type, trn, tbl_info, lsn, hook_arg);
}


/*****************************************************************************
  Functions for logging of key page changes
*****************************************************************************/

/**
   @brief
   Write log entry for page that has got data added or deleted at start of page
*/

my_bool _ma_log_prefix(MARIA_HA *info, my_off_t page,
                       uchar *buff, uint changed_length,
                       int move_length)
{
  uint translog_parts;
  LSN lsn;
  uchar log_data[FILEID_STORE_SIZE + PAGE_STORE_SIZE + 7 + 7], *log_pos;
  LEX_STRING log_array[TRANSLOG_INTERNAL_PARTS + 3];
  DBUG_ENTER("_ma_log_prefix");
  DBUG_PRINT("enter", ("page: %lu  changed_length: %u  move_length: %d",
                        (ulong) page, changed_length, move_length));

  page/= info->s->block_size;
  log_pos= log_data + FILEID_STORE_SIZE;
  page_store(log_pos, page);
  log_pos+= PAGE_STORE_SIZE;

  if (move_length < 0)
  {
    /* Delete prefix */
    log_pos[0]= KEY_OP_DEL_PREFIX;
    int2store(log_pos+1, -move_length);
    log_pos+= 3;
    if (changed_length)
    {
      /*
        We don't need a KEY_OP_OFFSET as KEY_OP_DEL_PREFIX has an implicit
        offset
      */
      log_pos[0]= KEY_OP_CHANGE;
      int2store(log_pos+1, changed_length);
      log_pos+= 3;
    }
  }
  else
  {
    /* Add prefix */
    DBUG_ASSERT(changed_length >0 && (int) changed_length >= move_length);
    log_pos[0]= KEY_OP_ADD_PREFIX;
    int2store(log_pos+1, move_length);
    int2store(log_pos+3, changed_length);
    log_pos+= 5;
  }

  translog_parts= 1;
  log_array[TRANSLOG_INTERNAL_PARTS + 0].str=    log_data;
  log_array[TRANSLOG_INTERNAL_PARTS + 0].length= (uint) (log_pos -
                                                         log_data);
  if (changed_length)
  {
    log_array[TRANSLOG_INTERNAL_PARTS + 1].str=    ((char*) buff +
                                                    info->s->keypage_header);
    log_array[TRANSLOG_INTERNAL_PARTS + 1].length= changed_length;
    translog_parts= 2;
  }

#ifdef EXTRA_DEBUG_KEY_CHANGES
  {
    int page_length= _ma_get_page_used(info->s, buff);
    ha_checksum crc;
    crc= my_checksum(0, buff + LSN_STORE_SIZE, page_length - LSN_STORE_SIZE);
    log_pos[0]= KEY_OP_CHECK;
    int2store(log_pos+1, page_length);
    int4store(log_pos+3, crc);

    log_array[TRANSLOG_INTERNAL_PARTS + translog_parts].str=    log_pos;
    log_array[TRANSLOG_INTERNAL_PARTS + translog_parts].length= 7;
    changed_length+= 7;
    translog_parts++;
  }
#endif

  DBUG_RETURN(translog_write_record(&lsn, LOGREC_REDO_INDEX,
                                    info->trn, info,
                                    log_array[TRANSLOG_INTERNAL_PARTS +
                                              0].length + changed_length,
                                    TRANSLOG_INTERNAL_PARTS + translog_parts,
                                    log_array, log_data, NULL));
}


/**
   @brief
   Write log entry for page that has got data added or deleted at end of page
*/

my_bool _ma_log_suffix(MARIA_HA *info, my_off_t page,
                       uchar *buff, uint org_length, uint new_length)
{
  LSN lsn;
  LEX_STRING log_array[TRANSLOG_INTERNAL_PARTS + 3];
  uchar log_data[FILEID_STORE_SIZE + PAGE_STORE_SIZE + 10 + 7], *log_pos;
  int diff;
  uint translog_parts, extra_length;
  DBUG_ENTER("_ma_log_suffix");
  DBUG_PRINT("enter", ("page: %lu  org_length: %u  new_length: %u",
                       (ulong) page, org_length, new_length));

  page/= info->s->block_size;

  log_pos= log_data + FILEID_STORE_SIZE;
  page_store(log_pos, page);
  log_pos+= PAGE_STORE_SIZE;

  if ((diff= (int) (new_length - org_length)) < 0)
  {
    log_pos[0]= KEY_OP_DEL_SUFFIX;
    int2store(log_pos+1, -diff);
    log_pos+= 3;
    translog_parts= 1;
    extra_length= 0;
  }
  else
  {
    log_pos[0]= KEY_OP_ADD_SUFFIX;
    int2store(log_pos+1, diff);
    log_pos+= 3;
    log_array[TRANSLOG_INTERNAL_PARTS + 1].str=   (char*) buff + org_length;
    log_array[TRANSLOG_INTERNAL_PARTS + 1].length= (uint) diff;
    translog_parts= 2;
    extra_length= (uint) diff;
  }

  log_array[TRANSLOG_INTERNAL_PARTS + 0].str=    log_data;
  log_array[TRANSLOG_INTERNAL_PARTS + 0].length= (uint) (log_pos -
                                                         log_data);

#ifdef EXTRA_DEBUG_KEY_CHANGES
  {
    ha_checksum crc;
    crc= my_checksum(0, buff + LSN_STORE_SIZE, new_length - LSN_STORE_SIZE);
    log_pos[0]= KEY_OP_CHECK;
    int2store(log_pos+1, new_length);
    int4store(log_pos+3, crc);

    log_array[TRANSLOG_INTERNAL_PARTS + translog_parts].str=    log_pos;
    log_array[TRANSLOG_INTERNAL_PARTS + translog_parts].length= 7;
    extra_length+= 7;
    translog_parts++;
  }
#endif

  DBUG_RETURN(translog_write_record(&lsn, LOGREC_REDO_INDEX,
                                    info->trn, info,
                                    log_array[TRANSLOG_INTERNAL_PARTS +
                                              0].length + extra_length,
                                    TRANSLOG_INTERNAL_PARTS + translog_parts,
                                    log_array, log_data, NULL));
}


/**
   @brief Log that a key was added to the page

   @param buff         Page buffer
   @param buff_length  Original length of buff (before key was added)

   @note
     If handle_overflow is set, then we have to protect against
     logging changes that is outside of the page.
     This may happen during underflow() handling where the buffer
     in memory temporary contains more data than block_size
*/

my_bool _ma_log_add(MARIA_HA *info, my_off_t page, uchar *buff,
                    uint buff_length, uchar *key_pos,
                    uint changed_length, int move_length,
                    my_bool handle_overflow __attribute__ ((unused)))
{
  LSN lsn;
  uchar log_data[FILEID_STORE_SIZE + PAGE_STORE_SIZE + 3 + 3 + 3 + 3 + 7];
  uchar *log_pos;
  LEX_STRING log_array[TRANSLOG_INTERNAL_PARTS + 3];
  uint offset= (uint) (key_pos - buff);
  uint page_length= info->s->block_size - KEYPAGE_CHECKSUM_SIZE;
  uint translog_parts;
  DBUG_ENTER("_ma_log_add");
  DBUG_PRINT("enter", ("page: %lu  org_page_length: %u  changed_length: %u  "
                       "move_length: %d",
                       (ulong) page, buff_length, changed_length,
                       move_length));
  DBUG_ASSERT(info->s->now_transactional);

  /*
    Write REDO entry that contains the logical operations we need
    to do the page
  */
  log_pos= log_data + FILEID_STORE_SIZE;
  page/= info->s->block_size;
  page_store(log_pos, page);
  log_pos+= PAGE_STORE_SIZE;

  if (buff_length + move_length > page_length)
  {
    /*
      Overflow. Cut either key or data from page end so that key fits
      The code that splits the too big page will ignore logging any
      data over page_length
    */
    DBUG_ASSERT(handle_overflow);
    if (offset + changed_length > page_length)
    {
      changed_length= page_length - offset;
      move_length= 0;
    }
    else
    {
      uint diff= buff_length + move_length - page_length;
      log_pos[0]= KEY_OP_DEL_SUFFIX;
      int2store(log_pos+1, diff);
      log_pos+= 3;
      buff_length= page_length - move_length;
    }
  }

  if (offset == buff_length)
    log_pos[0]= KEY_OP_ADD_SUFFIX;
  else
  {
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
  }
  int2store(log_pos+1, changed_length);
  log_pos+= 3;
  translog_parts= 2;

  log_array[TRANSLOG_INTERNAL_PARTS + 0].str=    log_data;
  log_array[TRANSLOG_INTERNAL_PARTS + 0].length= (uint) (log_pos -
                                                         log_data);
  log_array[TRANSLOG_INTERNAL_PARTS + 1].str=    key_pos;
  log_array[TRANSLOG_INTERNAL_PARTS + 1].length= changed_length;

#ifdef EXTRA_DEBUG_KEY_CHANGES
  {
    MARIA_SHARE *share= info->s;
    ha_checksum crc;
    uint save_page_length= _ma_get_page_used(share, buff);
    uint new_length= buff_length + move_length;
    _ma_store_page_used(share, buff, new_length);
    crc= my_checksum(0, buff + LSN_STORE_SIZE, new_length - LSN_STORE_SIZE);
    log_pos[0]= KEY_OP_CHECK;
    int2store(log_pos+1, new_length);
    int4store(log_pos+3, crc);

    log_array[TRANSLOG_INTERNAL_PARTS + translog_parts].str=    log_pos;
    log_array[TRANSLOG_INTERNAL_PARTS + translog_parts].length= 7;
    changed_length+= 7;
    translog_parts++;
    _ma_store_page_used(share, buff, save_page_length);
  }
#endif

  if (translog_write_record(&lsn, LOGREC_REDO_INDEX,
                            info->trn, info,
                            log_array[TRANSLOG_INTERNAL_PARTS +
                                      0].length + changed_length,
                            TRANSLOG_INTERNAL_PARTS + translog_parts,
                            log_array, log_data, NULL))
    DBUG_RETURN(-1);
  DBUG_RETURN(0);
}


/****************************************************************************
  Redo of key pages
****************************************************************************/

/**
   @brief Apply LOGREC_REDO_INDEX_NEW_PAGE

   @param  info            Maria handler
   @param  header          Header (without FILEID)

   @return Operation status
     @retval 0      OK
     @retval 1      Error
*/

uint _ma_apply_redo_index_new_page(MARIA_HA *info, LSN lsn,
                                   const uchar *header, uint length)
{
  ulonglong root_page= page_korr(header);
  ulonglong free_page= page_korr(header + PAGE_STORE_SIZE);
  uint      key_nr=    key_nr_korr(header + PAGE_STORE_SIZE * 2);
  my_bool   page_type_flag= header[PAGE_STORE_SIZE * 2 + KEY_NR_STORE_SIZE];
  enum pagecache_page_lock unlock_method;
  enum pagecache_page_pin unpin_method;
  MARIA_PINNED_PAGE page_link;
  my_off_t file_size;
  uchar *buff;
  uint result;
  MARIA_SHARE *share= info->s;
  DBUG_ENTER("_ma_apply_redo_index_new_page");
  DBUG_PRINT("enter", ("root_page: %lu  free_page: %lu",
                       (ulong) root_page, (ulong) free_page));

  /* Set header to point at key data */

  share->state.changed|= (STATE_CHANGED | STATE_NOT_OPTIMIZED_KEYS |
                          STATE_NOT_SORTED_PAGES | STATE_NOT_ZEROFILLED |
                          STATE_NOT_MOVABLE);

  header+= PAGE_STORE_SIZE * 2 + KEY_NR_STORE_SIZE + 1;
  length-= PAGE_STORE_SIZE * 2 + KEY_NR_STORE_SIZE + 1;

  /* free_page is 0 if we shouldn't set key_del */
  if (free_page)
  {
    if (free_page != IMPOSSIBLE_PAGE_NO)
      share->state.key_del= (my_off_t) free_page * share->block_size;
    else
      share->state.key_del= HA_OFFSET_ERROR;
  }
  file_size= (my_off_t) (root_page + 1) * share->block_size;

  /* If root page */
  if (page_type_flag &&
      cmp_translog_addr(lsn, share->state.is_of_horizon) >= 0)
    share->state.key_root[key_nr]= file_size - share->block_size;

  if (file_size > info->state->key_file_length)
  {
    info->state->key_file_length= file_size;
    buff= info->keyread_buff;
    info->keyread_buff_used= 1;
    unlock_method= PAGECACHE_LOCK_WRITE;
    unpin_method=  PAGECACHE_PIN;
  }
  else
  {
    if (!(buff= pagecache_read(share->pagecache, &share->kfile,
                               root_page, 0, 0,
                               PAGECACHE_PLAIN_PAGE, PAGECACHE_LOCK_WRITE,
                               &page_link.link)))
    {
      if (my_errno != HA_ERR_FILE_TOO_SHORT &&
          my_errno != HA_ERR_WRONG_CRC)
      {
        result= 1;
        goto err;
      }
      buff= pagecache_block_link_to_buffer(page_link.link);
    }
    else if (lsn_korr(buff) >= lsn)
    {
      /* Already applied */
      DBUG_PRINT("info", ("Page is up to date, skipping redo"));
      result= 0;
      goto err;
    }
    unlock_method= PAGECACHE_LOCK_LEFT_WRITELOCKED;
    unpin_method=  PAGECACHE_PIN_LEFT_PINNED;
  }

  /* Write modified page */
  bzero(buff, LSN_STORE_SIZE);
  memcpy(buff + LSN_STORE_SIZE, header, length);
  bzero(buff + LSN_STORE_SIZE + length,
        share->block_size - LSN_STORE_SIZE - KEYPAGE_CHECKSUM_SIZE - length);
  bfill(buff + share->block_size - KEYPAGE_CHECKSUM_SIZE,
        KEYPAGE_CHECKSUM_SIZE, (uchar) 255);

  result= 0;
  if (unlock_method == PAGECACHE_LOCK_WRITE &&
      pagecache_write(share->pagecache,
                      &share->kfile, root_page, 0,
                      buff, PAGECACHE_PLAIN_PAGE,
                      unlock_method, unpin_method,
                      PAGECACHE_WRITE_DELAY, &page_link.link,
                      LSN_IMPOSSIBLE))
    result= 1;

  /* Mark page to be unlocked and written at _ma_unpin_all_pages() */
  page_link.unlock= PAGECACHE_LOCK_WRITE_UNLOCK;
  page_link.changed= 1;
  push_dynamic(&info->pinned_pages, (void*) &page_link);
  DBUG_RETURN(result);

err:
  pagecache_unlock_by_link(share->pagecache, page_link.link,
                           PAGECACHE_LOCK_WRITE_UNLOCK,
                           PAGECACHE_UNPIN, LSN_IMPOSSIBLE,
                           LSN_IMPOSSIBLE, 0);
  DBUG_RETURN(result);
}


/**
   @brief Apply LOGREC_REDO_INDEX_FREE_PAGE

   @param  info            Maria handler
   @param  header          Header (without FILEID)

   @return Operation status
     @retval 0      OK
     @retval 1      Error
*/

uint _ma_apply_redo_index_free_page(MARIA_HA *info,
                                    LSN lsn,
                                    const uchar *header)
{
  ulonglong page= page_korr(header);
  ulonglong free_page= page_korr(header + PAGE_STORE_SIZE);
  my_off_t old_link;
  MARIA_PINNED_PAGE page_link;
  MARIA_SHARE *share= info->s;
  uchar *buff;
  int result;
  DBUG_ENTER("_ma_apply_redo_index_free_page");
  DBUG_PRINT("enter", ("page: %lu  free_page: %lu",
                       (ulong) page, (ulong) free_page));

  share->state.changed|= (STATE_CHANGED | STATE_NOT_OPTIMIZED_KEYS |
                          STATE_NOT_SORTED_PAGES | STATE_NOT_ZEROFILLED |
                          STATE_NOT_MOVABLE);

  share->state.key_del= (my_off_t) page * share->block_size;
  old_link=  ((free_page != IMPOSSIBLE_PAGE_NO) ?
              (my_off_t) free_page * share->block_size :
              HA_OFFSET_ERROR);
  if (!(buff= pagecache_read(share->pagecache, &share->kfile,
                             page, 0, 0,
                             PAGECACHE_PLAIN_PAGE, PAGECACHE_LOCK_WRITE,
                             &page_link.link)))
  {
    result= (uint) my_errno;
    goto err;
  }
  if (lsn_korr(buff) >= lsn)
  {
    /* Already applied */
    result= 0;
    goto err;
  }
  /* Free page */
  bzero(buff + LSN_STORE_SIZE, share->keypage_header - LSN_STORE_SIZE);
  _ma_store_keynr(share, buff, (uchar) MARIA_DELETE_KEY_NR);
  _ma_store_page_used(share, buff, share->keypage_header + 8);
  mi_sizestore(buff + share->keypage_header, old_link);

#ifdef IDENTICAL_PAGES_AFTER_RECOVERY
  {
    bzero(buff + share->keypage_header + 8,
          share->block_size - share->keypage_header - 8 -
          KEYPAGE_CHECKSUM_SIZE);
  }
#endif

  /* Mark page to be unlocked and written at _ma_unpin_all_pages() */
  page_link.unlock= PAGECACHE_LOCK_WRITE_UNLOCK;
  page_link.changed= 1;
  push_dynamic(&info->pinned_pages, (void*) &page_link);
  DBUG_RETURN(0);

err:
  pagecache_unlock_by_link(share->pagecache, page_link.link,
                           PAGECACHE_LOCK_WRITE_UNLOCK,
                           PAGECACHE_UNPIN, LSN_IMPOSSIBLE,
                           LSN_IMPOSSIBLE, 0);
  DBUG_RETURN(result);
}


/**
   @brief Apply LOGREC_REDO_INDEX

   @fn ma_apply_redo_index()
   @param  info            Maria handler
   @param  header          Header (without FILEID)

   @notes
     Data for this part is a set of logical instructions of how to
     construct the key page.

   Information of the layout of the components for REDO_INDEX:

   Name              Parameters (in byte) Information
   KEY_OP_OFFSET     2                    Set position for next operations
   KEY_OP_SHIFT      2 (signed int)       How much to shift down or up
   KEY_OP_CHANGE     2 length,  data      Data to replace at 'pos'
   KEY_OP_ADD_PREFIX 2 move-length        How much data should be moved up
                     2 change-length      Data to be replaced at page start
   KEY_OP_DEL_PREFIX 2 length             Bytes to be deleted at page start
   KEY_OP_ADD_SUFFIX 2 length, data       Add data to end of page
   KEY_OP_DEL_SUFFIX 2 length             Reduce page length with this
				          Sets position to start of page
   KEY_OP_CHECK      6 page_length[2},CRC Used only when debugging

   @return Operation status
     @retval 0      OK
     @retval 1      Error
*/

long my_counter= 0;

uint _ma_apply_redo_index(MARIA_HA *info,
                          LSN lsn, const uchar *header, uint head_length)
{
  MARIA_SHARE *share= info->s;
  ulonglong page= page_korr(header);
  MARIA_PINNED_PAGE page_link;
  uchar *buff;
  const uchar *header_end= header + head_length;
  uint page_offset= 0;
  uint nod_flag, page_length, keypage_header;
  int result;
  uint org_page_length;
  DBUG_ENTER("_ma_apply_redo_index");
  DBUG_PRINT("enter", ("page: %lu", (ulong) page));

  /* Set header to point at key data */
  header+= PAGE_STORE_SIZE;

  if (!(buff= pagecache_read(share->pagecache, &share->kfile,
                             page, 0, 0,
                             PAGECACHE_PLAIN_PAGE, PAGECACHE_LOCK_WRITE,
                             &page_link.link)))
  {
    result= 1;
    goto err;
  }
  if (lsn_korr(buff) >= lsn)
  {
    /* Already applied */
    DBUG_PRINT("info", ("Page is up to date, skipping redo"));
    result= 0;
    goto err;
  }

  _ma_get_used_and_nod(share, buff, page_length, nod_flag);
  keypage_header= share->keypage_header;
  org_page_length= page_length;
  DBUG_PRINT("info", ("page_length: %u", page_length));

  /* Apply modifications to page */
  do
  {
    switch ((enum en_key_op) (*header++)) {
    case KEY_OP_OFFSET:                         /* 1 */
      page_offset= uint2korr(header);
      header+= 2;
      DBUG_ASSERT(page_offset >= keypage_header && page_offset <= page_length);
      break;
    case KEY_OP_SHIFT:                          /* 2 */
    {
      int length= sint2korr(header);
      header+= 2;
      DBUG_ASSERT(page_offset != 0 && page_offset <= page_length &&
                  page_length + length < share->block_size);

      if (length < 0)
        bmove(buff + page_offset, buff + page_offset - length,
              page_length - page_offset + length);
      else
        bmove_upp(buff + page_length + length, buff + page_length,
                  page_length - page_offset);
      page_length+= length;
      break;
    }
    case KEY_OP_CHANGE:                         /* 3 */
    {
      uint length= uint2korr(header);
      DBUG_ASSERT(page_offset != 0 && page_offset + length <= page_length);

      memcpy(buff + page_offset, header + 2 , length);
      header+= 2 + length;
      break;
    }
    case KEY_OP_ADD_PREFIX:                     /* 4 */
    {
      uint insert_length= uint2korr(header);
      uint changed_length= uint2korr(header+2);
      DBUG_ASSERT(insert_length <= changed_length &&
                  page_length + changed_length <= share->block_size);

      bmove_upp(buff + page_length + insert_length, buff + page_length,
                page_length - keypage_header);
      memcpy(buff + keypage_header, header + 4 , changed_length);
      header+= 4 + changed_length;
      page_length+= insert_length;
      break;
    }
    case KEY_OP_DEL_PREFIX:                     /* 5 */
    {
      uint length= uint2korr(header);
      header+= 2;
      DBUG_ASSERT(length <= page_length - keypage_header);

      bmove(buff + keypage_header, buff + keypage_header +
            length, page_length - keypage_header - length);
      page_length-= length;

      page_offset= keypage_header;              /* Prepare for change */
      break;
    }
    case KEY_OP_ADD_SUFFIX:                     /* 6 */
    {
      uint insert_length= uint2korr(header);
      DBUG_ASSERT(page_length + insert_length <= share->block_size);
      memcpy(buff + page_length, header+2, insert_length);

      page_length+= insert_length;
      header+= 2 + insert_length;
      break;
    }
    case KEY_OP_DEL_SUFFIX:                     /* 7 */
    {
      uint del_length= uint2korr(header);
      header+= 2;
      DBUG_ASSERT(page_length - del_length >= keypage_header);
      page_length-= del_length;
      break;
    }
    case KEY_OP_CHECK:                          /* 8 */
    {
#ifdef EXTRA_DEBUG_KEY_CHANGES
      uint check_page_length;
      ha_checksum crc;
      check_page_length= uint2korr(header);
      crc=               uint4korr(header+2);
      _ma_store_page_used(share, buff, page_length);
      DBUG_ASSERT(check_page_length == page_length);
      DBUG_ASSERT(crc == (uint32) my_checksum(0, buff + LSN_STORE_SIZE,
                                              page_length- LSN_STORE_SIZE));
#endif
      header+= 6;
      break;
    }
    case KEY_OP_NONE:
    default:
      DBUG_ASSERT(0);
      result= 1;
      goto err;
    }
  } while (header < header_end);
  DBUG_ASSERT(header == header_end);

  /* Write modified page */
  _ma_store_page_used(share, buff, page_length);

  /*
    Clean old stuff up. Gives us better compression of we archive things
    and makes things easer to debug
  */
  if (page_length < org_page_length)
    bzero(buff + page_length, org_page_length-page_length);

  /* Mark page to be unlocked and written at _ma_unpin_all_pages() */
  page_link.unlock= PAGECACHE_LOCK_WRITE_UNLOCK;
  page_link.changed= 1;
  push_dynamic(&info->pinned_pages, (void*) &page_link);
  DBUG_RETURN(0);

err:
  pagecache_unlock_by_link(share->pagecache, page_link.link,
                           PAGECACHE_LOCK_WRITE_UNLOCK,
                           PAGECACHE_UNPIN, LSN_IMPOSSIBLE,
                           LSN_IMPOSSIBLE, 0);
  if (result)
    _ma_mark_file_crashed(share);
  DBUG_RETURN(result);
}


/****************************************************************************
  Undo of key block changes
****************************************************************************/

/**
   @brief Undo of insert of key (ie, delete the inserted key)
*/

my_bool _ma_apply_undo_key_insert(MARIA_HA *info, LSN undo_lsn,
                                  const uchar *header, uint length)
{
  LSN lsn;
  my_bool res;
  uint keynr;
  uchar key[HA_MAX_KEY_BUFF];
  MARIA_SHARE *share= info->s;
  my_off_t new_root;
  struct st_msg_to_write_hook_for_undo_key msg;
  DBUG_ENTER("_ma_apply_undo_key_insert");

  share->state.changed|= (STATE_CHANGED | STATE_NOT_OPTIMIZED_KEYS |
                          STATE_NOT_SORTED_PAGES | STATE_NOT_ZEROFILLED |
                          STATE_NOT_MOVABLE);
  keynr= key_nr_korr(header);
  length-= KEY_NR_STORE_SIZE;

  /* We have to copy key as _ma_ck_real_delete() may change it */
  memcpy(key, header + KEY_NR_STORE_SIZE, length);
  DBUG_DUMP("key", key, length);

  new_root= share->state.key_root[keynr];
  res= _ma_ck_real_delete(info, share->keyinfo+keynr, key,
                          length - share->rec_reflength, &new_root);
  if (res)
    _ma_mark_file_crashed(share);
  msg.root= &share->state.key_root[keynr];
  msg.value= new_root;
  msg.keynr= keynr;

  if (_ma_write_clr(info, undo_lsn, *msg.root == msg.value ?
                    LOGREC_UNDO_KEY_INSERT : LOGREC_UNDO_KEY_INSERT_WITH_ROOT,
                    0, 0, &lsn, (void*) &msg))
    res= 1;

  _ma_fast_unlock_key_del(info);
  _ma_unpin_all_pages_and_finalize_row(info, lsn);
  DBUG_RETURN(res);
}


/**
   @brief Undo of delete of key (ie, insert the deleted key)
*/

my_bool _ma_apply_undo_key_delete(MARIA_HA *info, LSN undo_lsn,
                                  const uchar *header, uint length)
{
  LSN lsn;
  my_bool res;
  uint keynr;
  uchar key[HA_MAX_KEY_BUFF];
  MARIA_SHARE *share= info->s;
  my_off_t new_root;
  struct st_msg_to_write_hook_for_undo_key msg;
  DBUG_ENTER("_ma_apply_undo_key_delete");

  share->state.changed|= (STATE_CHANGED | STATE_NOT_OPTIMIZED_KEYS |
                          STATE_NOT_SORTED_PAGES | STATE_NOT_ZEROFILLED |
                          STATE_NOT_MOVABLE);
  keynr= key_nr_korr(header);
  length-= KEY_NR_STORE_SIZE;

  /* We have to copy key as _ma_ck_real_write_btree() may change it */
  memcpy(key, header + KEY_NR_STORE_SIZE, length);
  DBUG_DUMP("key", key, length);

  new_root= share->state.key_root[keynr];
  res= _ma_ck_real_write_btree(info, share->keyinfo+keynr, key,
                               length - share->rec_reflength,
                               &new_root,
                               share->keyinfo[keynr].write_comp_flag);
  if (res)
    _ma_mark_file_crashed(share);

  msg.root= &share->state.key_root[keynr];
  msg.value= new_root;
  msg.keynr= keynr;
  if (_ma_write_clr(info, undo_lsn,
                    *msg.root == msg.value ?
                    LOGREC_UNDO_KEY_DELETE : LOGREC_UNDO_KEY_DELETE_WITH_ROOT,
                    0, 0, &lsn,
                    (void*) &msg))
    res= 1;

  _ma_fast_unlock_key_del(info);
  _ma_unpin_all_pages_and_finalize_row(info, lsn);
  DBUG_RETURN(res);
}


/****************************************************************************
  Handle some local variables
****************************************************************************/

/**
  @brief lock key_del for other threads usage

  @fn     _ma_lock_key_del()
  @param  info            Maria handler
  @param  insert_at_end   Set to 1 if we are doing an insert

  @notes
    To allow higher concurrency in the common case where we do inserts
    and we don't have any linked blocks we do the following:
    - Mark in info->used_key_del that we are not using key_del
    - Return at once (without marking key_del as used)

    This is safe as we in this case don't write current_key_del into
    the redo log and during recover we are not updating key_del.
*/

my_bool _ma_lock_key_del(MARIA_HA *info, my_bool insert_at_end)
{
  MARIA_SHARE *share= info->s;

  if (info->used_key_del != 1)
  {
    pthread_mutex_lock(&share->intern_lock);
    if (share->state.key_del == HA_OFFSET_ERROR && insert_at_end)
    {
      pthread_mutex_unlock(&share->intern_lock);
      info->used_key_del= 2;                  /* insert-with-append */
      return 1;
    }
#ifdef THREAD
    while (share->used_key_del)
      pthread_cond_wait(&share->intern_cond, &share->intern_lock);
#endif
    info->used_key_del= 1;
    share->used_key_del= 1;
    share->current_key_del= share->state.key_del;
    pthread_mutex_unlock(&share->intern_lock);
  }
  return 0;
}


/**
  @brief copy changes to key_del and unlock it
*/

void _ma_unlock_key_del(MARIA_HA *info)
{
  DBUG_ASSERT(info->used_key_del);
  if (info->used_key_del == 1)                  /* Ignore insert-with-append */
  {
    MARIA_SHARE *share= info->s;
    pthread_mutex_lock(&share->intern_lock);
    share->used_key_del= 0;
    share->state.key_del= info->s->current_key_del;
    pthread_mutex_unlock(&share->intern_lock);
    pthread_cond_signal(&share->intern_cond);
  }
  info->used_key_del= 0;
}
