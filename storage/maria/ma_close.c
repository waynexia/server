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

  if (share->reopen == 1 && share->kfile.file >= 0)
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
  maria_open_list=list_delete(maria_open_list,&info->open_list);
  pthread_mutex_unlock(&share->intern_lock);

  my_free(info->rec_buff, MYF(MY_ALLOW_ZERO_PTR));
  (*share->end)(info);

  if (flag)
  {
    if (share->kfile.file >= 0)
    {
      if ((*share->once_end)(share))
        error= my_errno;
      if (flush_pagecache_blocks(share->pagecache, &share->kfile,
                                 (share->temporary ?
                                  FLUSH_IGNORE_CHANGED :
                                  FLUSH_RELEASE)))
        error= my_errno;
      /*
        File must be synced as it is going out of the maria_open_list and so
        becoming unknown to Checkpoint.
      */
      if (my_sync(share->kfile.file, MYF(MY_WME)))
        error= my_errno;
      /*
        If we are crashed, we can safely flush the current state as it will
        not change the crashed state.
        We can NOT write the state in other cases as other threads
        may be using the file at this point
        IF using --external-locking, which does not apply to Maria.
      */
      if (share->mode != O_RDONLY && maria_is_crashed(info))
	_ma_state_info_write(share->kfile.file, &share->state, 1);
      if (my_close(share->kfile.file, MYF(0)))
        error= my_errno;
    }
#ifdef HAVE_MMAP
    if (share->file_map)
      _ma_unmap_file(info);
#endif
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
  if (info->dfile.file >= 0 && my_close(info->dfile.file, MYF(0)))
    error = my_errno;

  my_free((gptr) info,MYF(0));

  if (error)
    DBUG_RETURN(my_errno= error);
  DBUG_RETURN(0);
} /* maria_close */
