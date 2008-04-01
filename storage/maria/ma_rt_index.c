/* Copyright (C) 2006 MySQL AB & Ramil Kalimullin & MySQL Finland AB
   & TCX DataKonsult AB

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
#include "trnman.h"
#include "ma_key_recover.h"

#ifdef HAVE_RTREE_KEYS

#include "ma_rt_index.h"
#include "ma_rt_key.h"
#include "ma_rt_mbr.h"

#define REINSERT_BUFFER_INC 10
#define PICK_BY_AREA
/*#define PICK_BY_PERIMETER*/

typedef struct st_page_level
{
  uint level;
  my_off_t offs;
} stPageLevel;

typedef struct st_page_list
{
  ulong n_pages;
  ulong m_pages;
  stPageLevel *pages;
} stPageList;


/*
   Find next key in r-tree according to search_flag recursively

   NOTES
     Used in maria_rtree_find_first() and maria_rtree_find_next()

   RETURN
     -1	 Error
     0   Found
     1   Not found
*/

static int maria_rtree_find_req(MARIA_HA *info, MARIA_KEYDEF *keyinfo,
                                uint search_flag,
                                uint nod_cmp_flag, my_off_t page, int level)
{
  MARIA_SHARE *share= info->s;
  uint nod_flag;
  int res;
  uchar *page_buf, *k, *last;
  int k_len;
  uint *saved_key= (uint*) (info->maria_rtree_recursion_state) + level;

  if (!(page_buf= (uchar*) my_alloca((uint)keyinfo->block_length)))
  {
    my_errno= HA_ERR_OUT_OF_MEM;
    return -1;
  }
  if (!_ma_fetch_keypage(info, keyinfo, page, PAGECACHE_LOCK_LEFT_UNLOCKED,
                         DFLT_INIT_HITS, page_buf, 0, 0))
    goto err1;
  nod_flag= _ma_test_if_nod(share, page_buf);

  k_len= keyinfo->keylength - share->base.rec_reflength;

  if (info->maria_rtree_recursion_depth >= level)
  {
    k= page_buf + *saved_key;
  }
  else
  {
    k= rt_PAGE_FIRST_KEY(share, page_buf, nod_flag);
  }
  last= rt_PAGE_END(share, page_buf);

  for (; k < last; k= rt_PAGE_NEXT_KEY(share, k, k_len, nod_flag))
  {
    if (nod_flag)
    {
      /* this is an internal node in the tree */
      if (!(res= maria_rtree_key_cmp(keyinfo->seg,
                                      info->first_mbr_key, k,
                                      info->last_rkey_length, nod_cmp_flag)))
      {
        switch ((res= maria_rtree_find_req(info, keyinfo, search_flag,
                                            nod_cmp_flag,
                                            _ma_kpos(nod_flag, k),
                                            level + 1)))
        {
          case 0: /* found - exit from recursion */
            *saved_key= k - page_buf;
            goto ok;
          case 1: /* not found - continue searching */
            info->maria_rtree_recursion_depth= level;
            break;
          default: /* error */
          case -1:
            goto err1;
        }
      }
    }
    else
    {
      /* this is a leaf */
      if (!maria_rtree_key_cmp(keyinfo->seg, info->first_mbr_key,
                               k, info->last_rkey_length, search_flag))
      {
        uchar *after_key= (uchar*) rt_PAGE_NEXT_KEY(share, k, k_len, nod_flag);
        info->cur_row.lastpos= _ma_dpos(info, 0, after_key);
        info->lastkey_length= k_len + share->base.rec_reflength;
        memcpy(info->lastkey, k, info->lastkey_length);
        info->maria_rtree_recursion_depth= level;
        *saved_key= last - page_buf;

        if (after_key < last)
        {
          info->int_keypos= info->buff;
          info->int_maxpos= info->buff + (last - after_key);
          memcpy(info->buff, after_key, last - after_key);
          info->keyread_buff_used= 0;
        }
        else
        {
	  info->keyread_buff_used= 1;
        }

        res= 0;
        goto ok;
      }
    }
  }
  info->cur_row.lastpos= HA_OFFSET_ERROR;
  my_errno= HA_ERR_KEY_NOT_FOUND;
  res= 1;

ok:
  my_afree((uchar*)page_buf);
  return res;

err1:
  my_afree((uchar*)page_buf);
  info->cur_row.lastpos= HA_OFFSET_ERROR;
  return -1;
}


/*
  Find first key in r-tree according to search_flag condition

  SYNOPSIS
   maria_rtree_find_first()
   info			Handler to MARIA file
   uint keynr		Key number to use
   key			Key to search for
   key_length		Length of 'key'
   search_flag		Bitmap of flags how to do the search

  RETURN
    -1  Error
    0   Found
    1   Not found
*/

int maria_rtree_find_first(MARIA_HA *info, uint keynr, uchar *key,
                           uint key_length, uint search_flag)
{
  my_off_t root;
  uint nod_cmp_flag;
  MARIA_KEYDEF *keyinfo= info->s->keyinfo + keynr;

  if ((root= info->s->state.key_root[keynr]) == HA_OFFSET_ERROR)
  {
    my_errno= HA_ERR_END_OF_FILE;
    return -1;
  }

  /*
    Save searched key, include data pointer.
    The data pointer is required if the search_flag contains MBR_DATA.
    (minimum bounding rectangle)
  */
  memcpy(info->first_mbr_key, key, keyinfo->keylength);
  info->last_rkey_length= key_length;

  info->maria_rtree_recursion_depth= -1;
  info->keyread_buff_used= 1;

  nod_cmp_flag= ((search_flag & (MBR_EQUAL | MBR_WITHIN)) ?
                 MBR_WITHIN : MBR_INTERSECT);
  return maria_rtree_find_req(info, keyinfo, search_flag, nod_cmp_flag, root,
                              0);
}


/*
   Find next key in r-tree according to search_flag condition

  SYNOPSIS
   maria_rtree_find_next()
   info			Handler to MARIA file
   uint keynr		Key number to use
   search_flag		Bitmap of flags how to do the search

   RETURN
     -1  Error
     0   Found
     1   Not found
*/

int maria_rtree_find_next(MARIA_HA *info, uint keynr, uint search_flag)
{
  my_off_t root;
  uint nod_cmp_flag;
  MARIA_KEYDEF *keyinfo= info->s->keyinfo + keynr;

  if (info->update & HA_STATE_DELETED)
    return maria_rtree_find_first(info, keynr, info->lastkey,
                                  info->lastkey_length,
                                  search_flag);

  if (!info->keyread_buff_used)
  {
    uchar *key= info->int_keypos;

    while (key < info->int_maxpos)
    {
      if (!maria_rtree_key_cmp(keyinfo->seg,
                               info->first_mbr_key, key,
                               info->last_rkey_length, search_flag))
      {
        uchar *after_key= key + keyinfo->keylength;

        info->cur_row.lastpos= _ma_dpos(info, 0, after_key);
        memcpy(info->lastkey, key, info->lastkey_length);

        if (after_key < info->int_maxpos)
	  info->int_keypos= after_key;
        else
	  info->keyread_buff_used= 1;
        return 0;
      }
      key+= keyinfo->keylength;
    }
  }
  if ((root= info->s->state.key_root[keynr]) == HA_OFFSET_ERROR)
  {
    my_errno= HA_ERR_END_OF_FILE;
    return -1;
  }

  nod_cmp_flag= ((search_flag & (MBR_EQUAL | MBR_WITHIN)) ?
        MBR_WITHIN : MBR_INTERSECT);
  return maria_rtree_find_req(info, keyinfo, search_flag, nod_cmp_flag, root, 0);
}


/*
  Get next key in r-tree recursively

  NOTES
    Used in maria_rtree_get_first() and maria_rtree_get_next()

  RETURN
    -1  Error
    0   Found
    1   Not found
*/

static int maria_rtree_get_req(MARIA_HA *info, MARIA_KEYDEF *keyinfo,
                               uint key_length, my_off_t page, int level)
{
  MARIA_SHARE *share= info->s;
  uchar *page_buf, *last, *k;
  uint nod_flag, k_len;
  int res;
  uint *saved_key= (uint*) (info->maria_rtree_recursion_state) + level;

  if (!(page_buf= (uchar*) my_alloca((uint)keyinfo->block_length)))
    return -1;
  if (!_ma_fetch_keypage(info, keyinfo, page, PAGECACHE_LOCK_LEFT_UNLOCKED,
                         DFLT_INIT_HITS, page_buf, 0, 0))
    goto err1;
  nod_flag= _ma_test_if_nod(share, page_buf);

  k_len= keyinfo->keylength - share->base.rec_reflength;

  if(info->maria_rtree_recursion_depth >= level)
  {
    k= page_buf + *saved_key;
    if (!nod_flag)
    {
      /* Only leaf pages contain data references. */
      /* Need to check next key with data reference. */
      k= rt_PAGE_NEXT_KEY(share, k, k_len, nod_flag);
    }
  }
  else
  {
    k= rt_PAGE_FIRST_KEY(share, page_buf, nod_flag);
  }
  last= rt_PAGE_END(share, page_buf);

  for (; k < last; k= rt_PAGE_NEXT_KEY(share, k, k_len, nod_flag))
  {
    if (nod_flag)
    {
      /* this is an internal node in the tree */
      switch ((res= maria_rtree_get_req(info, keyinfo, key_length,
                                         _ma_kpos(nod_flag, k), level + 1)))
      {
        case 0: /* found - exit from recursion */
          *saved_key= k - page_buf;
          goto ok;
        case 1: /* not found - continue searching */
          info->maria_rtree_recursion_depth= level;
          break;
        default:
        case -1: /* error */
          goto err1;
      }
    }
    else
    {
      /* this is a leaf */
      uchar *after_key= rt_PAGE_NEXT_KEY(share, k, k_len, nod_flag);
      info->cur_row.lastpos= _ma_dpos(info, 0, after_key);
      info->lastkey_length= k_len + share->base.rec_reflength;
      memcpy(info->lastkey, k, info->lastkey_length);

      info->maria_rtree_recursion_depth= level;
      *saved_key= k - page_buf;

      if (after_key < last)
      {
        info->int_keypos= (uchar*) saved_key;
        memcpy(info->buff, page_buf, keyinfo->block_length);
        info->int_maxpos= rt_PAGE_END(share, info->buff);
        info->keyread_buff_used= 0;
      }
      else
      {
	info->keyread_buff_used= 1;
      }

      res= 0;
      goto ok;
    }
  }
  info->cur_row.lastpos= HA_OFFSET_ERROR;
  my_errno= HA_ERR_KEY_NOT_FOUND;
  res= 1;

ok:
  my_afree((uchar*)page_buf);
  return res;

err1:
  my_afree((uchar*)page_buf);
  info->cur_row.lastpos= HA_OFFSET_ERROR;
  return -1;
}


/*
  Get first key in r-tree

  RETURN
    -1	Error
    0	Found
    1	Not found
*/

int maria_rtree_get_first(MARIA_HA *info, uint keynr, uint key_length)
{
  my_off_t root;
  MARIA_KEYDEF *keyinfo= info->s->keyinfo + keynr;

  if ((root= info->s->state.key_root[keynr]) == HA_OFFSET_ERROR)
  {
    my_errno= HA_ERR_END_OF_FILE;
    return -1;
  }

  info->maria_rtree_recursion_depth= -1;
  info->keyread_buff_used= 1;

  return maria_rtree_get_req(info, &keyinfo[keynr], key_length, root, 0);
}


/*
  Get next key in r-tree

  RETURN
    -1	Error
    0	Found
    1	Not found
*/

int maria_rtree_get_next(MARIA_HA *info, uint keynr, uint key_length)
{
  my_off_t root;
  MARIA_KEYDEF *keyinfo= info->s->keyinfo + keynr;

  if (!info->keyread_buff_used)
  {
    uint k_len= keyinfo->keylength - info->s->base.rec_reflength;
    /* rt_PAGE_NEXT_KEY(info->int_keypos) */
    uchar *key= info->buff + *(int*)info->int_keypos + k_len +
                 info->s->base.rec_reflength;
    /* rt_PAGE_NEXT_KEY(key) */
    uchar *after_key= key + k_len + info->s->base.rec_reflength;

    info->cur_row.lastpos= _ma_dpos(info, 0, after_key);
    info->lastkey_length= k_len + info->s->base.rec_reflength;
    memcpy(info->lastkey, key, k_len + info->s->base.rec_reflength);

    *(int*)info->int_keypos= key - info->buff;
    if (after_key >= info->int_maxpos)
    {
      info->keyread_buff_used= 1;
    }

    return 0;
  }
  else
  {
    if ((root= info->s->state.key_root[keynr]) == HA_OFFSET_ERROR)
    {
      my_errno= HA_ERR_END_OF_FILE;
      return -1;
    }

    return maria_rtree_get_req(info, &keyinfo[keynr], key_length, root, 0);
  }
}


/*
  Choose non-leaf better key for insertion
*/

#ifdef PICK_BY_PERIMETER
static uchar *maria_rtree_pick_key(MARIA_HA *info, MARIA_KEYDEF *keyinfo,
                                   uchar *key,
                                   uint key_length, uchar *page_buf,
                                   uint nod_flag)
{
  double increase;
  double best_incr;
  double perimeter;
  double best_perimeter;
  uchar *best_key= NULL;
  uchar *k= rt_PAGE_FIRST_KEY(page_buf, nod_flag);
  uchar *last= rt_PAGE_END(info, page_buf);

  LINT_INIT(best_perimeter);
  LINT_INIT(best_key);
  LINT_INIT(best_incr);

  for (; k < last; k= rt_PAGE_NEXT_KEY(k, key_length, nod_flag))
  {
    if ((increase= maria_rtree_perimeter_increase(keyinfo->seg, k, key, key_length,
					     &perimeter)) == -1)
      return NULL;
    if ((increase < best_incr)||
	(increase == best_incr && perimeter < best_perimeter))
    {
      best_key= k;
      best_perimeter= perimeter;
      best_incr= increase;
    }
  }
  return best_key;
}

#endif /*PICK_BY_PERIMETER*/

#ifdef PICK_BY_AREA
static uchar *maria_rtree_pick_key(MARIA_HA *info, MARIA_KEYDEF *keyinfo,
                                  uchar *key,
                                  uint key_length, uchar *page_buf,
                                  uint nod_flag)
{
  MARIA_SHARE *share= info->s;
  double increase;
  double best_incr= DBL_MAX;
  double area;
  double best_area;
  uchar *best_key;
  uchar *k= rt_PAGE_FIRST_KEY(share, page_buf, nod_flag);
  uchar *last= rt_PAGE_END(share, page_buf);

  LINT_INIT(best_area);
  LINT_INIT(best_key);

  for (; k < last; k= rt_PAGE_NEXT_KEY(share, k, key_length, nod_flag))
  {
    /* The following is safe as -1.0 is an exact number */
    if ((increase= maria_rtree_area_increase(keyinfo->seg, k, key, key_length,
                                              &area)) == -1.0)
      return NULL;
    /* The following should be safe, even if we compare doubles */
    if (!best_key || increase < best_incr ||
        ((increase == best_incr) && (area < best_area)))
    {
      best_key= k;
      best_area= area;
      best_incr= increase;
    }
  }
  return best_key;
}

#endif /*PICK_BY_AREA*/

/*
  Go down and insert key into tree

  RETURN
    -1	Error
    0	Child was not split
    1	Child was split
*/

static int maria_rtree_insert_req(MARIA_HA *info, MARIA_KEYDEF *keyinfo,
                                  uchar *key,
                                  uint key_length, my_off_t page,
                                  my_off_t *new_page,
                                  int ins_level, int level)
{
  uint nod_flag;
  int res;
  uchar *page_buf, *k;
  MARIA_PINNED_PAGE *page_link;
  DBUG_ENTER("maria_rtree_insert_req");

  if (!(page_buf= (uchar*) my_alloca((uint)keyinfo->block_length +
                                     HA_MAX_KEY_BUFF)))
  {
    my_errno= HA_ERR_OUT_OF_MEM;
    DBUG_RETURN(-1); /* purecov: inspected */
  }
  if (!_ma_fetch_keypage(info, keyinfo, page, PAGECACHE_LOCK_WRITE,
                         DFLT_INIT_HITS, page_buf, 0, &page_link))
    goto err1;
  nod_flag= _ma_test_if_nod(info->s, page_buf);
  DBUG_PRINT("rtree", ("page: %lu  level: %d  ins_level: %d  nod_flag: %u",
                       (ulong) page, level, ins_level, nod_flag));

  if ((ins_level == -1 && nod_flag) ||       /* key: go down to leaf */
      (ins_level > -1 && ins_level > level)) /* branch: go down to ins_level */
  {
    if ((k= maria_rtree_pick_key(info, keyinfo, key, key_length, page_buf,
                                  nod_flag)) == NULL)
      goto err1;
    switch ((res= maria_rtree_insert_req(info, keyinfo, key, key_length,
                                          _ma_kpos(nod_flag, k), new_page,
                                          ins_level, level + 1)))
    {
      case 0: /* child was not split */
      {
        maria_rtree_combine_rect(keyinfo->seg, k, key, k, key_length);
        page_link->changed= 1;
        if (_ma_write_keypage(info, keyinfo, page,
                              PAGECACHE_LOCK_LEFT_WRITELOCKED,
                              DFLT_INIT_HITS, page_buf))
          goto err1;
        goto ok;
      }
      case 1: /* child was split */
      {
        uchar *new_key= page_buf + keyinfo->block_length + nod_flag;
        /* set proper MBR for key */
        if (maria_rtree_set_key_mbr(info, keyinfo, k, key_length,
                                    _ma_kpos(nod_flag, k)))
          goto err1;
        /* add new key for new page */
        _ma_kpointer(info, new_key - nod_flag, *new_page);
        if (maria_rtree_set_key_mbr(info, keyinfo, new_key, key_length,
                                    *new_page))
          goto err1;
        res= maria_rtree_add_key(info, keyinfo, new_key, key_length,
                                 page_buf, new_page);
        page_link->changed= 1;
        if (_ma_write_keypage(info, keyinfo, page,
                              PAGECACHE_LOCK_LEFT_WRITELOCKED,
                              DFLT_INIT_HITS, page_buf))
          goto err1;
        goto ok;
      }
      default:
      case -1: /* error */
      {
        goto err1;
      }
    }
  }
  else
  {
    res= maria_rtree_add_key(info, keyinfo, key, key_length, page_buf,
                              new_page);
    page_link->changed= 1;
    if (_ma_write_keypage(info, keyinfo, page, PAGECACHE_LOCK_LEFT_WRITELOCKED,
                          DFLT_INIT_HITS, page_buf))
      goto err1;
  }

ok:
  my_afree(page_buf);
  DBUG_RETURN(res);

err1:
  my_afree(page_buf);
  DBUG_RETURN(-1); /* purecov: inspected */
}


/*
  Insert key into the tree

  RETURN
    -1	Error
    0	Root was not split
    1	Root was split
*/

static int maria_rtree_insert_level(MARIA_HA *info, uint keynr, uchar *key,
                                    uint key_length, int ins_level)
{
  my_off_t old_root;
  MARIA_SHARE *share= info->s;
  MARIA_KEYDEF *keyinfo= share->keyinfo + keynr;
  int res;
  my_off_t new_page;
  MARIA_PINNED_PAGE *page_link;
  DBUG_ENTER("maria_rtree_insert_level");

  if ((old_root= share->state.key_root[keynr]) == HA_OFFSET_ERROR)
  {
    MARIA_PINNED_PAGE tmp_page_link;
    page_link= &tmp_page_link;
    if ((old_root= _ma_new(info, DFLT_INIT_HITS, &page_link)) ==
        HA_OFFSET_ERROR)
      DBUG_RETURN(-1);
    info->keyread_buff_used= 1;
    bzero(info->buff, share->block_size);
    _ma_store_keynr(share, info->buff, keynr);
    _ma_store_page_used(share, info->buff, share->keypage_header);

    res= maria_rtree_add_key(info, keyinfo, key, key_length, info->buff,
                              NULL);
    if (_ma_write_keypage(info, keyinfo, old_root,
                          page_link->write_lock,
                          DFLT_INIT_HITS, info->buff))
      DBUG_RETURN(1);
    share->state.key_root[keynr]= old_root;
    DBUG_RETURN(res);
  }

  switch ((res= maria_rtree_insert_req(info, keyinfo, key, key_length,
                                       old_root, &new_page, ins_level, 0)))
  {
    case 0: /* root was not split */
    {
      break;
    }
    case 1: /* root was split, grow a new root */
    {
      uchar *new_root_buf, *new_key;
      my_off_t new_root;
      uint nod_flag= share->base.key_reflength;
      MARIA_PINNED_PAGE tmp_page_link;
      page_link= &tmp_page_link;

      DBUG_PRINT("rtree", ("root was split, grow a new root"));
      if (!(new_root_buf= (uchar*) my_alloca((uint)keyinfo->block_length +
                                            HA_MAX_KEY_BUFF)))
      {
        my_errno= HA_ERR_OUT_OF_MEM;
        DBUG_RETURN(-1); /* purecov: inspected */
      }

      bzero(new_root_buf, share->block_size);
      if (nod_flag)
        _ma_store_keypage_flag(share, new_root_buf, KEYPAGE_FLAG_ISNOD);
      _ma_store_keynr(share, new_root_buf, keynr);
      _ma_store_page_used(share, new_root_buf, share->keypage_header);
      if ((new_root= _ma_new(info, DFLT_INIT_HITS, &page_link)) ==
	  HA_OFFSET_ERROR)
        goto err1;

      new_key= new_root_buf + keyinfo->block_length + nod_flag;

      _ma_kpointer(info, new_key - nod_flag, old_root);
      if (maria_rtree_set_key_mbr(info, keyinfo, new_key, key_length,
                                  old_root))
        goto err1;
      if (maria_rtree_add_key(info, keyinfo, new_key, key_length, new_root_buf,
                              NULL)
          == -1)
        goto err1;
      _ma_kpointer(info, new_key - nod_flag, new_page);
      if (maria_rtree_set_key_mbr(info, keyinfo, new_key, key_length,
                                  new_page))
        goto err1;
      if (maria_rtree_add_key(info, keyinfo, new_key, key_length, new_root_buf,
                              NULL)
          == -1)
        goto err1;
      if (_ma_write_keypage(info, keyinfo, new_root, page_link->write_lock,
                            DFLT_INIT_HITS, new_root_buf))
        goto err1;
      share->state.key_root[keynr]= new_root;
      DBUG_PRINT("rtree", ("new root page: %lu  level: %d  nod_flag: %u",
                           (ulong) new_root, 0,
                           _ma_test_if_nod(share, new_root_buf)));

      my_afree((uchar*)new_root_buf);
      break;
err1:
      my_afree((uchar*)new_root_buf);
      DBUG_RETURN(-1); /* purecov: inspected */
    }
    default:
    case -1: /* error */
    {
      break;
    }
  }
  DBUG_RETURN(res);
}


/*
  Insert key into the tree - interface function

  RETURN
    -1	Error
    0	OK
*/

int maria_rtree_insert(MARIA_HA *info, uint keynr, uchar *key, uint key_length)
{
  int res;
  DBUG_ENTER("maria_rtree_insert");
  res= - (!key_length ||
          (maria_rtree_insert_level(info, keynr, key, key_length, -1) == -1));
  _ma_fast_unlock_key_del(info);
  /** @todo RECOVERY use a real LSN */
  _ma_unpin_all_pages_and_finalize_row(info, LSN_IMPOSSIBLE);
  DBUG_RETURN(res);
}


/*
  Fill reinsert page buffer

  RETURN
    -1	Error
    0	OK
*/

static int maria_rtree_fill_reinsert_list(stPageList *ReinsertList, my_off_t page,
                                    int level)
{
  DBUG_ENTER("maria_rtree_fill_reinsert_list");
  DBUG_PRINT("rtree", ("page: %lu  level: %d", (ulong) page, level));
  if (ReinsertList->n_pages == ReinsertList->m_pages)
  {
    ReinsertList->m_pages += REINSERT_BUFFER_INC;
    if (!(ReinsertList->pages= (stPageLevel*)my_realloc((uchar*)ReinsertList->pages,
      ReinsertList->m_pages * sizeof(stPageLevel), MYF(MY_ALLOW_ZERO_PTR))))
      goto err1;
  }
  /* save page to ReinsertList */
  ReinsertList->pages[ReinsertList->n_pages].offs= page;
  ReinsertList->pages[ReinsertList->n_pages].level= level;
  ReinsertList->n_pages++;
  DBUG_RETURN(0);

err1:
  DBUG_RETURN(-1); /* purecov: inspected */
}


/*
  Go down and delete key from the tree

  RETURN
    -1	Error
    0	Deleted
    1	Not found
    2	Empty leaf
*/

static int maria_rtree_delete_req(MARIA_HA *info, MARIA_KEYDEF *keyinfo,
                                  uchar *key,
                                  uint key_length, my_off_t page,
                                  uint *page_size,
                                  stPageList *ReinsertList, int level)
{
  ulong i;
  uint nod_flag;
  int res;
  uchar *page_buf, *last, *k;
  MARIA_PINNED_PAGE *page_link;
  MARIA_SHARE *share= info->s;
  DBUG_ENTER("maria_rtree_delete_req");

  if (!(page_buf= (uchar*) my_alloca((uint)keyinfo->block_length)))
  {
    my_errno= HA_ERR_OUT_OF_MEM;
    DBUG_RETURN(-1); /* purecov: inspected */
  }
  if (!_ma_fetch_keypage(info, keyinfo, page, PAGECACHE_LOCK_WRITE,
                         DFLT_INIT_HITS, page_buf, 0, &page_link))
    goto err1;
  nod_flag= _ma_test_if_nod(share, page_buf);
  DBUG_PRINT("rtree", ("page: %lu  level: %d  nod_flag: %u",
                       (ulong) page, level, nod_flag));

  k= rt_PAGE_FIRST_KEY(share, page_buf, nod_flag);
  last= rt_PAGE_END(share, page_buf);

  for (i= 0;
       k < last;
       k= rt_PAGE_NEXT_KEY(share, k, key_length, nod_flag), i++)
  {
    if (nod_flag)
    {
      /* not leaf */
      if (!maria_rtree_key_cmp(keyinfo->seg, key, k, key_length, MBR_WITHIN))
      {
        switch ((res= maria_rtree_delete_req(info, keyinfo, key, key_length,
                  _ma_kpos(nod_flag, k), page_size, ReinsertList, level + 1)))
        {
          case 0: /* deleted */
          {
            /* test page filling */
            if (*page_size + key_length >=
                rt_PAGE_MIN_SIZE(keyinfo->block_length))
            {
              /* OK */
              /* Calculate a new key value (MBR) for the shrinked block. */
              if (maria_rtree_set_key_mbr(info, keyinfo, k, key_length,
                                  _ma_kpos(nod_flag, k)))
                goto err1;
              page_link->changed= 1;
              if (_ma_write_keypage(info, keyinfo, page,
                                    PAGECACHE_LOCK_LEFT_WRITELOCKED,
                                    DFLT_INIT_HITS, page_buf))
                goto err1;
            }
            else
            {
              /*
                Too small: delete key & add it descendant to reinsert list.
                Store position and level of the block so that it can be
                accessed later for inserting the remaining keys.
              */
              DBUG_PRINT("rtree", ("too small. move block to reinsert list"));
              if (maria_rtree_fill_reinsert_list(ReinsertList,
                                                 _ma_kpos(nod_flag, k),
                                           level + 1))
                goto err1;
              /*
                Delete the key that references the block. This makes the
                block disappear from the index. Hence we need to insert
                its remaining keys later. Note: if the block is a branch
                block, we do not only remove this block, but the whole
                subtree. So we need to re-insert its keys on the same
                level later to reintegrate the subtrees.
              */
              maria_rtree_delete_key(info, page_buf, k, key_length, nod_flag);
              page_link->changed= 1;
              if (_ma_write_keypage(info, keyinfo, page,
                                    PAGECACHE_LOCK_LEFT_WRITELOCKED,
                                    DFLT_INIT_HITS, page_buf))
                goto err1;
              *page_size= _ma_get_page_used(share, page_buf);
            }

            goto ok;
          }
          case 1: /* not found - continue searching */
          {
            break;
          }
          case 2: /* vacuous case: last key in the leaf */
          {
            maria_rtree_delete_key(info, page_buf, k, key_length, nod_flag);
            page_link->changed= 1;
            if (_ma_write_keypage(info, keyinfo, page,
                                  PAGECACHE_LOCK_LEFT_WRITELOCKED,
                                  DFLT_INIT_HITS, page_buf))
              goto err1;
            *page_size= _ma_get_page_used(share, page_buf);
            res= 0;
            goto ok;
          }
          default: /* error */
          case -1:
          {
            goto err1;
          }
        }
      }
    }
    else
    {
      /* leaf */
      if (!maria_rtree_key_cmp(keyinfo->seg, key, k, key_length,
                               MBR_EQUAL | MBR_DATA))
      {
        page_link->changed= 1;

        maria_rtree_delete_key(info, page_buf, k, key_length, nod_flag);
        *page_size= _ma_get_page_used(share, page_buf);
        if (*page_size == info->s->keypage_header)
        {
          /* last key in the leaf */
          res= 2;
          if (_ma_dispose(info, page, 0))
            goto err1;
        }
        else
        {
          res= 0;
          if (_ma_write_keypage(info, keyinfo, page,
                                PAGECACHE_LOCK_LEFT_WRITELOCKED,
                                DFLT_INIT_HITS, page_buf))
            goto err1;
        }
        goto ok;
      }
    }
  }
  res= 1;

ok:
  my_afree((uchar*)page_buf);
  DBUG_RETURN(res);

err1:
  my_afree((uchar*)page_buf);
  DBUG_RETURN(-1); /* purecov: inspected */
}


/*
  Delete key - interface function

  RETURN
    -1	Error
    0	Deleted
*/

int maria_rtree_delete(MARIA_HA *info, uint keynr, uchar *key, uint key_length)
{
  MARIA_SHARE *share= info->s;
  uint page_size;
  stPageList ReinsertList;
  my_off_t old_root;
  MARIA_KEYDEF *keyinfo= info->s->keyinfo + keynr;
  MARIA_PINNED_PAGE *page_link, *root_page_link;
  int res;
  DBUG_ENTER("maria_rtree_delete");

  if ((old_root= share->state.key_root[keynr]) == HA_OFFSET_ERROR)
  {
    my_errno= HA_ERR_END_OF_FILE;
    DBUG_RETURN(-1); /* purecov: inspected */
  }
  DBUG_PRINT("rtree", ("starting deletion at root page: %lu",
                       (ulong) old_root));

  ReinsertList.pages= NULL;
  ReinsertList.n_pages= 0;
  ReinsertList.m_pages= 0;

  switch (maria_rtree_delete_req(info, keyinfo, key, key_length, old_root,
                                 &page_size, &ReinsertList, 0)) {
  case 2: /* empty */
  {
    share->state.key_root[keynr]= HA_OFFSET_ERROR;
    res= 0;
    goto err;
  }
  case 0: /* deleted */
  {
    uint nod_flag;
    ulong i;
    for (i= 0; i < ReinsertList.n_pages; ++i)
    {
      uchar *page_buf, *k, *last;

      if (!(page_buf= (uchar*) my_alloca((uint)keyinfo->block_length)))
      {
        my_errno= HA_ERR_OUT_OF_MEM;
        goto err1;
      }
      if (!_ma_fetch_keypage(info, keyinfo, ReinsertList.pages[i].offs,
                             PAGECACHE_LOCK_WRITE,
                             DFLT_INIT_HITS, page_buf, 0, &page_link))
        goto err1;
      nod_flag= _ma_test_if_nod(share, page_buf);
      DBUG_PRINT("rtree", ("reinserting keys from "
                           "page: %lu  level: %d  nod_flag: %u",
                           (ulong) ReinsertList.pages[i].offs,
                           ReinsertList.pages[i].level, nod_flag));

      k= rt_PAGE_FIRST_KEY(share, page_buf, nod_flag);
      last= rt_PAGE_END(share, page_buf);
      for (; k < last; k= rt_PAGE_NEXT_KEY(share, k, key_length, nod_flag))
      {
        if ((res=
             maria_rtree_insert_level(info, keynr, k, key_length,
                                      ReinsertList.pages[i].level)) == -1)
        {
          my_afree(page_buf);
          goto err1;
        }
        if (res)
        {
          ulong j;
          DBUG_PRINT("rtree", ("root has been split, adjust levels"));
          for (j= i; j < ReinsertList.n_pages; j++)
          {
            ReinsertList.pages[j].level++;
            DBUG_PRINT("rtree", ("keys from page: %lu  now level: %d",
                                 (ulong) ReinsertList.pages[i].offs,
                                 ReinsertList.pages[i].level));
          }
        }
      }
      res= 0;
      my_afree(page_buf);
      page_link->changed= 1;
      if (_ma_dispose(info, ReinsertList.pages[i].offs, 0))
        goto err1;
    }
    if (ReinsertList.pages)
      my_free((uchar*) ReinsertList.pages, MYF(0));

    /* check for redundant root (not leaf, 1 child) and eliminate */
    if ((old_root= share->state.key_root[keynr]) == HA_OFFSET_ERROR)
      goto err1;
    if (!_ma_fetch_keypage(info, keyinfo, old_root,
                           PAGECACHE_LOCK_WRITE,
                           DFLT_INIT_HITS, info->buff, 0, &root_page_link))
      goto err1;
    nod_flag= _ma_test_if_nod(share, info->buff);
    page_size= _ma_get_page_used(share, info->buff);
    if (nod_flag && (page_size == share->keypage_header + key_length +
                     nod_flag))
    {
      my_off_t new_root= _ma_kpos(nod_flag,
                                  rt_PAGE_FIRST_KEY(share, info->buff,
                                                    nod_flag));
      root_page_link->changed= 1;
      if (_ma_dispose(info, old_root, 0))
        goto err1;
      share->state.key_root[keynr]= new_root;
    }
    info->update= HA_STATE_DELETED;
    res= 0;
    goto err;

err1:
    res= -1;
    goto err; /* purecov: inspected */
  }
  case 1:                                     /* not found */
  {
    my_errno= HA_ERR_KEY_NOT_FOUND;
    res= -1;
    goto err; /* purecov: inspected */
  }
  default:
  case -1:                                    /* error */
    res= -1;
    goto err; /* purecov: inspected */
  }
err:
  _ma_fast_unlock_key_del(info);
  /** @todo RECOVERY use a real LSN */
  _ma_unpin_all_pages_and_finalize_row(info, LSN_IMPOSSIBLE);
  DBUG_RETURN(res);
}


/*
  Estimate number of suitable keys in the tree

  RETURN
    estimated value
*/

ha_rows maria_rtree_estimate(MARIA_HA *info, uint keynr, uchar *key,
                       uint key_length, uint flag)
{
  MARIA_SHARE *share= info->s;
  MARIA_KEYDEF *keyinfo= share->keyinfo + keynr;
  my_off_t root;
  uint i= 0;
  uint nod_flag, k_len;
  uchar *page_buf, *k, *last;
  double area= 0;
  ha_rows res= 0;

  if (flag & MBR_DISJOINT)
    return info->state->records;

  if ((root= share->state.key_root[keynr]) == HA_OFFSET_ERROR)
    return HA_POS_ERROR;
  if (!(page_buf= (uchar*) my_alloca((uint)keyinfo->block_length)))
    return HA_POS_ERROR;
  if (!_ma_fetch_keypage(info, keyinfo, root, PAGECACHE_LOCK_LEFT_UNLOCKED,
                         DFLT_INIT_HITS, page_buf, 0, 0))
    goto err1;
  nod_flag= _ma_test_if_nod(share, page_buf);

  k_len= keyinfo->keylength - share->base.rec_reflength;

  k= rt_PAGE_FIRST_KEY(share, page_buf, nod_flag);
  last= rt_PAGE_END(share, page_buf);

  for (; k < last; k= rt_PAGE_NEXT_KEY(share, k, k_len, nod_flag), i++)
  {
    if (nod_flag)
    {
      double k_area= maria_rtree_rect_volume(keyinfo->seg, k, key_length);

      /* The following should be safe, even if we compare doubles */
      if (k_area == 0)
      {
        if (flag & (MBR_CONTAIN | MBR_INTERSECT))
        {
          area += 1;
        }
        else if (flag & (MBR_WITHIN | MBR_EQUAL))
        {
          if (!maria_rtree_key_cmp(keyinfo->seg, key, k, key_length,
                                   MBR_WITHIN))
            area += 1;
        }
        else
          goto err1;
      }
      else
      {
        if (flag & (MBR_CONTAIN | MBR_INTERSECT))
        {
          area+= maria_rtree_overlapping_area(keyinfo->seg, key, k,
                                              key_length) / k_area;
        }
        else if (flag & (MBR_WITHIN | MBR_EQUAL))
        {
          if (!maria_rtree_key_cmp(keyinfo->seg, key, k, key_length,
                                   MBR_WITHIN))
            area+= (maria_rtree_rect_volume(keyinfo->seg, key, key_length) /
                    k_area);
        }
        else
          goto err1;
      }
    }
    else
    {
      if (!maria_rtree_key_cmp(keyinfo->seg, key, k, key_length, flag))
        ++res;
    }
  }
  if (nod_flag)
  {
    if (i)
      res= (ha_rows) (area / i * info->state->records);
    else
      res= HA_POS_ERROR;
  }

  my_afree((uchar*)page_buf);
  return res;

err1:
  my_afree(page_buf);
  return HA_POS_ERROR;
}

#endif /*HAVE_RTREE_KEYS*/
