/* Copyright (C) 2006 MySQL AB & Ramil Kalimullin

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

#ifdef HAVE_RTREE_KEYS
#include "ma_rt_index.h"
#include "ma_rt_key.h"
#include "ma_rt_mbr.h"

/*
  Add key to the page

  RESULT VALUES
    -1 	Error
    0 	Not split
    1	Split
*/

int maria_rtree_add_key(MARIA_HA *info, MARIA_KEYDEF *keyinfo, uchar *key,
                        uint key_length, uchar *page_buf, my_off_t *new_page)
{
  uint page_size= _ma_get_page_used(info, page_buf);
  uint nod_flag= _ma_test_if_nod(info, page_buf);
  DBUG_ENTER("maria_rtree_add_key");

  if (page_size + key_length + info->s->base.rec_reflength <=
      keyinfo->block_length)
  {
    /* split won't be necessary */
    if (nod_flag)
    {
      /* save key */
      DBUG_ASSERT(_ma_kpos(nod_flag, key) < info->state->key_file_length);
      memcpy(rt_PAGE_END(info, page_buf), key - nod_flag,
             key_length + nod_flag);
      page_size+= key_length + nod_flag;
    }
    else
    {
      /* save key */
      DBUG_ASSERT(_ma_dpos(info, nod_flag, key + key_length +
                           info->s->base.rec_reflength) <
                  info->state->data_file_length +
                  info->s->base.pack_reclength);
      memcpy(rt_PAGE_END(info, page_buf), key, key_length +
                                         info->s->base.rec_reflength);
      page_size+= key_length + info->s->base.rec_reflength;
    }
    _ma_store_page_used(info, page_buf, page_size, nod_flag);
    DBUG_RETURN(0);
  }

  DBUG_RETURN(maria_rtree_split_page(info, keyinfo, page_buf, key, key_length,
                                     new_page) ? -1 : 1);
}


/*
  Delete key from the page
*/

int maria_rtree_delete_key(MARIA_HA *info, uchar *page_buf, uchar *key,
		     uint key_length, uint nod_flag)
{
  uint16 page_size= _ma_get_page_used(info, page_buf);
  uchar *key_start;

  key_start= key - nod_flag;
  if (!nod_flag)
    key_length+= info->s->base.rec_reflength;

  memmove(key_start, key + key_length, page_size - key_length -
	  (key - page_buf));
  page_size-= key_length + nod_flag;

  _ma_store_page_used(info, page_buf, page_size, nod_flag);
  return 0;
}


/*
  Calculate and store key MBR
*/

int maria_rtree_set_key_mbr(MARIA_HA *info, MARIA_KEYDEF *keyinfo, uchar *key,
		      uint key_length, my_off_t child_page)
{
  DBUG_ENTER("maria_rtree_set_key_mbr");
  if (!_ma_fetch_keypage(info, keyinfo, child_page,
                         PAGECACHE_LOCK_LEFT_UNLOCKED,
                         DFLT_INIT_HITS, info->buff, 0, 0))
    DBUG_RETURN(-1);

  DBUG_RETURN(maria_rtree_page_mbr(info, keyinfo->seg,
                                   info->buff, key, key_length));
}

#endif /*HAVE_RTREE_KEYS*/
