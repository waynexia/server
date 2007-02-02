#include "../maria_def.h"
#include <stdio.h>
#include <errno.h>

#ifndef DBUG_OFF
static const char *default_dbug_option;
#endif

#define PCACHE_SIZE (1024*1024*10)

/*#define LOG_FLAGS TRANSLOG_SECTOR_PROTECTION | TRANSLOG_PAGE_CRC */
#define LOG_FLAGS 0
/*#define LONG_BUFFER_SIZE (1024L*1024L*1024L + 1024L*1024L*512)*/
#define LONG_BUFFER_SIZE (1024L*1024L*1024L)
#define MIN_REC_LENGTH 30
#define SHOW_DIVIDER 10
#define LOG_FILE_SIZE (1024L*1024L*1024L + 1024L*1024L*512)
#define ITERATIONS 3
#define WRITERS 3
static uint number_of_writers= WRITERS;

static pthread_cond_t COND_thread_count;
static pthread_mutex_t LOCK_thread_count;
static uint thread_count;

static ulong lens[WRITERS][ITERATIONS];
static LSN lsns1[WRITERS][ITERATIONS];
static LSN lsns2[WRITERS][ITERATIONS];
static uchar *long_buffer;

/*
  Get pseudo-random length of the field in
    limits [MIN_REC_LENGTH..LONG_BUFFER_SIZE]

  SYNOPSYS
    get_len()

  RETURN
    length - length >= 0 length <= LONG_BUFFER_SIZE
*/

static uint32 get_len()
{
  uint32 rec_len;
  do
  {
    rec_len= random() /
      (RAND_MAX / (LONG_BUFFER_SIZE - MIN_REC_LENGTH - 1)) + MIN_REC_LENGTH;
  } while (rec_len >= LONG_BUFFER_SIZE);
  return rec_len;
}


/*
  Check that the buffer filled correctly

  SYNOPSIS
    check_content()
    ptr                  Pointer to the buffer
    length               length of the buffer

  RETURN
    0 - OK
    1 - Error
*/

static my_bool check_content(uchar *ptr, ulong length)
{
  ulong i;
  for (i= 0; i < length; i++)
  {
    if (ptr[i] != (i & 0xFF))
    {
      fprintf(stderr, "Byte # %lu is %x instead of %x",
              i, (uint) ptr[i], (uint) (i & 0xFF));
      return 1;
    }
  }
  return 0;
}


/*
  Read whole record content, and check content (put with offset)

  SYNOPSIS
    read_and_check_content()
    rec                  The record header buffer
    buffer               The buffer to read the record in
    skip                 Skip this number of bytes ot the record content

  RETURN
    0 - OK
    1 - Error
*/


static my_bool read_and_check_content(TRANSLOG_HEADER_BUFFER *rec,
                                      uchar *buffer, uint skip)
{
  int res= 0;
  translog_size_t len;
  DBUG_ENTER("read_and_check_content");
  DBUG_ASSERT(rec->record_length < LONG_BUFFER_SIZE + 7 * 2 + 2);
  if ((len= translog_read_record(&rec->lsn, 0, rec->record_length,
                                 buffer, NULL)) != rec->record_length)
  {
    fprintf(stderr, "Requested %lu byte, read %lu\n",
            (ulong) rec->record_length, (ulong) len);
    res= 1;
  }
  res|= check_content(buffer + skip, rec->record_length - skip);
  DBUG_RETURN(res);
}

void writer(int num)
{
  LSN lsn;
  uchar long_tr_id[6];
  uint i;
  DBUG_ENTER("writer");

  for (i= 0; i < ITERATIONS; i++)
  {
    uint len= get_len();
    lens[num][i]= len;

    int2store(long_tr_id, num);
    int4store(long_tr_id + 2, i);
    if (translog_write_record(&lsn,
                              LOGREC_LONG_TRANSACTION_ID,
                              num, NULL, 6, long_tr_id, 0))
    {
      fprintf(stderr, "Can't write LOGREC_LONG_TRANSACTION_ID record #%lu "
              "thread %i\n", (ulong) i, num);
      translog_destroy();
      return;
    }
    lsns1[num][i]= lsn;
    if (translog_write_record(&lsn,
                              LOGREC_REDO_INSERT_ROW_HEAD,
                              num, NULL, len, long_buffer, 0))
    {
      fprintf(stderr, "Can't write variable record #%lu\n", (ulong) i);
      translog_destroy();
      return;
    }
    lsns2[num][i]= lsn;
    DBUG_PRINT("info", ("thread: %u, iteration: %u, len: %lu, "
                        "lsn1 (%lu,0x%lx) lsn2 (%lu,0x%lx)",
                        num, i, (ulong) lens[num][i],
                        (ulong) lsns1[num][i].file_no,
                        (ulong) lsns1[num][i].rec_offset,
                        (ulong) lsns2[num][i].file_no,
                        (ulong) lsns2[num][i].rec_offset));
    printf("thread: %u, iteration: %u, len: %lu, "
           "lsn1 (%lu,0x%lx) lsn2 (%lu,0x%lx)\n",
           num, i, (ulong) lens[num][i],
           (ulong) lsns1[num][i].file_no,
           (ulong) lsns1[num][i].rec_offset,
           (ulong) lsns2[num][i].file_no, (ulong) lsns2[num][i].rec_offset);
  }
  DBUG_VOID_RETURN;
}


static void *test_thread_writer(void *arg)
{
  int param= *((int*) arg);

  my_thread_init();
  DBUG_ENTER("test_writer");
  DBUG_PRINT("enter", ("param: %d", param));

  writer(param);

  DBUG_PRINT("info", ("Thread %s ended\n", my_thread_name()));
  pthread_mutex_lock(&LOCK_thread_count);
  thread_count--;
  VOID(pthread_cond_signal(&COND_thread_count));        /* Tell main we are
                                                           ready */
  pthread_mutex_unlock(&LOCK_thread_count);
  free((gptr) arg);
  my_thread_end();
  DBUG_RETURN(0);
}


int main(int argc, char **argv __attribute__ ((unused)))
{
  uint32 i;
  uint pagen;
  PAGECACHE pagecache;
  LSN first_lsn, *lsn_ptr;
  TRANSLOG_HEADER_BUFFER rec;
  struct st_translog_scanner_data scanner;
  pthread_t tid;
  pthread_attr_t thr_attr;
  int *param, error;
  int rc;

  bzero(&pagecache, sizeof(pagecache));
  maria_data_root= ".";
  long_buffer= malloc(LONG_BUFFER_SIZE + 7 * 2 + 2);
  if (long_buffer == 0)
  {
    fprintf(stderr, "End of memory\n");
    exit(1);
  }
  for (i= 0; i < (LONG_BUFFER_SIZE + 7 * 2 + 2); i++)
    long_buffer[i]= (i & 0xFF);


  MY_INIT(argv[0]);

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

  DBUG_ENTER("main");
  DBUG_PRINT("info", ("Main thread: %s\n", my_thread_name()));

  if ((error= pthread_cond_init(&COND_thread_count, NULL)))
  {
    fprintf(stderr, "COND_thread_count: %d from pthread_cond_init "
            "(errno: %d)\n", error, errno);
    exit(1);
  }
  if ((error= pthread_mutex_init(&LOCK_thread_count, MY_MUTEX_INIT_FAST)))
  {
    fprintf(stderr, "LOCK_thread_count: %d from pthread_cond_init "
            "(errno: %d)\n", error, errno);
    exit(1);
  }
  if ((error= pthread_attr_init(&thr_attr)))
  {
    fprintf(stderr, "Got error: %d from pthread_attr_init "
            "(errno: %d)\n", error, errno);
    exit(1);
  }
  if ((error= pthread_attr_setdetachstate(&thr_attr, PTHREAD_CREATE_DETACHED)))
  {
    fprintf(stderr,
            "Got error: %d from pthread_attr_setdetachstate (errno: %d)\n",
            error, errno);
    exit(1);
  }
#ifndef pthread_attr_setstacksize               /* void return value */
  if ((error= pthread_attr_setstacksize(&thr_attr, 65536L)))
  {
    fprintf(stderr, "Got error: %d from pthread_attr_setstacksize "
            "(errno: %d)\n", error, errno);
    exit(1);
  }
#endif
#ifdef HAVE_THR_SETCONCURRENCY
  VOID(thr_setconcurrency(2));
#endif

  my_thread_global_init();

  if (ma_control_file_create_or_open())
  {
    fprintf(stderr, "Can't init control file (%d)\n", errno);
    exit(1);
  }
  if ((pagen= init_pagecache(&pagecache, PCACHE_SIZE, 0, 0,
                             TRANSLOG_PAGE_SIZE)) == 0)
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

  srandom(122334817L);
  {
    uchar long_tr_id[6]=
    {
      0x11, 0x22, 0x33, 0x44, 0x55, 0x66
    };

    if (translog_write_record(&first_lsn,
                              LOGREC_LONG_TRANSACTION_ID,
                              0, NULL, 6, long_tr_id, 0))
    {
      fprintf(stderr, "Can't write the first record\n");
      translog_destroy();
      exit(1);
    }
  }


  if ((error= pthread_mutex_lock(&LOCK_thread_count)))
  {
    fprintf(stderr, "LOCK_thread_count: %d from pthread_mutex_lock "
            "(errno: %d)\n", error, errno);
    exit(1);
  }

  while (number_of_writers != 0)
  {
    param= (int*) malloc(sizeof(int));
    *param= number_of_writers - 1;
    if ((error= pthread_create(&tid, &thr_attr, test_thread_writer,
                               (void*) param)))
    {
      fprintf(stderr, "Got error: %d from pthread_create (errno: %d)\n",
              error, errno);
      exit(1);
    }
    thread_count++;
    number_of_writers--;
  }
  DBUG_PRINT("info", ("All threads are started"));
  pthread_mutex_unlock(&LOCK_thread_count);

  pthread_attr_destroy(&thr_attr);

  /* wait finishing */
  if ((error= pthread_mutex_lock(&LOCK_thread_count)))
    fprintf(stderr, "LOCK_thread_count: %d from pthread_mutex_lock\n", error);
  while (thread_count)
  {
    if ((error= pthread_cond_wait(&COND_thread_count, &LOCK_thread_count)))
      fprintf(stderr, "COND_thread_count: %d from pthread_cond_wait\n", error);
  }
  if ((error= pthread_mutex_unlock(&LOCK_thread_count)))
    fprintf(stderr, "LOCK_thread_count: %d from pthread_mutex_unlock\n", error);
  DBUG_PRINT("info", ("All threads ended"));

  /* Find last LSN and flush up to it (all our log) */
  {
    LSN max=
    {
      0, 0
    };
    for (i= 0; i < WRITERS; i++)
    {
      if (cmp_translog_addr(lsns2[i][ITERATIONS - 1], max) > 0)
        max= lsns2[i][ITERATIONS - 1];
    }
    DBUG_PRINT("info", ("first lsn: (%lu,0x%lx), max lsn: (%lu,0x%lx)",
                        (ulong) first_lsn.file_no,
                        (ulong) first_lsn.rec_offset,
                        (ulong) max.file_no, (ulong) max.rec_offset));
    translog_flush(&max);
  }

  rc= 1;

  {
    uint indeces[WRITERS];
    uint index, len, stage;
    bzero(indeces, sizeof(uint) * WRITERS);

    bzero(indeces, sizeof(indeces));

    lsn_ptr= &first_lsn;
    for (i= 0;; i++)
    {
      len= translog_read_next_record_header(lsn_ptr, &rec, 1, &scanner);
      lsn_ptr= NULL;

      if (len == 0)
      {
        fprintf(stderr, "1-%d translog_read_next_record_header failed (%d)\n",
                i, errno);
        translog_free_record_header(&rec);
        goto err;
      }
      if (rec.lsn.file_no == CONTROL_FILE_IMPOSSIBLE_FILENO)
      {
        if (i != WRITERS * ITERATIONS * 2)
        {
          fprintf(stderr, "EOL met at iteration %u instead of %u\n",
                  i, ITERATIONS * WRITERS * 2);
          translog_free_record_header(&rec);
          goto err;
        }
        break;
      }
      index= indeces[rec.short_trid] / 2;
      stage= indeces[rec.short_trid] % 2;
      printf("read(%d) thread: %d, iteration %d, stage %d\n",
             i, (uint) rec.short_trid, index, stage);
      if (stage == 0)
      {
        if (rec.type !=LOGREC_LONG_TRANSACTION_ID ||
            rec.record_length != 6 ||
            uint2korr(rec.header) != rec.short_trid ||
            index != uint4korr(rec.header + 2) ||
            cmp_translog_addr(lsns1[rec.short_trid][index], rec.lsn) != 0)
        {
          fprintf(stderr, "Incorrect LOGREC_LONG_TRANSACTION_ID data read(%d)\n"
                  "type %u, strid %u %u, len %u, i: %u %u, "
                  "lsn(%lu,0x%lx) (%lu,0x%lx)\n",
                  i, (uint) rec.type,
                  (uint) rec.short_trid, (uint) uint2korr(rec.header),
                  (uint) rec.record_length,
                  (uint) index, (uint) uint4korr(rec.header + 2),
                  (ulong) rec.lsn.file_no, (ulong) rec.lsn.rec_offset,
                  (ulong) lsns1[rec.short_trid][index].file_no,
                  (ulong) lsns1[rec.short_trid][index].rec_offset);
          translog_free_record_header(&rec);
          goto err;
        }
      }
      else
      {
        if (rec.type !=LOGREC_REDO_INSERT_ROW_HEAD ||
            len != 9 ||
            rec.record_length != lens[rec.short_trid][index] ||
            cmp_translog_addr(lsns2[rec.short_trid][index], rec.lsn) != 0 ||
            check_content(rec.header, len))
        {
          fprintf(stderr,
                  "Incorrect LOGREC_REDO_INSERT_ROW_HEAD data read(%d) "
                  " thread: %d, iteration %d, stage %d\n"
                  "type %u (%d), len %u, length %lu %lu (%d) "
                  "lsn(%lu,0x%lx) (%lu,0x%lx)\n",
                  i, (uint) rec.short_trid, index, stage,
                  (uint) rec.type, (rec.type !=LOGREC_REDO_INSERT_ROW_HEAD),
                  (uint) len,
                  (ulong) rec.record_length, lens[rec.short_trid][index],
                  (rec.record_length != lens[rec.short_trid][index]),
                  (ulong) rec.lsn.file_no, (ulong) rec.lsn.rec_offset,
                  (ulong) lsns2[rec.short_trid][index].file_no,
                  (ulong) lsns2[rec.short_trid][index].rec_offset);
          translog_free_record_header(&rec);
          goto err;
        }
        if (read_and_check_content(&rec, long_buffer, 0))
        {
          fprintf(stderr,
                  "Incorrect LOGREC_REDO_INSERT_ROW_HEAD in whole rec read "
                  "lsn(%u,0x%lx)\n",
                  (uint) rec.lsn.file_no, (ulong) rec.lsn.rec_offset);
          translog_free_record_header(&rec);
          goto err;
        }
      }
      translog_free_record_header(&rec);
      indeces[rec.short_trid]++;
    }
  }

  rc= 0;
err:
  translog_destroy();
  end_pagecache(&pagecache, 1);
  ma_control_file_end();

  DBUG_RETURN(test(exit_status() || rc));
}
