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

/* Return useful base information for an open table */

#include "maria_def.h"
#ifdef	__WIN__
#include <sys/stat.h>
#endif

	/* Get position to last record */

MARIA_RECORD_POS maria_position(MARIA_HA *info)
{
  return info->cur_row.lastpos;
}


/* Get information about the table */
/* if flag == 2 one get current info (no sync from database */

int maria_status(MARIA_HA *info, register MARIA_INFO *x, uint flag)
{
  MY_STAT state;
  MARIA_SHARE *share=info->s;
  DBUG_ENTER("maria_status");

  x->recpos= info->cur_row.lastpos;
  if (flag == HA_STATUS_POS)
    DBUG_RETURN(0);				/* Compatible with ISAM */
  if (!(flag & HA_STATUS_NO_LOCK))
  {
    pthread_mutex_lock(&share->intern_lock);
    VOID(_ma_readinfo(info,F_RDLCK,0));
    fast_ma_writeinfo(info);
    pthread_mutex_unlock(&share->intern_lock);
  }
  if (flag & HA_STATUS_VARIABLE)
  {
    x->records	 	= info->state->records;
    x->deleted	 	= info->state->del;
    x->delete_length	= info->state->empty;
    x->data_file_length	=info->state->data_file_length;
    x->index_file_length=info->state->key_file_length;

    x->keys	 	= share->state.header.keys;
    x->check_time	= share->state.check_time;
    x->mean_reclength	= info->state->records ?
      (ulong) ((info->state->data_file_length-info->state->empty)/
	       info->state->records) : (ulong) share->min_pack_length;
  }
  if (flag & HA_STATUS_ERRKEY)
  {
    x->errkey=       info->errkey;
    x->dup_key_pos=  info->dup_key_pos;
  }
  if (flag & HA_STATUS_CONST)
  {
    x->reclength	= share->base.reclength;
    x->max_data_file_length=share->base.max_data_file_length;
    x->max_index_file_length=info->s->base.max_key_file_length;
    x->filenr	 = info->dfile;
    x->options	 = share->options;
    x->create_time=share->state.create_time;
    x->reflength= maria_get_pointer_length(share->base.max_data_file_length,
                                        maria_data_pointer_size);
    x->record_offset= ((share->options &
			(HA_OPTION_PACK_RECORD | HA_OPTION_COMPRESS_RECORD)) ?
		       0L : share->base.pack_reclength);
    x->sortkey= -1;				/* No clustering */
    x->rec_per_key	= share->state.rec_per_key_part;
    x->key_map	 	= share->state.key_map;
    x->data_file_name   = share->data_file_name;
    x->index_file_name  = share->index_file_name;
  }
  if ((flag & HA_STATUS_TIME) && !my_fstat(info->dfile,&state,MYF(0)))
    x->update_time=state.st_mtime;
  else
    x->update_time=0;
  if (flag & HA_STATUS_AUTO)
  {
    x->auto_increment= share->state.auto_increment+1;
    if (!x->auto_increment)			/* This shouldn't happen */
      x->auto_increment= ~(ulonglong) 0;
  }
  DBUG_RETURN(0);
}


/*
  Write a message to the error log.

  SYNOPSIS
    _ma_report_error()
    file_name                   Name of table file (e.g. index_file_name).
    errcode                     Error number.

  DESCRIPTION
    This function supplies my_error() with a table name. Most error
    messages need one. Since string arguments in error messages are limited
    to 64 characters by convention, we ensure that in case of truncation,
    that the end of the index file path is in the message. This contains
    the most valuable information (the table name and the database name).

  RETURN
    void
*/

void _ma_report_error(int errcode, const char *file_name)
{
  uint  length;
  DBUG_ENTER("_ma_report_error");
  DBUG_PRINT("enter",("errcode %d, table '%s'", errcode, file_name));

  if ((length= strlen(file_name)) > 64)
  {
    uint dir_length= dirname_length(file_name);
    file_name+= dir_length;
    if ((length-= dir_length) > 64)
      file_name+= length - 64;
  }
  my_error(errcode, MYF(ME_NOREFRESH), file_name);
  DBUG_VOID_RETURN;
}
