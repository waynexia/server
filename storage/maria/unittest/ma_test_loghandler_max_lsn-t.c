/* Copyright (C) 2006-2008 MySQL AB

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

#include "../maria_def.h"
#include <stdio.h>
#include <errno.h>
#include <tap.h>
#include "../trnman.h"

extern my_bool maria_log_remove();
extern void translog_example_table_init();

#ifndef DBUG_OFF
static const char *default_dbug_option;
#endif

#define PCACHE_SIZE (1024*1024*10)
#define PCACHE_PAGE TRANSLOG_PAGE_SIZE
#define LOG_FILE_SIZE (8*1024L*1024L)
#define LOG_FLAGS 0


int main(int argc __attribute__((unused)), char *argv[])
{
  ulong i;
  uint pagen;
  uchar long_tr_id[6];
  PAGECACHE pagecache;
  LSN lsn, max_lsn, last_lsn= LSN_IMPOSSIBLE;
  LEX_CUSTRING parts[TRANSLOG_INTERNAL_PARTS + 1];

  MY_INIT(argv[0]);

  plan(2);

  bzero(&pagecache, sizeof(pagecache));
  maria_data_root= (char *)".";
  if (maria_log_remove())
    exit(1);

  bzero(long_tr_id, 6);
#ifndef DBUG_OFF
#if defined(__WIN__)
  default_dbug_option= "d:t:i:O,\\ma_test_loghandler.trace";
#else
  default_dbug_option= "d:t:i:o,/tmp/ma_test_loghandler.trace";
#endif
  if (argc > 1)
  {
    DBUG_SET(default_dbug_option);
    DBUG_SET_INITIAL(default_dbug_option);
  }
#endif

  if (ma_control_file_open(TRUE, TRUE))
  {
    fprintf(stderr, "Can't init control file (%d)\n", errno);
    exit(1);
  }
  if ((pagen= init_pagecache(&pagecache, PCACHE_SIZE, 0, 0,
                             PCACHE_PAGE, 0)) == 0)
  {
    fprintf(stderr, "Got error: init_pagecache() (errno: %d)\n", errno);
    exit(1);
  }
  if (translog_init_with_table(".", LOG_FILE_SIZE, 50112, 0, &pagecache,
                               LOG_FLAGS, 0, &translog_example_table_init,
                               0))
  {
    fprintf(stderr, "Can't init loghandler (%d)\n", errno);
    exit(1);
  }
  /* Suppressing of automatic record writing */
  dummy_transaction_object.first_undo_lsn|= TRANSACTION_LOGGED_LONG_ID;

  max_lsn= translog_get_file_max_lsn_stored(1);
  if (max_lsn == 1)
  {
    fprintf(stderr, "Error reading the first log file.");
    translog_destroy();
    exit(1);
  }
  if (max_lsn != LSN_IMPOSSIBLE)
  {
    fprintf(stderr, "Incorrect first lsn response (%lu,0x%lx).",
            LSN_IN_PARTS(max_lsn));
    translog_destroy();
    exit(1);
  }
  ok(1, "Empty log response");


  /* write more then 1 file */
  int4store(long_tr_id, 0);
  parts[TRANSLOG_INTERNAL_PARTS + 0].str= long_tr_id;
  parts[TRANSLOG_INTERNAL_PARTS + 0].length= 6;
  for(i= 0; i < LOG_FILE_SIZE/6; i++)
  {
    if (translog_write_record(&lsn,
                              LOGREC_FIXED_RECORD_0LSN_EXAMPLE,
                              &dummy_transaction_object, NULL, 6,
                              TRANSLOG_INTERNAL_PARTS + 1,
                              parts, NULL, NULL))
    {
      fprintf(stderr, "Can't write record #%lu\n", (ulong) 0);
      translog_destroy();
      exit(1);
    }
    if (LSN_FILE_NO(lsn) == 1)
      last_lsn= lsn;
  }


  max_lsn= translog_get_file_max_lsn_stored(1);
  if (max_lsn == 1)
  {
    fprintf(stderr, "Error reading the first log file\n");
    translog_destroy();
    exit(1);
  }
  if (max_lsn == LSN_IMPOSSIBLE)
  {
    fprintf(stderr, "Isn't first file still finished?!!\n");
    translog_destroy();
    exit(1);
  }
  if (max_lsn != last_lsn)
  {
    fprintf(stderr, "Incorrect max lsn: (%lu,0x%lx)  "
            " last lsn on first file: (%lu,0x%lx)\n",
            LSN_IN_PARTS(max_lsn), LSN_IN_PARTS(last_lsn));
    translog_destroy();
    exit(1);
  }

  ok(1, "First file max LSN");

  translog_destroy();
  end_pagecache(&pagecache, 1);
  ma_control_file_end();
  if (maria_log_remove())
    exit(1);
  exit(0);
}
