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

/*
  WL#3071 Maria checkpoint
  First version written by Guilhem Bichot on 2006-04-27.
  Does not compile yet.
*/

/* Here is the implementation of this module */

/*
  Summary:
  - there are asynchronous checkpoints (a writer to the log notices that it's
  been a long time since we last checkpoint-ed, so posts a request for a
  background thread to do a checkpoint; does not care about the success of the
  checkpoint). Then the checkpoint is done by the checkpoint thread, at an
  unspecified moment ("later") (==soon, of course).
  - there are synchronous checkpoints: a thread requests a checkpoint to
  happen now and wants to know when it finishes and if it succeeded; then the
  checkpoint is done by that same thread.
*/

#include "page_cache.h"
#include "least_recently_dirtied.h"
#include "transaction.h"
#include "share.h"
#include "log.h"

/* could also be called LSN_ERROR */
#define LSN_IMPOSSIBLE ((LSN)0)
#define LSN_MAX ((LSN)ULONGLONG_MAX)

/*
  this transaction is used for any system work (purge, checkpoint writing
  etc), that is, background threads. It will not be declared/initialized here
  in the final version.
*/
st_transaction system_trans= {0 /* long trans id */, 0 /* short trans id */,0,...};

/* those three are protected by the log's mutex */
/*
  The maximum rec_lsn in the LRD when last checkpoint was run, serves for the
  MEDIUM checkpoint.
*/
LSN max_rec_lsn_at_last_checkpoint= 0;
CHECKPOINT_LEVEL next_asynchronous_checkpoint_to_do= NONE;
CHECKPOINT_LEVEL synchronous_checkpoint_in_progress= NONE;

/*
  Used by MySQL client threads requesting a checkpoint (like "ALTER MARIA
  ENGINE DO CHECKPOINT"), and probably by maria_panic(), and at the end of the
  UNDO recovery phase.
*/
my_bool execute_synchronous_checkpoint(CHECKPOINT_LEVEL level)
{
  DBUG_ENTER("execute_synchronous_checkpoint");
  DBUG_ASSERT(level > NONE);

  lock(log_mutex);
  while ((synchronous_checkpoint_in_progress != NONE) ||
         (next_asynchronous_checkpoint_to_do != NONE))
    wait_on_checkpoint_done_cond();

  synchronous_checkpoint_in_progress= level;
  execute_checkpoint(level);
  safemutex_assert_owner(log_mutex);
  synchronous_checkpoint_in_progress= NONE;
  unlock(log_mutex);
  broadcast(checkpoint_done_cond);
}

/* Picks a checkpoint request, if there is one, and executes it */
my_bool execute_asynchronous_checkpoint_if_any()
{
  CHECKPOINT_LEVEL level;
  DBUG_ENTER("execute_asynchronous_checkpoint");

  lock(log_mutex);
  if (likely(next_asynchronous_checkpoint_to_do == NONE))
  {
    unlock(log_mutex);
    DBUG_RETURN(FALSE);
  }

  while (synchronous_checkpoint_in_progress)
    wait_on_checkpoint_done_cond();

do_checkpoint:
  level= next_asynchronous_checkpoint_to_do;
  DBUG_ASSERT(level > NONE);
  execute_checkpoint(level);
  safemutex_assert_owner(log_mutex);
  if (next_asynchronous_checkpoint_to_do > level)
    goto do_checkpoint;     /* one more request was posted */
  else
  {
    DBUG_ASSERT(next_asynchronous_checkpoint_to_do == level);
    next_asynchronous_checkpoint_to_do= NONE; /* all work done */
  }
  unlock(log_mutex);
  broadcast(checkpoint_done_cond);
}


/*
  Does the actual checkpointing. Called by
  execute_synchronous_checkpoint() and
  execute_asynchronous_checkpoint_if_any().
*/
my_bool execute_checkpoint(CHECKPOINT_LEVEL level)
{
  LSN candidate_max_rec_lsn_at_last_checkpoint;
  /* to avoid { lock + no-op + unlock } in the common (==indirect) case */
  my_bool need_log_mutex;

  DBUG_ENTER("execute_checkpoint");

  safemutex_assert_owner(log_mutex);
  copy_of_max_rec_lsn_at_last_checkpoint= max_rec_lsn_at_last_checkpoint;

  if (unlikely(need_log_mutex= (level > INDIRECT)))
  {
    /* much I/O work to do, release log mutex */
    unlock(log_mutex);

    switch (level)
    {
    case FULL:
      /* flush all pages up to the current end of the LRD */
      flush_all_LRD_to_lsn(LSN_MAX);
      /* this will go full speed (normal scheduling, no sleep) */
      break;
    case MEDIUM:
      /*
        flush all pages which were already dirty at last checkpoint:
        ensures that recovery will never start from before the next-to-last
        checkpoint (two-checkpoint rule).
        It is max, not min as the WL says (TODO update WL).
      */
      flush_all_LRD_to_lsn(copy_of_max_rec_lsn_at_last_checkpoint);
      /* this will go full speed (normal scheduling, no sleep) */
      break;
    }
  }

  candidate_max_rec_lsn_at_last_checkpoint= checkpoint_indirect(need_log_mutex);

  lock(log_mutex);
  /*
    this portion cannot be done as a hook in write_log_record() for the
    LOGREC_CHECKPOINT type because:
    - at that moment we still have not written to the control file so cannot
    mark the request as done; this could be solved by writing to the control
    file in the hook but that would be an I/O under the log's mutex, bad.
    - it would not be nice organisation of code (I tried it :).
  */
  if (candidate_max_rec_lsn_at_last_checkpoint != LSN_IMPOSSIBLE)
  {
    /* checkpoint succeeded */
    maximum_rec_lsn_last_checkpoint= candidate_max_rec_lsn_at_last_checkpoint;
    written_since_last_checkpoint= (my_off_t)0;
    DBUG_RETURN(FALSE);
  }
  /*
    keep mutex locked because callers will want to clear mutex-protected
    status variables
  */
  DBUG_RETURN(TRUE);
}


/*
  Does an indirect checpoint (collects data from data structures, writes into
  a checkpoint log record).
  Returns the largest LSN of the LRD when the checkpoint happened (this is a
  fuzzy definition), or LSN_IMPOSSIBLE on error. That LSN is used for the
  "two-checkpoint rule" (MEDIUM checkpoints).
*/
LSN checkpoint_indirect(my_bool need_log_mutex)
{
  DBUG_ENTER("checkpoint_indirect");

  int error= 0;
  /* checkpoint record data: */
  LSN checkpoint_start_lsn;
  LEX_STRING string1={0,0}, string2={0,0}, string3={0,0};
  LEX_STRING *string_array[4];
  char *ptr;
  LSN checkpoint_lsn;
  LSN candidate_max_rec_lsn_at_last_checkpoint= 0;
  list_element *el;   /* to scan lists */
  ulong stored_LRD_size= 0;


  DBUG_ASSERT(sizeof(byte *) <= 8);
  DBUG_ASSERT(sizeof(LSN) <= 8);

  if (need_log_mutex)
    lock(log_mutex); /* maybe this will clash with log_read_end_lsn() */
  checkpoint_start_lsn= log_read_end_lsn();
  unlock(log_mutex);

  DBUG_PRINT("info",("checkpoint_start_lsn %lu", checkpoint_start_lsn));

  /* STEP 1: fetch information about dirty pages */

  /*
    We lock the entire cache but will be quick, just reading/writing a few MBs
    of memory at most.
  */
  pagecache_pthread_mutex_lock(&pagecache->cache_lock);

  /*
    This is an over-estimation, as in theory blocks_changed may contain
    non-PAGECACHE_LSN_PAGE pages, which we don't want to store into the
    checkpoint record; the true number of page-LRD-info we'll store into the
    record is stored_LRD_size.
  */
  string1.length= 8+8+(8+8)*pagecache->blocks_changed;
  if (NULL == (string1.str= my_malloc(string1.length)))
    goto err;
  ptr= string1.str;
  int8store(ptr, checkpoint_start_lsn);
  ptr+= 8+8; /* don't store stored_LRD_size now, wait */
  if (pagecache->blocks_changed > 0)
  {
    /*
      There are different ways to scan the dirty blocks;
      flush_all_key_blocks() uses a loop over pagecache->used_last->next_used,
      and for each element of the loop, loops into
      pagecache->changed_blocks[FILE_HASH(file of the element)].
      This has the drawback that used_last includes non-dirty blocks, and it's
      two loops over many elements. Here we try something simpler.
      If there are no blocks in changed_blocks[file_hash], we should hit
      zeroes and skip them.
    */
    uint file_hash;
    for (file_hash= 0; file_hash < PAGECACHE_CHANGED_BLOCKS_HASH; file_hash++)
    {
      PAGECACHE_BLOCK_LINK *block;
      for (block= pagecache->changed_blocks[file_hash] ;
           block;
           block= block->next_changed)
      {
        DBUG_ASSERT(block->hash_link != NULL);
        DBUG_ASSERT(block->status & BLOCK_CHANGED);
        if (block->type != PAGECACHE_LSN_PAGE)
        {
          /* no need to store it in the checkpoint record */
          continue;
        }
        /* Q: two "block"s cannot have the same "hash_link", right? */
        int8store(ptr, block->hash_link->pageno);
        ptr+= 8;
        /* I assume rec_lsn will be member of "block", not of "hash_link" */
        int8store(ptr, block->rec_lsn);
        ptr+= 8;
        stored_LRD_size++;
        set_if_bigger(candidate_max_rec_lsn_at_last_checkpoint,
                      block->rec_lsn);
      }
    }
  pagecache_pthread_mutex_unlock(&pagecache->cache_lock);
  DBUG_ASSERT(stored_LRD_size <= pagecache->blocks_changed);
  int8store(string1.str+8, stored_LRD_size);
  string1.length= 8+8+(8+8)*stored_LRD_size;

  /* STEP 2: fetch information about transactions */

  /*
    If trx are in more than one list (e.g. three:
    running transactions, committed transactions, purge queue), we can either
    take mutexes of all three together or do crabbing.
    But if an element can move from list 1 to list 3 without passing through
    list 2, crabbing is dangerous.
    Hopefully it's ok to take 3 mutexes together...
    Otherwise I'll have to make sure I miss no important trx and I handle dups.
  */
  lock(global_transactions_list_mutex); /* or 3 mutexes if there are 3 */
  string2.length= 8+(8+8)*trx_list->count;
  if (NULL == (string2.str= my_malloc(string2.length)))
    goto err;
  ptr= string2.str;
  int8store(ptr, trx_list->count);
  ptr+= 8;
  for (el= trx_list->first; el; el= el->next)
  {
    /* possibly latch el.rwlock */
    *ptr= el->state;
    ptr++;
    int7store(ptr, el->long_trans_id);
    ptr+= 7;
    int2store(ptr, el->short_trans_id);
    ptr+= 2;
    int8store(ptr, el->undo_lsn);
    ptr+= 8;
    int8store(ptr, el->undo_purge_lsn);
    ptr+= 8;
    /*
      if no latch, use double variable of type ULONGLONG_CONSISTENT in
      st_transaction, or even no need if Intel >=486
    */
    int8store(ptr, el->first_undo_lsn);
    ptr+= 8;
    /* possibly unlatch el.rwlock */
  }
  unlock(global_transactions_list_mutex);

  /* STEP 3: fetch information about table files */

  /* This global mutex is in fact THR_LOCK_maria (see ma_open()) */
  lock(global_share_list_mutex);
  string3.length= 8+(8+8)*share_list->count;
  if (NULL == (string3.str= my_malloc(string3.length)))
    goto err;
  ptr= string3.str;
  /* possibly latch each MARIA_SHARE, one by one, like this: */
  pthread_mutex_lock(&share->intern_lock);
  /*
    We'll copy the file id (a bit like share->kfile), the file name
    (like share->unique_file_name[_length]).
  */
  make_copy_of_global_share_list_to_array;
  pthread_mutex_unlock(&share->intern_lock);
  unlock(global_share_list_mutex);

  /* work on copy */
  int8store(ptr, elements_in_array);
  ptr+= 8;
  for (el in array)
  {
    int8store(ptr, array[...].file_id);
    ptr+= 8;
    memcpy(ptr, array[...].file_name, ...);
    ptr+= ...;
    /*
      these two are long ops (involving disk I/O) that's why we copied the
      list, to not keep the list locked for long:
    */
    flush_bitmap_pages(el);
    /* TODO: and also autoinc counter, logical file end, free page list */

    /*
      fsyncs the fd, that's the loooong operation (e.g. max 150 fsync per
      second, so if you have touched 1000 files it's 7 seconds).
    */
    force_file(el);
  }

  /* LAST STEP: now write the checkpoint log record */

  string_array[0]= string1;
  string_array[1]= string2;
  string_array[2]= string3;
  string_array[3]= NULL;

  checkpoint_lsn= log_write_record(LOGREC_CHECKPOINT,
                                   &system_trans, string_array);

  /*
    Do nothing between the log write and the control file write, for the
    "repair control file" tool to be possible one day.
  */

  if (LSN_IMPOSSIBLE == checkpoint_lsn)
    goto err;

  if (0 != control_file_write_and_force(checkpoint_lsn, NULL))
    goto err;

  goto end;

err:
  print_error_to_error_log(the_error_message);
  candidate_max_rec_lsn_at_last_checkpoint= LSN_IMPOSSIBLE;

end:
  my_free(buffer1.str, MYF(MY_ALLOW_ZERO_PTR));
  my_free(buffer2.str, MYF(MY_ALLOW_ZERO_PTR));
  my_free(buffer3.str, MYF(MY_ALLOW_ZERO_PTR));

  DBUG_RETURN(candidate_max_rec_lsn_at_last_checkpoint);
}



/*
  Here's what should be put in log_write_record() in the log handler:
*/
log_write_record(...)
{
  ...;
  lock(log_mutex);
  ...;
  write_to_log(length);
  written_since_last_checkpoint+= length;
  if (written_since_last_checkpoint >
      MAX_LOG_BYTES_WRITTEN_BETWEEN_CHECKPOINTS)
  {
    /*
      ask one system thread (the "LRD background flusher and checkpointer
      thread" WL#3261) to do a checkpoint
    */
    request_asynchronous_checkpoint(INDIRECT);
  }
  ...;
  unlock(log_mutex);
  ...;
}

/*
  Requests a checkpoint from the background thread, *asynchronously*
  (requestor does not wait for completion, and does not even later check the
  result).
  In real life it will be called by log_write_record().
*/
void request_asynchronous_checkpoint(CHECKPOINT_LEVEL level);
{
  safemutex_assert_owner(log_mutex);

  DBUG_ASSERT(level > NONE);
  if (checkpoint_request < level)
  {
    /* no equal or stronger running or to run, we post request */
    /*
      note that thousands of requests for checkpoints are going to come all
      at the same time (when the log bound
      MAX_LOG_BYTES_WRITTEN_BETWEEN_CHECKPOINTS is passed), so it may not be a
      good idea for each of them to broadcast a cond to wake up the background
      checkpoint thread. We just don't broacast a cond, the checkpoint thread
      (see least_recently_dirtied.c) will notice our request in max a few
      seconds.
    */
    checkpoint_request= level; /* post request */
  }

  /*
    If there was an error, only an error
    message to the error log will say it; normal, for a checkpoint triggered
    by a log write, we probably don't want the client's log write to throw an
    error, as the log write succeeded and a checkpoint failure is not
    critical: the failure in this case is more for the DBA to know than for
    the end user.
  */
}
