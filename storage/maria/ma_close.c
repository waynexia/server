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

/* close a isam-database */
/*
  TODO:
   We need to have a separate mutex on the closed file to allow other threads
   to open other files during the time we flush the cache and close this file
*/

#include "maria_def.h"

int maria_close(register MARIA_HA *info)
{
  int error=0,flag;
  MARIA_SHARE *share=info->s;
  DBUG_ENTER("maria_close");
  DBUG_PRINT("enter",("base: 0x%lx  reopen: %u  locks: %u",
		      (long) info, (uint) share->reopen,
                      (uint) share->tot_locks));

  pthread_mutex_lock(&THR_LOCK_maria);
  if (info->lock_type == F_EXTRA_LCK)
    info->lock_type=F_UNLCK;			/* HA_EXTRA_NO_USER_CHANGE */

  if (share->reopen == 1 && share->kfile >= 0)
    _ma_decrement_open_count(info);

  if (info->lock_type != F_UNLCK)
  {
    if (maria_lock_database(info,F_UNLCK))
      error=my_errno;
  }
  pthread_mutex_lock(&share->intern_lock);

  if (share->options & HA_OPTION_READ_ONLY_DATA)
  {
    share->r_locks--;
    share->tot_locks--;
  }
  if (info->opt_flag & (READ_CACHE_USED | WRITE_CACHE_USED))
  {
    if (end_io_cache(&info->rec_cache))
      error=my_errno;
    info->opt_flag&= ~(READ_CACHE_USED | WRITE_CACHE_USED);
  }
  flag= !--share->reopen;
  /*
    RECOVERYTODO:
    If "flag" is TRUE, in the line below we are going to make the table
    unknown to future checkpoints, so it needs to have fsync'ed itself
    entirely (bitmap, pages, etc) at this point.
    The flushing is currently done a few lines further (which is ok, as we
    still hold THR_LOCK_maria), but syncing is missing.
  */
  maria_open_list=list_delete(maria_open_list,&info->open_list);
  pthread_mutex_unlock(&share->intern_lock);

  my_free(_ma_get_rec_buff_ptr(info, info->rec_buff), MYF(MY_ALLOW_ZERO_PTR));
  if (flag)
  {
    if (share->kfile >= 0 &&
	flush_key_blocks(share->key_cache, share->kfile,
			 share->temporary ? FLUSH_IGNORE_CHANGED :
			 FLUSH_RELEASE))
      error=my_errno;
    if (share->kfile >= 0)
    {
      /*
        If we are crashed, we can safely flush the current state as it will
        not change the crashed state.
        We can NOT write the state in other cases as other threads
        may be using the file at this point
      */
      if (share->mode != O_RDONLY && maria_is_crashed(info))
	_ma_state_info_write(share->kfile, &share->state, 1);
      if (my_close(share->kfile,MYF(0)))
        error = my_errno;
    }
#ifdef HAVE_MMAP
    if (share->file_map)
      _ma_unmap_file(info);
#endif
    if (share->decode_trees)
    {
      my_free((gptr) share->decode_trees,MYF(0));
      my_free((gptr) share->decode_tables,MYF(0));
    }
#ifdef THREAD
    thr_lock_delete(&share->lock);
    VOID(pthread_mutex_destroy(&share->intern_lock));
    {
      int i,keys;
      keys = share->state.header.keys;
      VOID(rwlock_destroy(&share->mmap_lock));
      for(i=0; i<keys; i++) {
	VOID(rwlock_destroy(&share->key_root_lock[i]));
      }
    }
#endif
    my_free((gptr) info->s,MYF(0));
  }
  pthread_mutex_unlock(&THR_LOCK_maria);
  if (info->ftparser_param)
  {
    my_free((gptr)info->ftparser_param, MYF(0));
    info->ftparser_param= 0;
  }
  if (info->dfile >= 0 && my_close(info->dfile,MYF(0)))
    error = my_errno;

  my_free((gptr) info,MYF(0));

  if (error)
  {
    DBUG_RETURN(my_errno=error);
  }
  DBUG_RETURN(0);
} /* maria_close */
