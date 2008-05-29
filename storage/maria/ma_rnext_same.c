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

#include "maria_def.h"
#include "ma_rt_index.h"

/*
  Read next row with the same key as previous read, but abort if
  the key changes.
  One may have done a write, update or delete of the previous row.

  NOTE! Even if one changes the previous row, the next read is done
  based on the position of the last used key!
*/

int maria_rnext_same(MARIA_HA *info, uchar *buf)
{
  int error;
  uint inx,not_used[2];
  MARIA_KEYDEF *keyinfo;
  DBUG_ENTER("maria_rnext_same");

  if ((int) (inx= info->lastinx) < 0 ||
      info->cur_row.lastpos == HA_OFFSET_ERROR)
    DBUG_RETURN(my_errno=HA_ERR_WRONG_INDEX);
  keyinfo= info->s->keyinfo+inx;
  if (fast_ma_readinfo(info))
    DBUG_RETURN(my_errno);

  if (info->s->lock_key_trees)
    rw_rdlock(&info->s->key_root_lock[inx]);

  switch (keyinfo->key_alg) {
#ifdef HAVE_RTREE_KEYS
    case HA_KEY_ALG_RTREE:
      if ((error=maria_rtree_find_next(info,inx,
				 maria_read_vec[info->last_key_func])))
      {
	error=1;
	my_errno=HA_ERR_END_OF_FILE;
	info->cur_row.lastpos= HA_OFFSET_ERROR;
	break;
      }
      break;
#endif
    case HA_KEY_ALG_BTREE:
    default:
      if (!(info->update & HA_STATE_RNEXT_SAME))
      {
        /* First rnext_same; Store old key */
        memcpy(info->lastkey2,info->lastkey,info->last_rkey_length);
      }
      for (;;)
      {
        if ((error= _ma_search_next(info,keyinfo,info->lastkey,
                                    info->lastkey_length,SEARCH_BIGGER,
                                    info->s->state.key_root[inx])))
          break;
        if (ha_key_cmp(keyinfo->seg, (uchar*) info->lastkey,
                       (uchar*) info->lastkey2,
                       info->last_rkey_length, SEARCH_FIND, not_used))
        {
          error=1;
          my_errno=HA_ERR_END_OF_FILE;
          info->cur_row.lastpos= HA_OFFSET_ERROR;
          break;
        }
        /* Skip rows that are inserted by other threads since we got a lock */
        if (!info->s->non_transactional_concurrent_insert ||
            info->cur_row.lastpos < info->state->data_file_length)
          break;
      }
  }
  if (info->s->lock_key_trees)
    rw_unlock(&info->s->key_root_lock[inx]);
	/* Don't clear if database-changed */
  info->update&= (HA_STATE_CHANGED | HA_STATE_ROW_CHANGED);
  info->update|= HA_STATE_NEXT_FOUND | HA_STATE_RNEXT_SAME;

  if (error)
  {
    if (my_errno == HA_ERR_KEY_NOT_FOUND)
      my_errno=HA_ERR_END_OF_FILE;
  }
  else if (!buf)
  {
    DBUG_RETURN(info->cur_row.lastpos == HA_OFFSET_ERROR ? my_errno : 0);
  }
  else if (!(*info->read_record)(info, buf, info->cur_row.lastpos))
  {
    info->update|= HA_STATE_AKTIV;		/* Record is read */
    DBUG_RETURN(0);
  }
  DBUG_RETURN(my_errno);
} /* maria_rnext_same */
