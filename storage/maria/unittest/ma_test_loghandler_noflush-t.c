#include "../maria_def.h"
#include <stdio.h>
#include <errno.h>
#include <tap.h>
#include "../trnman.h"

extern my_bool maria_log_remove();

#ifndef DBUG_OFF
static const char *default_dbug_option;
#endif

#define PCACHE_SIZE (1024*1024*10)
#define PCACHE_PAGE TRANSLOG_PAGE_SIZE
#define LOG_FILE_SIZE (1024L*1024L*1024L + 1024L*1024L*512)
#define LOG_FLAGS 0

static char *first_translog_file= (char*)"maria_log.00000001";

int main(int argc __attribute__((unused)), char *argv[])
{
  uint pagen;
  int rc= 1;
  uchar long_tr_id[6];
  PAGECACHE pagecache;
  LSN first_lsn;
  MY_STAT st;
  TRANSLOG_HEADER_BUFFER rec;
  LEX_STRING parts[TRANSLOG_INTERNAL_PARTS + 1];

  MY_INIT(argv[0]);

  plan(1);

  bzero(&pagecache, sizeof(pagecache));
  maria_data_root= ".";
  if (maria_log_remove())
    exit(1);
  /* be sure that we have no logs in the directory*/
  if (my_stat(CONTROL_FILE_BASE_NAME, &st,  MYF(0)))
    my_delete(CONTROL_FILE_BASE_NAME, MYF(0));
  if (my_stat(first_translog_file, &st,  MYF(0)))
    my_delete(first_translog_file, MYF(0));

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
                             PCACHE_PAGE)) == 0)
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

  int4store(long_tr_id, 0);
  long_tr_id[5]= 0xff;
  parts[TRANSLOG_INTERNAL_PARTS + 0].str= (char*)long_tr_id;
  parts[TRANSLOG_INTERNAL_PARTS + 0].length= 6;
  if (translog_write_record(&first_lsn,
                            LOGREC_FIXED_RECORD_0LSN_EXAMPLE,
                            &dummy_transaction_object, NULL, 6,
                            TRANSLOG_INTERNAL_PARTS + 1,
                            parts, NULL))
  {
    fprintf(stderr, "Can't write record #%lu\n", (ulong) 0);
    translog_destroy();
    exit(1);
  }

  translog_size_t len= translog_read_record_header(first_lsn, &rec);
  if (len == 0)
  {
    fprintf(stderr, "translog_read_record_header failed (%d)\n", errno);
    goto err;
  }
  if (rec.type !=LOGREC_FIXED_RECORD_0LSN_EXAMPLE || rec.short_trid != 0 ||
      rec.record_length != 6 || uint4korr(rec.header) != 0 ||
      ((uchar)rec.header[4]) != 0 || ((uchar)rec.header[5]) != 0xFF ||
      first_lsn != rec.lsn)
  {
    fprintf(stderr, "Incorrect LOGREC_FIXED_RECORD_0LSN_EXAMPLE "
            "data read(0)\n"
            "type: %u (%d)  strid: %u (%d)  len: %u (%d)  i: %u (%d), "
            "4: %u (%d)  5: %u (%d)  "
            "lsn(%lu,0x%lx) (%d)\n",
            (uint) rec.type, (rec.type !=LOGREC_FIXED_RECORD_0LSN_EXAMPLE),
            (uint) rec.short_trid, (rec.short_trid != 0),
            (uint) rec.record_length, (rec.record_length != 6),
            (uint) uint4korr(rec.header), (uint4korr(rec.header) != 0),
            (uint) rec.header[4], (((uchar)rec.header[4]) != 0),
            (uint) rec.header[5], (((uchar)rec.header[5]) != 0xFF),
            LSN_IN_PARTS(rec.lsn), (first_lsn != rec.lsn));
    goto err;
  }

  ok(1, "read OK");
  rc= 0;

err:
  translog_destroy();
  end_pagecache(&pagecache, 1);
  ma_control_file_end();
  if (maria_log_remove())
    exit(1);

  exit(rc);
}
