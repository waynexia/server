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

#include "ma_fulltext.h"
#include "trnman_public.h"

/**
   @brief drops (deletes) a table

   @param  name             table's name

   @return Operation status
     @retval 0      ok
     @retval 1      error
*/

int maria_delete_table(const char *name)
{
  char from[FN_REFLEN];
#ifdef USE_RAID
  uint raid_type=0,raid_chunks=0;
#endif
  MARIA_HA *info;
  myf sync_dir;
  DBUG_ENTER("maria_delete_table");

#ifdef EXTRA_DEBUG
  _ma_check_table_is_closed(name,"delete");
#endif
  /** @todo LOCK take X-lock on table */
  /*
    We need to know if this table is transactional.
    When built with RAID support, we also need to determine if this table
    makes use of the raid feature. If yes, we need to remove all raid
    chunks. This is done with my_raid_delete(). Unfortunately it is
    necessary to open the table just to check this. We use
    'open_for_repair' to be able to open even a crashed table. If even
    this open fails, we assume no raid configuration for this table
    and try to remove the normal data file only. This may however
    leave the raid chunks behind.
  */
  if (!(info= maria_open(name, O_RDONLY, HA_OPEN_FOR_REPAIR)))
  {
#ifdef USE_RAID
    raid_type= 0;
#endif
    sync_dir= 0;
  }
  else
  {
#ifdef USE_RAID
    raid_type=      info->s->base.raid_type;
    raid_chunks=    info->s->base.raid_chunks;
#endif
    sync_dir= (info->s->now_transactional && !info->s->temporary &&
               !maria_in_recovery) ?
      MY_SYNC_DIR : 0;
    maria_close(info);
  }
#ifdef USE_RAID
#ifdef EXTRA_DEBUG
  _ma_check_table_is_closed(name,"delete");
#endif
#endif /* USE_RAID */

  if (sync_dir)
  {
    /*
      For this log record to be of any use for Recovery, we need the upper
      MySQL layer to be crash-safe in DDLs.
      For now this record can serve when we apply logs to a backup, so we sync
      it.
    */
    LSN lsn;
    LEX_CUSTRING log_array[TRANSLOG_INTERNAL_PARTS + 1];
    log_array[TRANSLOG_INTERNAL_PARTS + 0].str=    name;
    log_array[TRANSLOG_INTERNAL_PARTS + 0].length= strlen(name) + 1;
    if (unlikely(translog_write_record(&lsn, LOGREC_REDO_DROP_TABLE,
                                       &dummy_transaction_object, NULL,
                                       (translog_size_t)
                                       log_array[TRANSLOG_INTERNAL_PARTS +
                                                 0].length,
                                       sizeof(log_array)/sizeof(log_array[0]),
                                       log_array, NULL, NULL) ||
                 translog_flush(lsn)))
      DBUG_RETURN(1);
  }

  fn_format(from,name,"",MARIA_NAME_IEXT,MY_UNPACK_FILENAME|MY_APPEND_EXT);
  if (my_delete_with_symlink(from, MYF(MY_WME | sync_dir)))
    DBUG_RETURN(my_errno);
  fn_format(from,name,"",MARIA_NAME_DEXT,MY_UNPACK_FILENAME|MY_APPEND_EXT);
#ifdef USE_RAID
  if (raid_type)
    DBUG_RETURN(my_raid_delete(from, raid_chunks, MYF(MY_WME | sync_dir)) ?
                my_errno : 0);
#endif
  DBUG_RETURN(my_delete_with_symlink(from, MYF(MY_WME | sync_dir)) ?
              my_errno : 0);
}
