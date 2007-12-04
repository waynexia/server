#include "../maria_def.h"
#include <stdio.h>
#include <errno.h>
#include <tap.h>
#include "../trnman.h"

extern my_bool maria_log_remove();
extern void example_loghandler_init();

#ifndef DBUG_OFF
static const char *default_dbug_option;
#endif

#define PCACHE_SIZE (1024*1024*10)
#define PCACHE_PAGE TRANSLOG_PAGE_SIZE
#define LOG_FILE_SIZE (4*1024L*1024L)
#define LOG_FLAGS 0
#define LONG_BUFFER_SIZE (LOG_FILE_SIZE + LOG_FILE_SIZE / 2)


int main(int argc __attribute__((unused)), char *argv[])
{
  ulong i;
  uint pagen;
  uchar long_tr_id[6];
  PAGECACHE pagecache;
  LSN lsn;
  LEX_STRING parts[TRANSLOG_INTERNAL_PARTS + 1];
  uchar *long_buffer= malloc(LONG_BUFFER_SIZE);

  MY_INIT(argv[0]);

  plan(4);

  bzero(&pagecache, sizeof(pagecache));
  bzero(long_buffer, LONG_BUFFER_SIZE);
  maria_data_root= ".";
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

  if (ma_control_file_create_or_open(TRUE))
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
  if (translog_init(".", LOG_FILE_SIZE, 50112, 0, &pagecache, LOG_FLAGS))
  {
    fprintf(stderr, "Can't init loghandler (%d)\n", errno);
    translog_destroy();
    exit(1);
  }
  example_loghandler_init();
  /* Suppressing of automatic record writing */
  dummy_transaction_object.first_undo_lsn|= TRANSACTION_LOGGED_LONG_ID;

  /* write more then 1 file */
  int4store(long_tr_id, 0);
  parts[TRANSLOG_INTERNAL_PARTS + 0].str= (char*)long_tr_id;
  parts[TRANSLOG_INTERNAL_PARTS + 0].length= 6;
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

  translog_purge(lsn);
  if (!translog_is_file(1))
  {
    fprintf(stderr, "First file was removed after first record\n");
    translog_destroy();
    exit(1);
  }
  ok(1, "First is not removed");

  for(i= 0; i < LOG_FILE_SIZE/6 && LSN_FILE_NO(lsn) == 1; i++)
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
  }

  translog_purge(lsn);
  if (translog_is_file(1))
  {
    fprintf(stderr, "First file was not removed.\n");
    translog_destroy();
    exit(1);
  }

  ok(1, "First file is removed");

  parts[TRANSLOG_INTERNAL_PARTS + 0].str= (char*)long_buffer;
  parts[TRANSLOG_INTERNAL_PARTS + 0].length= LONG_BUFFER_SIZE;
  if (translog_write_record(&lsn,
			    LOGREC_VARIABLE_RECORD_0LSN_EXAMPLE,
			    &dummy_transaction_object, NULL, LONG_BUFFER_SIZE,
			    TRANSLOG_INTERNAL_PARTS + 1, parts, NULL, NULL))
  {
    fprintf(stderr, "Can't write variable record\n");
    translog_destroy();
    exit(1);
  }

  translog_purge(lsn);
  if (!translog_is_file(2) || !translog_is_file(3))
  {
    fprintf(stderr, "Second file (%d) or third file (%d) is not present.\n",
	    translog_is_file(2), translog_is_file(3));
    translog_destroy();
    exit(1);
  }

  ok(1, "Second and third files are not removed");

  int4store(long_tr_id, 0);
  parts[TRANSLOG_INTERNAL_PARTS + 0].str= (char*)long_tr_id;
  parts[TRANSLOG_INTERNAL_PARTS + 0].length= 6;
  if (translog_write_record(&lsn,
                            LOGREC_FIXED_RECORD_0LSN_EXAMPLE,
                            &dummy_transaction_object, NULL, 6,
                            TRANSLOG_INTERNAL_PARTS + 1,
                            parts, NULL, NULL))
  {
    fprintf(stderr, "Can't write last record\n");
    translog_destroy();
    exit(1);
  }

  translog_purge(lsn);
  if (translog_is_file(2))
  {
    fprintf(stderr, "Second file is not removed\n");
    translog_destroy();
    exit(1);
  }

  ok(1, "Second file is removed");

  translog_destroy();
  end_pagecache(&pagecache, 1);
  ma_control_file_end();
  if (maria_log_remove())
    exit(1);
  exit(0);
}
