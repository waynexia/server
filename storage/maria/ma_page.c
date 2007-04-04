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

/* Read and write key blocks */

#include "maria_def.h"

	/* Fetch a key-page in memory */

byte *_ma_fetch_keypage(register MARIA_HA *info, MARIA_KEYDEF *keyinfo,
                        my_off_t page, int level,
                        byte *buff, int return_buffer)
{
  byte *tmp;
  uint page_size;
  DBUG_ENTER("_ma_fetch_keypage");
  DBUG_PRINT("enter",("page: %ld", (long) page));

  DBUG_ASSERT(info->s->pagecache->block_size == keyinfo->block_length &&
              info->s->pagecache->block_size == info->s->block_size);
  /*
    TODO: replace PAGECACHE_PLAIN_PAGE with PAGECACHE_LSN_PAGE when
    LSN on the pages will be implemented
  */
  tmp= pagecache_read(info->s->pagecache, &info->s->kfile,
                      page / keyinfo->block_length, level, buff,
                      PAGECACHE_PLAIN_PAGE, PAGECACHE_LOCK_LEFT_UNLOCKED, 0);
  if (tmp == info->buff)
    info->keybuff_used=1;
  else if (!tmp)
  {
    DBUG_PRINT("error",("Got errno: %d from pagecache_read",my_errno));
    info->last_keypage=HA_OFFSET_ERROR;
    maria_print_error(info->s, HA_ERR_CRASHED);
    my_errno=HA_ERR_CRASHED;
    DBUG_RETURN(0);
  }
  info->last_keypage=page;
  page_size=maria_getint(tmp);
  if (page_size < 4 || page_size > keyinfo->block_length)
  {
    DBUG_PRINT("error",("page %lu had wrong page length: %u",
			(ulong) page, page_size));
    DBUG_DUMP("page", (char*) tmp, keyinfo->block_length);
    info->last_keypage = HA_OFFSET_ERROR;
    maria_print_error(info->s, HA_ERR_CRASHED);
    my_errno= HA_ERR_CRASHED;
    tmp= 0;
  }
  DBUG_RETURN(tmp);
} /* _ma_fetch_keypage */


	/* Write a key-page on disk */

int _ma_write_keypage(register MARIA_HA *info, register MARIA_KEYDEF *keyinfo,
		      my_off_t page, int level, byte *buff)
{
  DBUG_ENTER("_ma_write_keypage");

#ifndef FAST					/* Safety check */
  if (page < info->s->base.keystart ||
      page+keyinfo->block_length > info->state->key_file_length ||
      (page & (MARIA_MIN_KEY_BLOCK_LENGTH-1)))
  {
    DBUG_PRINT("error",("Trying to write inside key status region: "
                        "key_start: %lu  length: %lu  page: %lu",
			(long) info->s->base.keystart,
			(long) info->state->key_file_length,
			(long) page));
    my_errno=EINVAL;
    DBUG_RETURN((-1));
  }
  DBUG_PRINT("page",("write page at: %lu",(long) page));
  DBUG_DUMP("buff",(byte*) buff,maria_getint(buff));
#endif

  DBUG_ASSERT(info->s->pagecache->block_size == keyinfo->block_length);
  DBUG_ASSERT(info->s->pagecache->block_size == info->s->block_size);
  /*
    TODO: replace PAGECACHE_PLAIN_PAGE with PAGECACHE_LSN_PAGE when
    LSN on the pages will be implemented
  */
  DBUG_RETURN(pagecache_write(info->s->pagecache,
                              &info->s->kfile, page / keyinfo->block_length,
                              level, buff, PAGECACHE_PLAIN_PAGE,
                              PAGECACHE_LOCK_LEFT_UNLOCKED,
                              PAGECACHE_PIN_LEFT_PINNED,
                              PAGECACHE_WRITE_DELAY, 0));
} /* maria_write_keypage */


	/* Remove page from disk */

int _ma_dispose(register MARIA_HA *info, MARIA_KEYDEF *keyinfo, my_off_t pos,
                int level)
{
  my_off_t old_link;
  char buff[8];
  uint offset;
  pgcache_page_no_t page_no;
  DBUG_ENTER("_ma_dispose");
  DBUG_PRINT("enter",("pos: %ld", (long) pos));

  old_link= info->s->state.key_del;
  info->s->state.key_del= pos;
  page_no= pos / keyinfo->block_length;
  offset= pos % keyinfo->block_length;
  mi_sizestore(buff,old_link);
  info->s->state.changed|= STATE_NOT_SORTED_PAGES;

  DBUG_ASSERT(info->s->pagecache->block_size == keyinfo->block_length &&
              info->s->pagecache->block_size == info->s->block_size);
  /*
    TODO: replace PAGECACHE_PLAIN_PAGE with PAGECACHE_LSN_PAGE when
    LSN on the pages will be implemented
  */
  DBUG_RETURN(pagecache_write_part(info->s->pagecache,
                                   &info->s->kfile, page_no, level, buff,
                                   PAGECACHE_PLAIN_PAGE,
                                   PAGECACHE_LOCK_LEFT_UNLOCKED,
                                   PAGECACHE_PIN_LEFT_PINNED,
                                   PAGECACHE_WRITE_DELAY, 0,
                                   offset, sizeof(buff)));
} /* _ma_dispose */


	/* Make new page on disk */

my_off_t _ma_new(register MARIA_HA *info, MARIA_KEYDEF *keyinfo, int level)
{
  my_off_t pos;
  byte buff[8];
  DBUG_ENTER("_ma_new");

  if ((pos= info->s->state.key_del) == HA_OFFSET_ERROR)
  {
    if (info->state->key_file_length >=
	info->s->base.max_key_file_length - keyinfo->block_length)
    {
      my_errno=HA_ERR_INDEX_FILE_FULL;
      DBUG_RETURN(HA_OFFSET_ERROR);
    }
    pos=info->state->key_file_length;
    info->state->key_file_length+= keyinfo->block_length;
  }
  else
  {
    DBUG_ASSERT(info->s->pagecache->block_size == keyinfo->block_length &&
                info->s->pagecache->block_size == info->s->block_size);
    /*
      TODO: replace PAGECACHE_PLAIN_PAGE with PAGECACHE_LSN_PAGE when
      LSN on the pages will be implemented
    */
    DBUG_ASSERT(info->s->pagecache->block_size == keyinfo->block_length);
    if (!pagecache_read(info->s->pagecache,
                        &info->s->kfile, pos / keyinfo->block_length, level,
			buff, PAGECACHE_PLAIN_PAGE,
                        PAGECACHE_LOCK_LEFT_UNLOCKED, 0))
      pos= HA_OFFSET_ERROR;
    else
      info->s->state.key_del= mi_sizekorr(buff);
  }
  info->s->state.changed|= STATE_NOT_SORTED_PAGES;
  DBUG_PRINT("exit",("Pos: %ld",(long) pos));
  DBUG_RETURN(pos);
} /* _ma_new */
