/* Copyright (C) 2006, 2007 MySQL AB

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

/*
  WL#3072 Maria recovery
  First version written by Guilhem Bichot on 2006-04-27.
*/

/* Here is the implementation of this module */

#include "maria_def.h"
#include "ma_recovery.h"
#include "ma_blockrec.h"
#include "trnman.h"

struct st_trn_for_recovery /* used only in the REDO phase */
{
  LSN group_start_lsn, undo_lsn, first_undo_lsn;
  TrID long_trid;
};
struct st_dirty_page /* used only in the REDO phase */
{
  uint64 file_and_page_id;
  LSN rec_lsn;
};
struct st_table_for_recovery /* used in the REDO and UNDO phase */
{
  MARIA_HA *info;
  File org_kfile, org_dfile; /**< OS descriptors when Checkpoint saw table */
};
/* Variables used by all functions of this module. Ok as single-threaded */
static struct st_trn_for_recovery *all_active_trans;
static struct st_table_for_recovery *all_tables;
static HASH all_dirty_pages;
static struct st_dirty_page *dirty_pages_pool;
static LSN current_group_end_lsn,
  checkpoint_start= LSN_IMPOSSIBLE;
static TrID max_long_trid= 0; /**< max long trid seen by REDO phase */
static FILE *tracef; /**< trace file for debugging */
static my_bool skip_DDLs; /**< if REDO phase should skip DDL records */

#define prototype_redo_exec_hook(R)                                          \
  static int exec_REDO_LOGREC_ ## R(const TRANSLOG_HEADER_BUFFER *rec)

#define prototype_redo_exec_hook_dummy(R)                                    \
  static int exec_REDO_LOGREC_ ## R(const TRANSLOG_HEADER_BUFFER *rec        \
                               __attribute ((unused)))

#define prototype_undo_exec_hook(R)                                          \
  static int exec_UNDO_LOGREC_ ## R(const TRANSLOG_HEADER_BUFFER *rec, TRN *trn)

prototype_redo_exec_hook(LONG_TRANSACTION_ID);
prototype_redo_exec_hook_dummy(CHECKPOINT);
prototype_redo_exec_hook(REDO_CREATE_TABLE);
prototype_redo_exec_hook(REDO_RENAME_TABLE);
prototype_redo_exec_hook(REDO_REPAIR_TABLE);
prototype_redo_exec_hook(REDO_DROP_TABLE);
prototype_redo_exec_hook(FILE_ID);
prototype_redo_exec_hook(REDO_INSERT_ROW_HEAD);
prototype_redo_exec_hook(REDO_INSERT_ROW_TAIL);
prototype_redo_exec_hook(REDO_PURGE_ROW_HEAD);
prototype_redo_exec_hook(REDO_PURGE_ROW_TAIL);
prototype_redo_exec_hook(REDO_PURGE_BLOCKS);
prototype_redo_exec_hook(REDO_DELETE_ALL);
prototype_redo_exec_hook(UNDO_ROW_INSERT);
prototype_redo_exec_hook(UNDO_ROW_DELETE);
prototype_redo_exec_hook(UNDO_ROW_UPDATE);
prototype_redo_exec_hook(COMMIT);
prototype_redo_exec_hook(CLR_END);
prototype_undo_exec_hook(UNDO_ROW_INSERT);
prototype_undo_exec_hook(UNDO_ROW_DELETE);
prototype_undo_exec_hook(UNDO_ROW_UPDATE);

static int run_redo_phase(LSN lsn, my_bool apply);
static uint end_of_redo_phase(my_bool prepare_for_undo_phase);
static int run_undo_phase(uint unfinished);
static void display_record_position(const LOG_DESC *log_desc,
                                    const TRANSLOG_HEADER_BUFFER *rec,
                                    uint number);
static int display_and_apply_record(const LOG_DESC *log_desc,
                                    const TRANSLOG_HEADER_BUFFER *rec);
static MARIA_HA *get_MARIA_HA_from_REDO_record(const
                                               TRANSLOG_HEADER_BUFFER *rec);
static MARIA_HA *get_MARIA_HA_from_UNDO_record(const
                                               TRANSLOG_HEADER_BUFFER *rec);
static void prepare_table_for_close(MARIA_HA *info, TRANSLOG_ADDRESS horizon);
static LSN parse_checkpoint_record(LSN lsn);
static void new_transaction(uint16 sid, TrID long_id, LSN undo_lsn,
                            LSN first_undo_lsn);
static int new_table(uint16 sid, const char *name,
                     File org_kfile, File org_dfile,
                     LSN lsn_of_file_id);
static int new_page(File fileid, pgcache_page_no_t pageid, LSN rec_lsn,
                    struct st_dirty_page *dirty_page);
static int close_all_tables(void);
static void print_redo_phase_progress(TRANSLOG_ADDRESS addr);

/** @brief global [out] buffer for translog_read_record(); never shrinks */
static LEX_STRING log_record_buffer;
static void enlarge_buffer(const TRANSLOG_HEADER_BUFFER *rec)
{
  if (log_record_buffer.length < rec->record_length)
  {
    log_record_buffer.length= rec->record_length;
    log_record_buffer.str= my_realloc(log_record_buffer.str,
                                      rec->record_length,
                                      MYF(MY_WME | MY_ALLOW_ZERO_PTR));
  }
}
static my_bool redo_phase_message_printed;
/** @brief Prints to a trace file if it is not NULL */
void tprint(FILE *trace_file, const char *format, ...)
  ATTRIBUTE_FORMAT(printf, 2, 3);
void tprint(FILE *trace_file, const char *format, ...)
{
  va_list args;
  va_start(args, format);
  if (trace_file != NULL)
    vfprintf(trace_file, format, args);
  va_end(args);
}

#define ALERT_USER() DBUG_ASSERT(0)


/**
   @brief Recovers from the last checkpoint.

   Runs the REDO phase using special structures, then sets up the playground
   of runtime: recreates transactions inside trnman, open tables with their
   two-byte-id mapping; takes a checkpoint and runs the UNDO phase. Closes all
   tables.

   @return Operation status
     @retval 0      OK
     @retval !=0    Error
*/

int maria_recover(void)
{
  int res= 1;
  FILE *trace_file;
  DBUG_ENTER("maria_recover");

  DBUG_ASSERT(!maria_in_recovery);
  maria_in_recovery= TRUE;

#if !defined(DBUG_OFF) && defined(EXTRA_DEBUG)
  trace_file= fopen("maria_recovery.trace", "w");
#else
  trace_file= NULL; /* no trace file for being fast */
#endif
  tprint(trace_file, "TRACE of the last MARIA recovery from mysqld\n");
  DBUG_ASSERT(maria_pagecache->inited);
  res= maria_apply_log(LSN_IMPOSSIBLE, TRUE, trace_file, TRUE, TRUE);
  if (!res)
    tprint(trace_file, "SUCCESS\n");
  if (trace_file)
    fclose(trace_file);
  maria_in_recovery= FALSE;
  DBUG_RETURN(res);
}


/**
   @brief Displays and/or applies the log

   @param  from_lsn        LSN from which log reading/applying should start;
                           LSN_IMPOSSIBLE means "use last checkpoint"
   @param  apply           if log records should be applied or not
   @param  trace_file      trace file where progress/debug messages will go
   @param  skip_DDLs       Should DDL records (CREATE/RENAME/DROP/REPAIR)
                           be skipped by the REDO phase or not

   @todo This trace_file thing is primitive; soon we will make it similar to
   ma_check_print_warning() etc, and a successful recovery does not need to
   create a trace file. But for debugging now it is useful.

   @return Operation status
     @retval 0      OK
     @retval !=0    Error
*/

int maria_apply_log(LSN from_lsn, my_bool apply, FILE *trace_file,
                    my_bool should_run_undo_phase, my_bool skip_DDLs_arg)
{
  int error= 0;
  uint unfinished_trans;
  DBUG_ENTER("maria_apply_log");

  DBUG_ASSERT(apply || !should_run_undo_phase);
  DBUG_ASSERT(!maria_multi_threaded);
  all_active_trans= (struct st_trn_for_recovery *)
    my_malloc((SHORT_TRID_MAX + 1) * sizeof(struct st_trn_for_recovery),
              MYF(MY_ZEROFILL));
  all_tables= (struct st_table_for_recovery *)
    my_malloc((SHARE_ID_MAX + 1) * sizeof(struct st_table_for_recovery),
              MYF(MY_ZEROFILL));
  if (!all_active_trans || !all_tables)
    goto err;

  redo_phase_message_printed= FALSE;
  tracef= trace_file;
  if (!(skip_DDLs= skip_DDLs_arg))
  {
    /*
      Example of what can go wrong when replaying DDLs:
      CREATE TABLE t (logged); INSERT INTO t VALUES(1) (logged);
      ALTER TABLE t ... which does
        CREATE a temporary table #sql... (logged)
        INSERT data from t into #sql... (not logged)
        RENAME #sql TO t (logged)
      Removing tables by hand and replaying the log will leave in the
      end an empty table "t": missing records. If after the RENAME an INSERT
      into t was done, that row had number 1 in its page, executing the
      REDO_INSERT_ROW_HEAD on the recreated empty t will fail (assertion
      failure in _ma_apply_redo_insert_row_head_or_tail(): new data page is
      created whereas rownr is not 0).
      Another issue is that replaying of DDLs is not correct enough to work if
      there was a crash during a DDL (see comment in execution of
      REDO_RENAME_TABLE ).
    */
    tprint(tracef, "WARNING: MySQL server currently disables log records"
           " about insertion of data by ALTER TABLE"
           " (copy_data_between_tables()), applying of log records may"
           " well not work. Additionally, applying of DDL records will"
           " cause damage if there are tables left by a crash of a DDL.\n");
  }

  if (from_lsn == LSN_IMPOSSIBLE)
  {
    if (last_checkpoint_lsn == LSN_IMPOSSIBLE)
    {
      from_lsn= translog_first_theoretical_lsn();
      /*
        as far as we have not yet any checkpoint then the very first
        log file should be present.
      */
      if (unlikely((from_lsn == LSN_IMPOSSIBLE) ||
                   (from_lsn == LSN_ERROR)))
        goto err;
    }
    else
    {
      from_lsn= parse_checkpoint_record(last_checkpoint_lsn);
      if (from_lsn == LSN_IMPOSSIBLE)
        goto err;
      from_lsn= translog_next_LSN(from_lsn, LSN_IMPOSSIBLE);
      if (from_lsn == LSN_ERROR)
        goto err;
      /*
        from_lsn LSN_IMPOSSIBLE will be correctly processed
        by run_redo_phase()
      */
    }
  }

  if (run_redo_phase(from_lsn, apply))
    goto err;

  unfinished_trans= end_of_redo_phase(should_run_undo_phase);
  if (unfinished_trans == (uint)-1)
    goto err;
  if (should_run_undo_phase)
  {
    if (run_undo_phase(unfinished_trans))
      return 1;
  }
  else if (unfinished_trans > 0)
    tprint(tracef, "WARNING: %u unfinished transactions; some tables may be"
           " left inconsistent!\n", unfinished_trans);

  /*
    we don't use maria_panic() because it would maria_end(), and Recovery does
    not want that (we want to keep some modules initialized for runtime).
  */
  if (close_all_tables())
    goto err;

  /* If inside ha_maria, a checkpoint will soon be taken and save our work */
  goto end;
err:
  error= 1;
  tprint(tracef, "Recovery of tables with transaction logs FAILED\n");
end:
  hash_free(&all_dirty_pages);
  bzero(&all_dirty_pages, sizeof(all_dirty_pages));
  my_free(dirty_pages_pool, MYF(MY_ALLOW_ZERO_PTR));
  dirty_pages_pool= NULL;
  my_free(all_tables, MYF(MY_ALLOW_ZERO_PTR));
  all_tables= NULL;
  my_free(all_active_trans, MYF(MY_ALLOW_ZERO_PTR));
  all_active_trans= NULL;
  my_free(log_record_buffer.str, MYF(MY_ALLOW_ZERO_PTR));
  log_record_buffer.str= NULL;
  log_record_buffer.length= 0;
  if (tracef != stdout && redo_phase_message_printed)
  {
    /** @todo RECOVERY BUG all prints to stderr should go to error log */
    fprintf(stderr, "\n");
  }
  /* we don't cleanly close tables if we hit some error (may corrupt them) */
  DBUG_RETURN(error);
}


/* very basic info about the record's header */
static void display_record_position(const LOG_DESC *log_desc,
                                    const TRANSLOG_HEADER_BUFFER *rec,
                                    uint number)
{
  /*
    if number==0, we're going over records which we had already seen and which
    form a group, so we indent below the group's end record
  */
  tprint(tracef, "%sRec#%u LSN (%lu,0x%lx) short_trid %u %s(num_type:%u) len %lu\n",
         number ? "" : "   ", number, LSN_IN_PARTS(rec->lsn),
         rec->short_trid, log_desc->name, rec->type,
         (ulong)rec->record_length);
}


static int display_and_apply_record(const LOG_DESC *log_desc,
                                    const TRANSLOG_HEADER_BUFFER *rec)
{
  int error;
  if (log_desc->record_execute_in_redo_phase == NULL)
  {
    /* die on all not-yet-handled records :) */
    DBUG_ASSERT("one more hook" == "to write");
    return 1;
  }
  if ((error= (*log_desc->record_execute_in_redo_phase)(rec)))
    tprint(tracef, "Got error when executing redo on record\n");
  return error;
}


prototype_redo_exec_hook(LONG_TRANSACTION_ID)
{
  uint16 sid= rec->short_trid;
  TrID long_trid= all_active_trans[sid].long_trid;
  /* abort group of this trn (must be of before a crash) */
  LSN gslsn= all_active_trans[sid].group_start_lsn;
  if (gslsn != LSN_IMPOSSIBLE)
  {
    tprint(tracef, "Group at LSN (%lu,0x%lx) short_trid %u aborted\n",
           LSN_IN_PARTS(gslsn), sid);
    all_active_trans[sid].group_start_lsn= LSN_IMPOSSIBLE;
  }
  if (long_trid != 0)
  {
    LSN ulsn= all_active_trans[sid].undo_lsn;
    if (ulsn != LSN_IMPOSSIBLE)
    {
      char llbuf[22];
      llstr(long_trid, llbuf);
      tprint(tracef, "Found an old transaction long_trid %s short_trid %u"
             " with same short id as this new transaction, and has neither"
             " committed nor rollback (undo_lsn: (%lu,0x%lx))\n", llbuf,
             sid, LSN_IN_PARTS(ulsn));
      goto err;
    }
  }
  long_trid= uint6korr(rec->header);
  new_transaction(sid, long_trid, LSN_IMPOSSIBLE, LSN_IMPOSSIBLE);
  goto end;
err:
  ALERT_USER();
  return 1;
end:
  return 0;
}


static void new_transaction(uint16 sid, TrID long_id, LSN undo_lsn,
                            LSN first_undo_lsn)
{
  char llbuf[22];
  all_active_trans[sid].long_trid= long_id;
  llstr(long_id, llbuf);
  tprint(tracef, "Transaction long_trid %s short_trid %u starts\n",
         llbuf, sid);
  all_active_trans[sid].undo_lsn= undo_lsn;
  all_active_trans[sid].first_undo_lsn= first_undo_lsn;
  set_if_bigger(max_long_trid, long_id);
}


prototype_redo_exec_hook_dummy(CHECKPOINT)
{
  /* the only checkpoint we care about was found via control file, ignore */
  return 0;
}


prototype_redo_exec_hook(REDO_CREATE_TABLE)
{
  File dfile= -1, kfile= -1;
  char *linkname_ptr, filename[FN_REFLEN];
  char *name, *ptr;
  myf create_flag;
  uint flags;
  int error= 1, create_mode= O_RDWR | O_TRUNC;
  MARIA_HA *info= NULL;
  if (skip_DDLs)
  {
    tprint(tracef, "we skip DDLs\n");
    return 0;
  }
  enlarge_buffer(rec);
  if (log_record_buffer.str == NULL ||
      translog_read_record(rec->lsn, 0, rec->record_length,
                           log_record_buffer.str, NULL) !=
      rec->record_length)
  {
    tprint(tracef, "Failed to read record\n");
    goto end;
  }
  name= log_record_buffer.str;
  tprint(tracef, "Table '%s'", name);
  /* we try hard to get create_rename_lsn, to avoid mistakes if possible */
  info= maria_open(name, O_RDONLY, HA_OPEN_FOR_REPAIR);
  if (info)
  {
    MARIA_SHARE *share= info->s;
    /* check that we're not already using it */
    if (share->reopen != 1)
    {
      tprint(tracef, ", is already open (reopen=%u)\n", share->reopen);
      ALERT_USER();
      goto end;
    }
    DBUG_ASSERT(share->now_transactional == share->base.born_transactional);
    if (!share->base.born_transactional)
    {
      /*
        could be that transactional table was later dropped, and a non-trans
        one was renamed to its name, thus create_rename_lsn is 0 and should
        not be trusted.
      */
      tprint(tracef, ", is not transactional, ignoring creation\n");
      ALERT_USER();
      error= 0;
      goto end;
    }
    if (cmp_translog_addr(share->state.create_rename_lsn, rec->lsn) >= 0)
    {
      tprint(tracef, ", has create_rename_lsn (%lu,0x%lx) more recent than"
             " record, ignoring creation",
             LSN_IN_PARTS(share->state.create_rename_lsn));
      error= 0;
      goto end;
    }
    if (maria_is_crashed(info))
    {
      tprint(tracef, ", is crashed, can't recreate it");
      ALERT_USER();
      goto end;
    }
    maria_close(info);
    info= NULL;
  }
  else /* one or two files absent, or header corrupted... */
    tprint(tracef, "can't be opened, probably does not exist");
  /* if does not exist, or is older, overwrite it */
  /** @todo symlinks */
  ptr= name + strlen(name) + 1;
  if ((flags= ptr[0] ? HA_DONT_TOUCH_DATA : 0))
    tprint(tracef, ", we will only touch index file");
  fn_format(filename, name, "", MARIA_NAME_IEXT,
            (MY_UNPACK_FILENAME |
             (flags & HA_DONT_TOUCH_DATA) ? MY_RETURN_REAL_PATH : 0) |
            MY_APPEND_EXT);
  linkname_ptr= NULL;
  create_flag= MY_DELETE_OLD;
  tprint(tracef, ", creating as '%s'", filename);
  if ((kfile= my_create_with_symlink(linkname_ptr, filename, 0, create_mode,
                                     MYF(MY_WME|create_flag))) < 0)
  {
    tprint(tracef, "Failed to create index file\n");
    goto end;
  }
  ptr++;
  uint kfile_size_before_extension= uint2korr(ptr);
  ptr+= 2;
  uint keystart= uint2korr(ptr);
  ptr+= 2;
  /* set create_rename_lsn (for maria_read_log to be idempotent) */
  lsn_store(ptr + sizeof(info->s->state.header) + 2, rec->lsn);
  /* we also set is_of_horizon, like maria_create() does */
  lsn_store(ptr + sizeof(info->s->state.header) + 2 + LSN_STORE_SIZE,
            rec->lsn);
  if (my_pwrite(kfile, ptr,
                kfile_size_before_extension, 0, MYF(MY_NABP|MY_WME)) ||
      my_chsize(kfile, keystart, 0, MYF(MY_WME)))
  {
    tprint(tracef, "Failed to write to index file\n");
    goto end;
  }
  if (!(flags & HA_DONT_TOUCH_DATA))
  {
    fn_format(filename,name,"", MARIA_NAME_DEXT,
              MY_UNPACK_FILENAME | MY_APPEND_EXT);
    linkname_ptr= NULL;
    create_flag=MY_DELETE_OLD;
    if (((dfile=
          my_create_with_symlink(linkname_ptr, filename, 0, create_mode,
                                 MYF(MY_WME | create_flag))) < 0) ||
        my_close(dfile, MYF(MY_WME)))
    {
      tprint(tracef, "Failed to create data file\n");
      goto end;
    }
    /*
      we now have an empty data file. To be able to
      _ma_initialize_data_file() we need some pieces of the share to be
      correctly filled. So we just open the table (fortunately, an empty
      data file does not preclude this).
    */
    if (((info= maria_open(name, O_RDONLY, 0)) == NULL) ||
        _ma_initialize_data_file(info->s, info->dfile.file))
    {
      tprint(tracef, "Failed to open new table or write to data file\n");
      goto end;
    }
  }
  error= 0;
end:
  tprint(tracef, "\n");
  if (kfile >= 0)
    error|= my_close(kfile, MYF(MY_WME));
  if (info != NULL)
    error|= maria_close(info);
  return error;
}


prototype_redo_exec_hook(REDO_RENAME_TABLE)
{
  char *old_name, *new_name;
  int error= 1;
  MARIA_HA *info= NULL;
  if (skip_DDLs)
  {
    tprint(tracef, "we skip DDLs\n");
    return 0;
  }
  enlarge_buffer(rec);
  if (log_record_buffer.str == NULL ||
      translog_read_record(rec->lsn, 0, rec->record_length,
                           log_record_buffer.str, NULL) !=
      rec->record_length)
  {
    tprint(tracef, "Failed to read record\n");
    goto end;
  }
  old_name= log_record_buffer.str;
  new_name= old_name + strlen(old_name) + 1;
  tprint(tracef, "Table '%s' to rename to '%s'; old-name table ", old_name,
         new_name);
  /*
    Here is why we skip CREATE/DROP/RENAME when doing a recovery from
    ha_maria (whereas we do when called from maria_read_log). Consider:
    CREATE TABLE t;
    RENAME TABLE t to u;
    DROP TABLE u;
    RENAME TABLE v to u; # crash between index rename and data rename.
    And do a Recovery (not removing tables beforehand).
    Recovery replays CREATE, then RENAME: the maria_open("t") works,
    maria_open("u") does not (no data file) so table "u" is considered
    inexistent and so maria_rename() is done which overwrites u's index file,
    which is lost. Ok, the data file (v.MAD) is still available, but only a
    REPAIR USE_FRM can rebuild the index, which is unsafe and downtime.
    So it is preferrable to not execute RENAME, and leave the "mess" of files,
    rather than possibly destroy a file. DBA will manually rename files.
    A safe recovery method would probably require checking the existence of
    the index file and of the data file separately (not via maria_open()), and
    maybe also to store a create_rename_lsn in the data file too
    For now, all we risk is to leave the mess (half-renamed files) left by the
    crash. We however sync files and directories at each file rename. The SQL
    layer is anyway not crash-safe for DDLs (except the repartioning-related
    ones).
    We replay DDLs in maria_read_log to be able to recreate tables from
    scratch. It means that "maria_read_log -a" should not be used on a
    database which just crashed during a DDL. And also ALTER TABLE does not
    log insertions of records into the temporary table, so replaying may
    fail (see comment and warning in maria_apply_log()).
  */
  info= maria_open(old_name, O_RDONLY, HA_OPEN_FOR_REPAIR);
  if (info)
  {
    MARIA_SHARE *share= info->s;
    /*
      We may have open instances on this table. But it does not matter, the
      maria_extra() below will take care of them.
    */
    if (!share->base.born_transactional)
    {
      tprint(tracef, ", is not transactional, ignoring renaming\n");
      ALERT_USER();
      error= 0;
      goto end;
    }
    if (cmp_translog_addr(share->state.create_rename_lsn, rec->lsn) >= 0)
    {
      tprint(tracef, ", has create_rename_lsn (%lu,0x%lx) more recent than"
             " record, ignoring renaming",
             LSN_IN_PARTS(share->state.create_rename_lsn));
      error= 0;
      goto end;
    }
    if (maria_is_crashed(info))
    {
      tprint(tracef, ", is crashed, can't rename it");
      ALERT_USER();
      goto end;
    }
    /*
      This maria_extra() call serves to signal that old open instances of
      this table should not be used anymore, and (only on Windows) to close
      open files so they can be renamed
    */
    if (maria_extra(info, HA_EXTRA_PREPARE_FOR_RENAME, NULL) ||
        maria_close(info))
      goto end;
    info= NULL;
    tprint(tracef, ", is ok for renaming; new-name table ");
  }
  else /* one or two files absent, or header corrupted... */
  {
    tprint(tracef, ", can't be opened, probably does not exist");
    error= 0;
    goto end;
  }
  /*
    We must also check the create_rename_lsn of the 'new_name' table if it
    exists: otherwise we may, with our rename which overwrites, destroy
    another table. For example:
    CREATE TABLE t;
    RENAME t to u;
    DROP TABLE u;
    RENAME v to u; # v is an old table, its creation/insertions not in log
    And start executing the log (without removing tables beforehand): creates
    t, renames it to u (if not testing create_rename_lsn) thus overwriting
    old-named v, drops u, and we are stuck, we have lost data.
  */
  info= maria_open(new_name, O_RDONLY, HA_OPEN_FOR_REPAIR);
  if (info)
  {
    MARIA_SHARE *share= info->s;
    /* We should not have open instances on this table. */
    if (share->reopen != 1)
    {
      tprint(tracef, ", is already open (reopen=%u)\n", share->reopen);
      ALERT_USER();
      goto end;
    }
    if (!share->base.born_transactional)
    {
      tprint(tracef, ", is not transactional, ignoring renaming\n");
      ALERT_USER();
      goto drop;
    }
    if (cmp_translog_addr(share->state.create_rename_lsn, rec->lsn) >= 0)
    {
      tprint(tracef, ", has create_rename_lsn (%lu,0x%lx) more recent than"
             " record, ignoring renaming",
             LSN_IN_PARTS(share->state.create_rename_lsn));
      /*
        We have to drop the old_name table. Consider:
        CREATE TABLE t;
        CREATE TABLE v;
        RENAME TABLE t to u;
        DROP TABLE u;
        RENAME TABLE v to u;
        and apply the log without removing tables beforehand. t will be
        created, v too; in REDO_RENAME u will be more recent, but we still
        have to drop t otherwise it stays.
      */
      goto drop;
    }
    if (maria_is_crashed(info))
    {
      tprint(tracef, ", is crashed, can't rename it");
      ALERT_USER();
      goto end;
    }
    if (maria_close(info))
      goto end;
    info= NULL;
    /* abnormal situation */
    tprint(tracef, ", exists but is older than record, can't rename it");
    goto end;
  }
  else /* one or two files absent, or header corrupted... */
    tprint(tracef, ", can't be opened, probably does not exist");
  tprint(tracef, ", renaming '%s'", old_name);
  if (maria_rename(old_name, new_name))
  {
    tprint(tracef, "Failed to rename table\n");
    goto end;
  }
  info= maria_open(new_name, O_RDONLY, 0);
  if (info == NULL)
  {
    tprint(tracef, "Failed to open renamed table\n");
    goto end;
  }
  if (_ma_update_create_rename_lsn(info->s, rec->lsn, TRUE))
    goto end;
  if (maria_close(info))
    goto end;
  info= NULL;
  error= 0;
  goto end;
drop:
  tprint(tracef, ", only dropping '%s'", old_name);
  if (maria_delete_table(old_name))
  {
    tprint(tracef, "Failed to drop table\n");
    goto end;
  }
  error= 0;
  goto end;
end:
  tprint(tracef, "\n");
  if (info != NULL)
    error|= maria_close(info);
  return error;
}


/*
  The record may come from REPAIR, ALTER TABLE ENABLE KEYS, OPTIMIZE.
*/
prototype_redo_exec_hook(REDO_REPAIR_TABLE)
{
  int error= 1;
  MARIA_HA *info;
  if (skip_DDLs)
  {
    /*
      REPAIR is not exactly a DDL, but it manipulates files without logging
      insertions into them.
    */
    tprint(tracef, "we skip DDLs\n");
    return 0;
  }
  if ((info= get_MARIA_HA_from_REDO_record(rec)) == NULL)
    return 0;
  /*
    Otherwise, the mapping is newer than the table, and our record is newer
    than the mapping, so we can repair.
  */
  tprint(tracef, "   repairing...\n");
  /**
     @todo RECOVERY BUG fix this:
     the maria_chk_init() call causes a heap of linker errors in ha_maria.cc!
  */
#if 0
  HA_CHECK param;
  maria_chk_init(&param);
  param.isam_file_name= info->s->open_file_name;
  param.testflag= uint4korr(rec->header);
  if (maria_repair(&param, info, info->s->open_file_name,
                   param.testflag & T_QUICK))
    goto end;
  if (_ma_update_create_rename_lsn(info->s, rec->lsn, TRUE))
    goto end;
  error= 0;
end:
  return error;
#else
  DBUG_ASSERT("fix this table repairing" == NULL);
  return error;
#endif
}


prototype_redo_exec_hook(REDO_DROP_TABLE)
{
  char *name;
  int error= 1;
  MARIA_HA *info= NULL;
  if (skip_DDLs)
  {
    tprint(tracef, "we skip DDLs\n");
    return 0;
  }
  enlarge_buffer(rec);
  if (log_record_buffer.str == NULL ||
      translog_read_record(rec->lsn, 0, rec->record_length,
                           log_record_buffer.str, NULL) !=
      rec->record_length)
  {
    tprint(tracef, "Failed to read record\n");
    goto end;
  }
  name= log_record_buffer.str;
  tprint(tracef, "Table '%s'", name);
  info= maria_open(name, O_RDONLY, HA_OPEN_FOR_REPAIR);
  if (info)
  {
    MARIA_SHARE *share= info->s;
    /*
      We may have open instances on this table. But it does not matter, the
      maria_extra() below will take care of them.
    */
    if (!share->base.born_transactional)
    {
      tprint(tracef, ", is not transactional, ignoring removal\n");
      ALERT_USER();
      error= 0;
      goto end;
    }
    if (cmp_translog_addr(share->state.create_rename_lsn, rec->lsn) >= 0)
    {
      tprint(tracef, ", has create_rename_lsn (%lu,0x%lx) more recent than"
             " record, ignoring removal",
             LSN_IN_PARTS(share->state.create_rename_lsn));
      error= 0;
      goto end;
    }
    if (maria_is_crashed(info))
    {
      tprint(tracef, ", is crashed, can't drop it");
      ALERT_USER();
      goto end;
    }
    /*
      This maria_extra() call serves to signal that old open instances of
      this table should not be used anymore, and (only on Windows) to close
      open files so they can be deleted
    */
    if (maria_extra(info, HA_EXTRA_PREPARE_FOR_DROP, NULL) ||
        maria_close(info))
      goto end;
    info= NULL;
    /* if it is older, or its header is corrupted, drop it */
    tprint(tracef, ", dropping '%s'", name);
    if (maria_delete_table(name))
    {
      tprint(tracef, "Failed to drop table\n");
      goto end;
    }
  }
  else /* one or two files absent, or header corrupted... */
    tprint(tracef,", can't be opened, probably does not exist");
  error= 0;
end:
  tprint(tracef, "\n");
  if (info != NULL)
    error|= maria_close(info);
  return error;
}


prototype_redo_exec_hook(FILE_ID)
{
  uint16 sid;
  int error= 1;
  const char *name;
  MARIA_HA *info;

  if (cmp_translog_addr(rec->lsn, checkpoint_start) < 0)
  {
    /*
      If that mapping was still true at checkpoint time, it was found in
      checkpoint record, no need to recreate it. If that mapping had ended at
      checkpoint time (table was closed or repaired), a flush and force
      happened and so mapping is not needed.
    */
    tprint(tracef, "ignoring because before checkpoint\n");
    return 0;
  }

  enlarge_buffer(rec);
  if (log_record_buffer.str == NULL ||
      translog_read_record(rec->lsn, 0, rec->record_length,
                           log_record_buffer.str, NULL) !=
       rec->record_length)
  {
    tprint(tracef, "Failed to read record\n");
    goto end;
  }
  sid= fileid_korr(log_record_buffer.str);
  info= all_tables[sid].info;
  if (info != NULL)
  {
    tprint(tracef, "   Closing table '%s'\n", info->s->open_file_name);
    prepare_table_for_close(info, rec->lsn);
    if (maria_close(info))
    {
      tprint(tracef, "Failed to close table\n");
      goto end;
    }
    all_tables[sid].info= NULL;
  }
  name= log_record_buffer.str + FILEID_STORE_SIZE;
  if (new_table(sid, name, -1, -1, rec->lsn))
    goto end;
  error= 0;
end:
  return error;
}


static int new_table(uint16 sid, const char *name,
                     File org_kfile, File org_dfile,
                     LSN lsn_of_file_id)
{
  /*
    -1 (skip table): close table and return 0;
    1 (error): close table and return 1;
    0 (success): leave table open and return 0.
  */
  int error= 1;

  tprint(tracef, "Table '%s', id %u", name, sid);
  MARIA_HA *info= maria_open(name, O_RDWR, HA_OPEN_FOR_REPAIR);
  if (info == NULL)
  {
    tprint(tracef, ", is absent (must have been dropped later?)"
           " or its header is so corrupted that we cannot open it;"
           " we skip it\n");
    error= 0;
    goto end;
  }
  if (maria_is_crashed(info))
  {
    tprint(tracef, "Table is crashed, can't apply log records to it\n");
    goto end;
  }
  MARIA_SHARE *share= info->s;
  /* check that we're not already using it */
  if (share->reopen != 1)
  {
    tprint(tracef, ", is already open (reopen=%u)\n", share->reopen);
    ALERT_USER();
    goto end;
  }
  DBUG_ASSERT(share->now_transactional == share->base.born_transactional);
  if (!share->base.born_transactional)
  {
    tprint(tracef, ", is not transactional\n");
    ALERT_USER();
    error= -1;
    goto end;
  }
  if (cmp_translog_addr(lsn_of_file_id, share->state.create_rename_lsn) <= 0)
  {
    tprint(tracef, ", has create_rename_lsn (%lu,0x%lx) more recent than"
           " LOGREC_FILE_ID's LSN (%lu,0x%lx), ignoring open request",
           LSN_IN_PARTS(share->state.create_rename_lsn),
           LSN_IN_PARTS(lsn_of_file_id));
    error= -1;
    goto end;
  }
  /* don't log any records for this work */
  _ma_tmp_disable_logging_for_table(share);
  /* execution of some REDO records relies on data_file_length */
  my_off_t dfile_len= my_seek(info->dfile.file, 0, SEEK_END, MYF(MY_WME));
  my_off_t kfile_len= my_seek(info->s->kfile.file, 0, SEEK_END, MYF(MY_WME));
  if ((dfile_len == MY_FILEPOS_ERROR) ||
      (kfile_len == MY_FILEPOS_ERROR))
  {
    tprint(tracef, ", length unknown\n");
    goto end;
  }
  share->state.state.data_file_length= dfile_len;
  share->state.state.key_file_length=  kfile_len;
  if ((dfile_len % share->block_size) > 0)
  {
    tprint(tracef, ", has too short last page\n");
    /* Recovery will fix this, no error */
    ALERT_USER();
  }
  /*
    This LSN serves in this situation; assume log is:
    FILE_ID(6->"t2") REDO_INSERT(6) FILE_ID(6->"t1") CHECKPOINT(6->"t1")
    then crash, checkpoint record is parsed and opens "t1" with id 6; assume
    REDO phase starts from the REDO_INSERT above: it will wrongly try to
    update a page of "t1". With this LSN below, REDO_INSERT can realize the
    mapping is newer than itself, and not execute.
    Same example is possible with UNDO_INSERT (update of the state).
  */
  info->s->lsn_of_file_id= lsn_of_file_id;
  all_tables[sid].info= info;
  all_tables[sid].org_kfile= org_kfile;
  all_tables[sid].org_dfile= org_dfile;
  /*
    We don't set info->s->id, it would be useless (no logging in REDO phase);
    if you change that, know that some records in REDO phase call
    _ma_update_create_rename_lsn() which resets info->s->id.
  */
  tprint(tracef, ", opened");
  error= 0;
end:
  tprint(tracef, "\n");
  if (error)
  {
    if (info != NULL)
      maria_close(info);
    if (error == -1)
      error= 0;
  }
  return error;
}


prototype_redo_exec_hook(REDO_INSERT_ROW_HEAD)
{
  int error= 1;
  uchar *buff= NULL;
  MARIA_HA *info= get_MARIA_HA_from_REDO_record(rec);
  if (info == NULL)
  {
    /*
      Table was skipped at open time (because later dropped/renamed, not
      transactional, or create_rename_lsn newer than LOGREC_FILE_ID); it is
      not an error.
    */
    return 0;
  }
  /*
    If REDO's LSN is > page's LSN (read from disk), we are going to modify the
    page and change its LSN. The normal runtime code stores the UNDO's LSN
    into the page. Here storing the REDO's LSN (rec->lsn) would work
    (we are not writing to the log here, so don't have to "flush up to UNDO's
    LSN"). But in a test scenario where we do updates at runtime, then remove
    tables, apply the log and check that this results in the same table as at
    runtime, putting the same LSN as runtime had done will decrease
    differences. So we use the UNDO's LSN which is current_group_end_lsn.
  */
  enlarge_buffer(rec);
  if (log_record_buffer.str == NULL)
  {
    tprint(tracef, "Failed to read allocate buffer for record\n");
    goto end;
  }
  if (translog_read_record(rec->lsn, 0, rec->record_length,
                           log_record_buffer.str, NULL) !=
      rec->record_length)
  {
    tprint(tracef, "Failed to read record\n");
    goto end;
  }
  buff= log_record_buffer.str;
  if (_ma_apply_redo_insert_row_head_or_tail(info, current_group_end_lsn,
                                             HEAD_PAGE,
                                             buff + FILEID_STORE_SIZE,
                                             buff +
                                             FILEID_STORE_SIZE +
                                             PAGE_STORE_SIZE +
                                             DIRPOS_STORE_SIZE,
                                             rec->record_length -
                                             (FILEID_STORE_SIZE +
                                              PAGE_STORE_SIZE +
                                              DIRPOS_STORE_SIZE)))
    goto end;
  error= 0;
end:
  return error;
}


prototype_redo_exec_hook(REDO_INSERT_ROW_TAIL)
{
  int error= 1;
  uchar *buff;
  MARIA_HA *info= get_MARIA_HA_from_REDO_record(rec);
  if (info == NULL)
    return 0;
  enlarge_buffer(rec);
  if (log_record_buffer.str == NULL ||
      translog_read_record(rec->lsn, 0, rec->record_length,
                           log_record_buffer.str, NULL) !=
       rec->record_length)
  {
    tprint(tracef, "Failed to read record\n");
    goto end;
  }
  buff= log_record_buffer.str;
  if (_ma_apply_redo_insert_row_head_or_tail(info, current_group_end_lsn,
                                             TAIL_PAGE,
                                             buff + FILEID_STORE_SIZE,
                                             buff +
                                             FILEID_STORE_SIZE +
                                             PAGE_STORE_SIZE +
                                             DIRPOS_STORE_SIZE,
                                             rec->record_length -
                                             (FILEID_STORE_SIZE +
                                              PAGE_STORE_SIZE +
                                              DIRPOS_STORE_SIZE)))
    goto end;
  error= 0;

end:
  return error;
}


prototype_redo_exec_hook(REDO_PURGE_ROW_HEAD)
{
  int error= 1;
  MARIA_HA *info= get_MARIA_HA_from_REDO_record(rec);
  if (info == NULL)
    return 0;
  if (_ma_apply_redo_purge_row_head_or_tail(info, current_group_end_lsn,
                                            HEAD_PAGE,
                                            rec->header + FILEID_STORE_SIZE))
    goto end;
  error= 0;
end:
  return error;
}


prototype_redo_exec_hook(REDO_PURGE_ROW_TAIL)
{
  int error= 1;
  MARIA_HA *info= get_MARIA_HA_from_REDO_record(rec);
  if (info == NULL)
    return 0;
  if (_ma_apply_redo_purge_row_head_or_tail(info, current_group_end_lsn,
                                            TAIL_PAGE,
                                            rec->header + FILEID_STORE_SIZE))
    goto end;
  error= 0;
end:
  return error;
}


prototype_redo_exec_hook(REDO_PURGE_BLOCKS)
{
  int error= 1;
  uchar *buff;
  MARIA_HA *info= get_MARIA_HA_from_REDO_record(rec);
  if (info == NULL)
    return 0;
  enlarge_buffer(rec);

  if (log_record_buffer.str == NULL ||
      translog_read_record(rec->lsn, 0, rec->record_length,
                           log_record_buffer.str, NULL) !=
       rec->record_length)
  {
    tprint(tracef, "Failed to read record\n");
    goto end;
  }

  buff= log_record_buffer.str;
  if (_ma_apply_redo_purge_blocks(info, current_group_end_lsn,
                                  buff + FILEID_STORE_SIZE))
    goto end;
  error= 0;
end:
  return error;
}


prototype_redo_exec_hook(REDO_DELETE_ALL)
{
  int error= 1;
  MARIA_HA *info= get_MARIA_HA_from_REDO_record(rec);
  if (info == NULL)
    return 0;
  tprint(tracef, "   deleting all %lu rows\n",
         (ulong)info->s->state.state.records);
  if (maria_delete_all_rows(info))
    goto end;
  error= 0;
end:
  return error;
}


#define set_undo_lsn_for_active_trans(TRID, LSN) do {  \
    all_active_trans[TRID].undo_lsn= LSN;                            \
    if (all_active_trans[TRID].first_undo_lsn == LSN_IMPOSSIBLE)   \
      all_active_trans[TRID].first_undo_lsn= LSN; } while (0)

prototype_redo_exec_hook(UNDO_ROW_INSERT)
{
  MARIA_HA *info= get_MARIA_HA_from_UNDO_record(rec);
  if (info == NULL)
    return 0;
  set_undo_lsn_for_active_trans(rec->short_trid, rec->lsn);
  if (cmp_translog_addr(rec->lsn, info->s->state.is_of_horizon) >= 0)
  {
    tprint(tracef, "   state older than record, updating rows' count\n");
    info->s->state.state.records++;
    /** @todo RECOVERY BUG Also update the table's checksum */
    /**
       @todo some bits below will rather be set when executing UNDOs related
       to keys
    */
    info->s->state.changed|= STATE_CHANGED | STATE_NOT_ANALYZED |
      STATE_NOT_OPTIMIZED_KEYS | STATE_NOT_SORTED_PAGES;
  }
  tprint(tracef, "   rows' count %lu\n", (ulong)info->s->state.state.records);
  return 0;
}


prototype_redo_exec_hook(UNDO_ROW_DELETE)
{
  MARIA_HA *info= get_MARIA_HA_from_UNDO_record(rec);
  if (info == NULL)
    return 0;
  set_undo_lsn_for_active_trans(rec->short_trid, rec->lsn);
  if (cmp_translog_addr(rec->lsn, info->s->state.is_of_horizon) >= 0)
  {
    tprint(tracef, "   state older than record, updating rows' count\n");
    info->s->state.state.records--;
    info->s->state.changed|= STATE_CHANGED | STATE_NOT_ANALYZED |
      STATE_NOT_OPTIMIZED_KEYS | STATE_NOT_SORTED_PAGES;
  }
  tprint(tracef, "   rows' count %lu\n", (ulong)info->s->state.state.records);
  return 0;
}


prototype_redo_exec_hook(UNDO_ROW_UPDATE)
{
  MARIA_HA *info= get_MARIA_HA_from_UNDO_record(rec);
  if (info == NULL)
    return 0;
  set_undo_lsn_for_active_trans(rec->short_trid, rec->lsn);
  if (cmp_translog_addr(rec->lsn, info->s->state.is_of_horizon) >= 0)
  {
    info->s->state.changed|= STATE_CHANGED | STATE_NOT_ANALYZED |
      STATE_NOT_OPTIMIZED_KEYS | STATE_NOT_SORTED_PAGES;
  }
  return 0;
}


prototype_redo_exec_hook(COMMIT)
{
  uint16 sid= rec->short_trid;
  TrID long_trid= all_active_trans[sid].long_trid;
  LSN gslsn= all_active_trans[sid].group_start_lsn;
  char llbuf[22];
  if (long_trid == 0)
  {
    tprint(tracef, "We don't know about transaction with short_trid %u;"
           "it probably committed long ago, forget it\n", sid);
    return 0;
  }
  llstr(long_trid, llbuf);
  tprint(tracef, "Transaction long_trid %s short_trid %u committed", llbuf, sid);
  if (gslsn != LSN_IMPOSSIBLE)
  {
    /*
      It's not an error, it may be that trn got a disk error when writing to a
      table, so an unfinished group staid in the log.
    */
    tprint(tracef, ", with group at LSN (%lu,0x%lx) short_trid %u aborted\n",
           LSN_IN_PARTS(gslsn), sid);
    all_active_trans[sid].group_start_lsn= LSN_IMPOSSIBLE;
  }
  else
    tprint(tracef, "\n");
  bzero(&all_active_trans[sid], sizeof(all_active_trans[sid]));
#ifdef MARIA_VERSIONING
  /*
    if real recovery:
    transaction was committed, move it to some separate list for later
    purging (but don't purge now! purging may have been started before, we
    may find REDO_PURGE records soon).
  */
#endif
  return 0;
}


prototype_redo_exec_hook(CLR_END)
{
  MARIA_HA *info= get_MARIA_HA_from_UNDO_record(rec);
  if (info == NULL)
    return 0;
  LSN previous_undo_lsn= lsn_korr(rec->header);
  enum translog_record_type undone_record_type=
    (rec->header)[LSN_STORE_SIZE + FILEID_STORE_SIZE];
  const LOG_DESC *log_desc= &log_record_type_descriptor[undone_record_type];

  set_undo_lsn_for_active_trans(rec->short_trid, previous_undo_lsn);
  tprint(tracef, "   CLR_END was about %s, undo_lsn now LSN (%lu,0x%lx)\n",
         log_desc->name, LSN_IN_PARTS(previous_undo_lsn));
  if (cmp_translog_addr(rec->lsn, info->s->state.is_of_horizon) >= 0)
  {
    tprint(tracef, "   state older than record, updating rows' count\n");
    switch (undone_record_type) {
    case LOGREC_UNDO_ROW_DELETE:
      info->s->state.state.records++;
      break;
    case LOGREC_UNDO_ROW_INSERT:
      info->s->state.state.records--;
      break;
    case LOGREC_UNDO_ROW_UPDATE:
      break;
    default:
      DBUG_ASSERT(0);
    }
    info->s->state.changed|= STATE_CHANGED | STATE_NOT_ANALYZED |
      STATE_NOT_OPTIMIZED_KEYS | STATE_NOT_SORTED_PAGES;
  }
  tprint(tracef, "   rows' count %lu\n", (ulong)info->s->state.state.records);
  return 0;
}


prototype_undo_exec_hook(UNDO_ROW_INSERT)
{
  my_bool error;
  MARIA_HA *info= get_MARIA_HA_from_UNDO_record(rec);
  LSN previous_undo_lsn= lsn_korr(rec->header);

  if (info == NULL)
  {
    /*
      Unlike for REDOs, if the table was skipped it is abnormal; we have a
      transaction to rollback which used this table, as it is not rolled back
      it was supposed to hold this table and so the table should still be
      there.
    */
    return 1;
  }
  info->s->state.changed|= STATE_CHANGED | STATE_NOT_ANALYZED |
    STATE_NOT_OPTIMIZED_KEYS | STATE_NOT_SORTED_PAGES;

  info->trn= trn;
  error= _ma_apply_undo_row_insert(info, previous_undo_lsn,
                                   rec->header + LSN_STORE_SIZE +
                                   FILEID_STORE_SIZE);
  info->trn= 0;
  /* trn->undo_lsn is updated in an inwrite_hook when writing the CLR_END */
  tprint(tracef, "   rows' count %lu\n", (ulong)info->s->state.state.records);
  tprint(tracef, "   undo_lsn now LSN (%lu,0x%lx)\n",
         LSN_IN_PARTS(previous_undo_lsn));
  return error;
}


prototype_undo_exec_hook(UNDO_ROW_DELETE)
{
  my_bool error;
  MARIA_HA *info= get_MARIA_HA_from_UNDO_record(rec);
  LSN previous_undo_lsn= lsn_korr(rec->header);

  if (info == NULL)
    return 1;

  info->s->state.changed|= STATE_CHANGED | STATE_NOT_ANALYZED |
    STATE_NOT_OPTIMIZED_KEYS | STATE_NOT_SORTED_PAGES;

  enlarge_buffer(rec);
  if (log_record_buffer.str == NULL ||
      translog_read_record(rec->lsn, 0, rec->record_length,
                           log_record_buffer.str, NULL) !=
       rec->record_length)
  {
    tprint(tracef, "Failed to read record\n");
    return 1;
  }

  info->trn= trn;
  /*
    For now we skip the page and directory entry. This is to be used
    later when we mark rows as deleted.
  */
  error= _ma_apply_undo_row_delete(info, previous_undo_lsn,
                                   log_record_buffer.str + LSN_STORE_SIZE +
                                   FILEID_STORE_SIZE + PAGE_STORE_SIZE +
                                   DIRPOS_STORE_SIZE,
                                   rec->record_length -
                                   (LSN_STORE_SIZE + FILEID_STORE_SIZE +
                                    PAGE_STORE_SIZE + DIRPOS_STORE_SIZE));
  info->trn= 0;
  tprint(tracef, "   rows' count %lu\n   undo_lsn now LSN (%lu,0x%lx)\n",
         (ulong)info->s->state.state.records,
         LSN_IN_PARTS(previous_undo_lsn));
  return error;
}


prototype_undo_exec_hook(UNDO_ROW_UPDATE)
{
  my_bool error;
  MARIA_HA *info= get_MARIA_HA_from_UNDO_record(rec);
  LSN previous_undo_lsn= lsn_korr(rec->header);

  if (info == NULL)
    return 1;

  info->s->state.changed|= STATE_CHANGED | STATE_NOT_ANALYZED |
    STATE_NOT_OPTIMIZED_KEYS | STATE_NOT_SORTED_PAGES;

  enlarge_buffer(rec);
  if (log_record_buffer.str == NULL ||
      translog_read_record(rec->lsn, 0, rec->record_length,
                           log_record_buffer.str, NULL) !=
       rec->record_length)
  {
    tprint(tracef, "Failed to read record\n");
    return 1;
  }

  info->trn= trn;
  error= _ma_apply_undo_row_update(info, previous_undo_lsn,
                                   log_record_buffer.str + LSN_STORE_SIZE +
                                   FILEID_STORE_SIZE,
                                   rec->record_length -
                                   (LSN_STORE_SIZE + FILEID_STORE_SIZE));
  info->trn= 0;
  tprint(tracef, "   undo_lsn now LSN (%lu,0x%lx)\n",
         LSN_IN_PARTS(previous_undo_lsn));
  return error;
}


static int run_redo_phase(LSN lsn, my_bool apply)
{
  /* install hooks for execution */
#define install_redo_exec_hook(R)                                        \
  log_record_type_descriptor[LOGREC_ ## R].record_execute_in_redo_phase= \
    exec_REDO_LOGREC_ ## R;
#define install_undo_exec_hook(R)                                        \
  log_record_type_descriptor[LOGREC_ ## R].record_execute_in_undo_phase= \
    exec_UNDO_LOGREC_ ## R;
  install_redo_exec_hook(LONG_TRANSACTION_ID);
  install_redo_exec_hook(CHECKPOINT);
  install_redo_exec_hook(REDO_CREATE_TABLE);
  install_redo_exec_hook(REDO_RENAME_TABLE);
  install_redo_exec_hook(REDO_REPAIR_TABLE);
  install_redo_exec_hook(REDO_DROP_TABLE);
  install_redo_exec_hook(FILE_ID);
  install_redo_exec_hook(REDO_INSERT_ROW_HEAD);
  install_redo_exec_hook(REDO_INSERT_ROW_TAIL);
  install_redo_exec_hook(REDO_PURGE_ROW_HEAD);
  install_redo_exec_hook(REDO_PURGE_ROW_TAIL);
  install_redo_exec_hook(REDO_PURGE_BLOCKS);
  install_redo_exec_hook(REDO_DELETE_ALL);
  install_redo_exec_hook(UNDO_ROW_INSERT);
  install_redo_exec_hook(UNDO_ROW_DELETE);
  install_redo_exec_hook(UNDO_ROW_UPDATE);
  install_redo_exec_hook(COMMIT);
  install_redo_exec_hook(CLR_END);
  install_undo_exec_hook(UNDO_ROW_INSERT);
  install_undo_exec_hook(UNDO_ROW_DELETE);
  install_undo_exec_hook(UNDO_ROW_UPDATE);

  current_group_end_lsn= LSN_IMPOSSIBLE;

  TRANSLOG_HEADER_BUFFER rec;

  if (unlikely(lsn == LSN_IMPOSSIBLE || lsn == translog_get_horizon()))
  {
    tprint(tracef, "checkpoint address refers to the log end log or "
           "log is empty, nothing to do.\n");
    return 0;
  }

  int len= translog_read_record_header(lsn, &rec);

  /** @todo EOF should be detected */
  if (len == RECHEADER_READ_ERROR)
  {
    tprint(tracef, "Failed to read header of the first record.\n");
    return 1;
  }
  struct st_translog_scanner_data scanner;
  if (translog_init_scanner(lsn, 1, &scanner))
  {
    tprint(tracef, "Scanner init failed\n");
    return 1;
  }
  uint i;
  for (i= 1;;i++)
  {
    uint16 sid= rec.short_trid;
    const LOG_DESC *log_desc= &log_record_type_descriptor[rec.type];
    display_record_position(log_desc, &rec, i);
    /*
      A complete group is a set of log records with an "end mark" record
      (e.g. a set of REDOs for an operation, terminated by an UNDO for this
      operation); if there is no "end mark" record the group is incomplete
      and won't be executed.
    */
    if ((log_desc->record_in_group == LOGREC_IS_GROUP_ITSELF) ||
        (log_desc->record_in_group == LOGREC_LAST_IN_GROUP))
    {
      if (all_active_trans[sid].group_start_lsn != LSN_IMPOSSIBLE)
      {
        if (log_desc->record_in_group == LOGREC_IS_GROUP_ITSELF)
        {
          /*
            can happen if the transaction got a table write error, then
            unlocked tables thus wrote a COMMIT record.
          */
          tprint(tracef, "\nDiscarding unfinished group before this record\n");
          ALERT_USER();
          all_active_trans[sid].group_start_lsn= LSN_IMPOSSIBLE;
        }
        else
        {
          /*
            There is a complete group for this transaction, containing more
            than this event.
          */
          tprint(tracef, "   ends a group:\n");
          struct st_translog_scanner_data scanner2;
          TRANSLOG_HEADER_BUFFER rec2;
          len=
            translog_read_record_header(all_active_trans[sid].group_start_lsn, &rec2);
          if (len < 0) /* EOF or error */
          {
            tprint(tracef, "Cannot find record where it should be\n");
            return 1;
          }
          if (translog_init_scanner(rec2.lsn, 1, &scanner2))
          {
            tprint(tracef, "Scanner2 init failed\n");
            return 1;
          }
          current_group_end_lsn= rec.lsn;
          do
          {
            if (rec2.short_trid == sid) /* it's in our group */
            {
              const LOG_DESC *log_desc2= &log_record_type_descriptor[rec2.type];
              display_record_position(log_desc2, &rec2, 0);
              if (apply && display_and_apply_record(log_desc2, &rec2))
                return 1;
            }
            len= translog_read_next_record_header(&scanner2, &rec2);
            if (len < 0) /* EOF or error */
            {
              tprint(tracef, "Cannot find record where it should be\n");
              return 1;
            }
          }
          while (rec2.lsn < rec.lsn);
          translog_free_record_header(&rec2);
          /* group finished */
          all_active_trans[sid].group_start_lsn= LSN_IMPOSSIBLE;
          current_group_end_lsn= LSN_IMPOSSIBLE; /* for debugging */
          display_record_position(log_desc, &rec, 0);
        }
      }
      if (apply && display_and_apply_record(log_desc, &rec))
        return 1;
    }
    else /* record does not end group */
    {
      /* just record the fact, can't know if can execute yet */
      if (all_active_trans[sid].group_start_lsn == LSN_IMPOSSIBLE)
      {
        /* group not yet started */
        all_active_trans[sid].group_start_lsn= rec.lsn;
      }
    }
    len= translog_read_next_record_header(&scanner, &rec);
    if (len < 0)
    {
      switch (len)
      {
      case RECHEADER_READ_EOF:
        tprint(tracef, "EOF on the log\n");
        break;
      case RECHEADER_READ_ERROR:
        tprint(tracef, "Error reading log\n");
        return 1;
      }
      break;
    }
  }
  translog_free_record_header(&rec);
  return 0;
}


/**
   @brief Informs about any aborted groups or unfinished transactions,
   prepares for the UNDO phase if needed.

   @param  prepare_for_undo_phase

   @note Observe that it may init trnman.
*/
static uint end_of_redo_phase(my_bool prepare_for_undo_phase)
{
  uint sid, unfinished= 0;
  char llbuf[22];

  hash_free(&all_dirty_pages);
  /*
    hash_free() can be called multiple times probably, but be safe it that
    changes
  */
  bzero(&all_dirty_pages, sizeof(all_dirty_pages));
  my_free(dirty_pages_pool, MYF(MY_ALLOW_ZERO_PTR));
  dirty_pages_pool= NULL;

  llstr(max_long_trid, llbuf);
  tprint(tracef, "Maximum transaction long id seen: %s\n", llbuf);
  if (prepare_for_undo_phase && trnman_init(max_long_trid))
    return -1;

  for (sid= 0; sid <= SHORT_TRID_MAX; sid++)
  {
    TrID long_trid= all_active_trans[sid].long_trid;
    LSN gslsn= all_active_trans[sid].group_start_lsn;
    TRN *trn;
    if (gslsn != LSN_IMPOSSIBLE)
    {
      tprint(tracef, "Group at LSN (%lu,0x%lx) short_trid %u aborted\n",
             LSN_IN_PARTS(gslsn), sid);
      ALERT_USER();
    }
    if (all_active_trans[sid].undo_lsn != LSN_IMPOSSIBLE)
    {
      char llbuf[22];
      llstr(long_trid, llbuf);
      tprint(tracef, "Transaction long_trid %s short_trid %u unfinished\n",
             llbuf, sid);
      /* dummy_transaction_object serves only for DDLs */
      DBUG_ASSERT(long_trid != 0);
      if (prepare_for_undo_phase)
      {
        if ((trn= trnman_recreate_trn_from_recovery(sid, long_trid)) == NULL)
          return -1;
        trn->undo_lsn= all_active_trans[sid].undo_lsn;
        trn->first_undo_lsn= all_active_trans[sid].first_undo_lsn |
          TRANSACTION_LOGGED_LONG_ID; /* because trn is known in log */
      }
      /* otherwise we will just warn about it */
      unfinished++;
    }
#ifdef MARIA_VERSIONING
    /*
      If real recovery: if transaction was committed, move it to some separate
      list for soon purging.
    */
#endif
  }

  my_free(all_active_trans, MYF(MY_ALLOW_ZERO_PTR));
  all_active_trans= NULL;

  /*
    The UNDO phase uses some normal run-time code of ROLLBACK: generates log
    records, etc; prepare tables for that
  */
  LSN addr= translog_get_horizon();
  for (sid= 0; sid <= SHARE_ID_MAX; sid++)
  {
    MARIA_HA *info= all_tables[sid].info;
    if (info != NULL)
    {
      prepare_table_for_close(info, addr);
      /*
        But we don't close it; we leave it available for the UNDO phase;
        it's likely that the UNDO phase will need it.
      */
      if (prepare_for_undo_phase)
        translog_assign_id_to_share_from_recovery(info->s, sid);
    }
  }

#if 0 /* will be enabled soon */
  if (prepare_for_undo_phase)
  {
    /*
      We take a checkpoint as it can save future recovery work if we crash
      soon. But we don't flush pages, as UNDOs would change them again
      probably.
    */
    if (ma_checkpoint_init(FALSE))
      return -1;
    int res= ma_checkpoint_execute(CHECKPOINT_INDIRECT, FALSE);
    ma_checkpoint_end();
    if (res)
      unfinished= -1;
  }
#endif

  return unfinished;
}


static int run_undo_phase(uint unfinished)
{
  if (unfinished > 0)
  {
    if (tracef != stdout)
    {
      /** @todo RECOVERY BUG all prints to stderr should go to error log */
      fprintf(stderr, " 100%%; transactions to roll back:");
    }
    tprint(tracef, "%u transactions will be rolled back\n", unfinished);
    for( ; ; )
    {
      if (tracef != stdout)
        fprintf(stderr, " %u", unfinished);
      if ((unfinished--) == 0)
        break;
      char llbuf[22];
      TRN *trn= trnman_get_any_trn();
      DBUG_ASSERT(trn != NULL);
      llstr(trn->trid, llbuf);
      tprint(tracef, "Rolling back transaction of long id %s\n", llbuf);

      /* Execute all undo entries */
      while (trn->undo_lsn)
      {
        TRANSLOG_HEADER_BUFFER rec;
        LOG_DESC *log_desc;
        if (translog_read_record_header(trn->undo_lsn, &rec) ==
            RECHEADER_READ_ERROR)
          return 1;
        log_desc= &log_record_type_descriptor[rec.type];
        display_record_position(log_desc, &rec, 0);
        if (log_desc->record_execute_in_undo_phase(&rec, trn))
        {
          tprint(tracef, "Got error when executing undo\n");
          return 1;
        }
      }

      if (trnman_rollback_trn(trn))
        return 1;
      /* We could want to span a few threads (4?) instead of 1 */
      /* In the future, we want to have this phase *online* */
    }
  }
  return 0;
}


/**
   @brief re-enables transactionality, updates is_of_horizon

   @param  info                table
   @param  horizon             address to set is_of_horizon
*/

static void prepare_table_for_close(MARIA_HA *info, TRANSLOG_ADDRESS horizon)
{
  MARIA_SHARE *share= info->s;
  /*
    In a fully-forward REDO phase (no checkpoint record),
    state is now at least as new as the LSN of the current record. It may be
    newer, in case we are seeing a LOGREC_FILE_ID which tells us to close a
    table, but that table was later modified further in the log.
    But if we parsed a checkpoint record, it may be this way in the log:
    FILE_ID(6->t2)... FILE_ID(6->t1)... CHECKPOINT(6->t1)
    Checkpoint parsing opened t1 with id 6; first FILE_ID above is going to
    make t1 close; the first condition below is however false (when checkpoint
    was taken it increased is_of_horizon) and so it works. For safety we
    add the second condition.
  */
  if (cmp_translog_addr(share->state.is_of_horizon, horizon) < 0 &&
      cmp_translog_addr(share->lsn_of_file_id, horizon) < 0)
    share->state.is_of_horizon= horizon;
  _ma_reenable_logging_for_table(share);
}


static MARIA_HA *get_MARIA_HA_from_REDO_record(const
                                               TRANSLOG_HEADER_BUFFER *rec)
{
  uint16 sid;
  pgcache_page_no_t page;
  MARIA_HA *info;
  char llbuf[22];

  print_redo_phase_progress(rec->lsn);
  sid= fileid_korr(rec->header);
  page= page_korr(rec->header + FILEID_STORE_SIZE);
  /**
     @todo RECOVERY BUG
     - for REDO_PURGE_BLOCKS, page is not at this pos
     - for DELETE_ALL, record ends here! buffer overrun!
     Solution: caller should pass a param enum { i_am_about_data_file,
     i_am_about_index_file, none }.
  */
  llstr(page, llbuf);
  tprint(tracef, "   For page %s of table of short id %u", llbuf, sid);
  info= all_tables[sid].info;
  if (info == NULL)
  {
    tprint(tracef, ", table skipped, so skipping record\n");
    return NULL;
  }
  tprint(tracef, ", '%s'", info->s->open_file_name);
  if (cmp_translog_addr(rec->lsn, info->s->lsn_of_file_id) <= 0)
  {
    /*
      This can happen only if processing a record before the checkpoint
      record.
      id->name mapping is newer than REDO record: for sure the table subject
      of the REDO has been flushed and forced (id re-assignment implies this);
      REDO can be ignored (and must be, as we don't know what this subject
      table was).
    */
    DBUG_ASSERT(cmp_translog_addr(rec->lsn, checkpoint_start) < 0);
    tprint(tracef, ", table's LOGREC_FILE_ID has LSN (%lu,0x%lx) more recent"
           " than record, skipping record",
           LSN_IN_PARTS(info->s->lsn_of_file_id));
    return NULL;
  }
  /* detect if an open instance of a dropped table (internal bug) */
  DBUG_ASSERT(info->s->last_version != 0);
  if (cmp_translog_addr(rec->lsn, checkpoint_start) < 0)
  {
    /**
       @todo RECOVERY BUG always assuming this is REDO for data file, but it
       could soon be index file
    */
    uint64 file_and_page_id=
      (((uint64)all_tables[sid].org_dfile) << 32) | page;
    struct st_dirty_page *dirty_page= (struct st_dirty_page *)
      hash_search(&all_dirty_pages,
                  (uchar *)&file_and_page_id, sizeof(file_and_page_id));
    if ((dirty_page == NULL) ||
        cmp_translog_addr(rec->lsn, dirty_page->rec_lsn) < 0)
    {
      tprint(tracef, ", ignoring because of dirty_pages list\n");
      return NULL;
    }
  }

  /*
    So we are going to read the page, and if its LSN is older than the
    record's we will modify the page
  */
  tprint(tracef, ", applying record\n");
  _ma_writeinfo(info, WRITEINFO_UPDATE_KEYFILE); /* to flush state on close */
  return info;
}


static MARIA_HA *get_MARIA_HA_from_UNDO_record(const
                                               TRANSLOG_HEADER_BUFFER *rec)
{
  uint16 sid;
  MARIA_HA *info;

  sid= fileid_korr(rec->header + LSN_STORE_SIZE);
  tprint(tracef, "   For table of short id %u", sid);
  info= all_tables[sid].info;
  if (info == NULL)
  {
    tprint(tracef, ", table skipped, so skipping record\n");
    return NULL;
  }
  tprint(tracef, ", '%s'", info->s->open_file_name);
  if (cmp_translog_addr(rec->lsn, info->s->lsn_of_file_id) <= 0)
  {
    tprint(tracef, ", table's LOGREC_FILE_ID has LSN (%lu,0x%lx) more recent"
           " than record, skipping record",
           LSN_IN_PARTS(info->s->lsn_of_file_id));
    return NULL;
  }
  DBUG_ASSERT(info->s->last_version != 0);
  _ma_writeinfo(info, WRITEINFO_UPDATE_KEYFILE); /* to flush state on close */
  tprint(tracef, ", applying record\n");
  return info;
}


/**
   @brief Parses checkpoint record.

   Builds from it the dirty_pages list (a hash), opens tables and maps them to
   their 2-byte IDs,  recreates transactions (not real TRNs though).

   @return From where in the log the REDO phase should start
     @retval LSN_IMPOSSIBLE error
     @retval other          ok
*/

static LSN parse_checkpoint_record(LSN lsn)
{
  uint i;
  TRANSLOG_HEADER_BUFFER rec;

  tprint(tracef, "Loading data from checkpoint record at LSN (%lu,0x%lx)\n",
         LSN_IN_PARTS(lsn));
  int len= translog_read_record_header(lsn, &rec);

  if (len == RECHEADER_READ_ERROR)
  {
    tprint(tracef, "Cannot find checkpoint record where it should be\n");
    return LSN_IMPOSSIBLE;
  }

  enlarge_buffer(&rec);
  if (log_record_buffer.str == NULL ||
      translog_read_record(rec.lsn, 0, rec.record_length,
                           log_record_buffer.str, NULL) !=
      rec.record_length)
  {
    tprint(tracef, "Failed to read record\n");
    return LSN_IMPOSSIBLE;
  }

  char *ptr= log_record_buffer.str;
  checkpoint_start= lsn_korr(ptr);
  ptr+= LSN_STORE_SIZE;

  /* transactions */
  uint nb_active_transactions= uint2korr(ptr);
  ptr+= 2;
  tprint(tracef, "%u active transactions\n", nb_active_transactions);
  LSN minimum_rec_lsn_of_active_transactions= lsn_korr(ptr);
  ptr+= LSN_STORE_SIZE;

  /*
    how much brain juice and discussions there was to come to writing this
    line
  */
  set_if_smaller(checkpoint_start, minimum_rec_lsn_of_active_transactions);

  for (i= 0; i < nb_active_transactions; i++)
  {
    uint16 sid= uint2korr(ptr);
    ptr+= 2;
    TrID long_id= uint6korr(ptr);
    ptr+= 6;
    DBUG_ASSERT(sid > 0 && long_id > 0);
    LSN undo_lsn= lsn_korr(ptr);
    ptr+= LSN_STORE_SIZE;
    LSN first_undo_lsn= lsn_korr(ptr);
    ptr+= LSN_STORE_SIZE;
    new_transaction(sid, long_id, undo_lsn, first_undo_lsn);
  }
  uint nb_committed_transactions= uint4korr(ptr);
  ptr+= 4;
  tprint(tracef, "%lu committed transactions\n",
         (ulong)nb_committed_transactions);
  /* no purging => committed transactions are not important */
  ptr+= (6 + LSN_STORE_SIZE) * nb_committed_transactions;

  /* tables  */
  uint nb_tables= uint4korr(ptr);
  ptr+= 4;
  tprint(tracef, "%u open tables\n", nb_tables);
  for (i= 0; i< nb_tables; i++)
  {
    char name[FN_REFLEN];
    uint16 sid= uint2korr(ptr);
    ptr+= 2;
    DBUG_ASSERT(sid > 0);
    File kfile= uint4korr(ptr);
    ptr+= 4;
    File dfile= uint4korr(ptr);
    ptr+= 4;
    LSN first_log_write_lsn= lsn_korr(ptr);
    ptr+= LSN_STORE_SIZE;
    uint name_len= strlen(ptr) + 1;
    ptr+= name_len;
    strnmov(name, ptr, sizeof(name));
    if (new_table(sid, name, kfile, dfile, first_log_write_lsn))
      return LSN_IMPOSSIBLE;
  }

  /* dirty pages */
  uint nb_dirty_pages= uint4korr(ptr);
  ptr+= 4;
  tprint(tracef, "%u dirty pages\n", nb_dirty_pages);
  if (hash_init(&all_dirty_pages, &my_charset_bin, nb_dirty_pages,
                offsetof(struct st_dirty_page, file_and_page_id),
                sizeof(((struct st_dirty_page *)NULL)->file_and_page_id),
                NULL, NULL, 0))
    return LSN_IMPOSSIBLE;
  dirty_pages_pool=
    (struct st_dirty_page *)my_malloc(nb_dirty_pages *
                                      sizeof(struct st_dirty_page),
                                      MYF(MY_WME));
  if (unlikely(dirty_pages_pool == NULL))
    return LSN_IMPOSSIBLE;
  struct st_dirty_page *next_dirty_page_in_pool= dirty_pages_pool;
  LSN minimum_rec_lsn_of_dirty_pages= LSN_MAX;
  for (i= 0; i < nb_dirty_pages ; i++)
  {
    File fileid= uint4korr(ptr);
    ptr+= 4;
    pgcache_page_no_t pageid= uint4korr(ptr);
    ptr+= 4;
    LSN rec_lsn= lsn_korr(ptr);
    ptr+= LSN_STORE_SIZE;
    if (new_page(fileid, pageid, rec_lsn, next_dirty_page_in_pool++))
      return LSN_IMPOSSIBLE;
    set_if_smaller(minimum_rec_lsn_of_dirty_pages, rec_lsn);
  }
  /* after that, there will be no insert/delete into the hash */
  /*
    sanity check on record (did we screw up with all those "ptr+=", did the
    checkpoint write code and checkpoint read code go out of sync?).
  */
  if (ptr != (log_record_buffer.str + log_record_buffer.length))
  {
    tprint(tracef, "checkpoint record corrupted\n");
    return LSN_IMPOSSIBLE;
  }
  set_if_smaller(checkpoint_start, minimum_rec_lsn_of_dirty_pages);

  return checkpoint_start;
}

static int new_page(File fileid, pgcache_page_no_t pageid, LSN rec_lsn,
                    struct st_dirty_page *dirty_page)
{
  /* serves as hash key */
  dirty_page->file_and_page_id= (((uint64)fileid) << 32) | pageid;
  dirty_page->rec_lsn= rec_lsn;
  return my_hash_insert(&all_dirty_pages, (uchar *)dirty_page);
}


static int close_all_tables(void)
{
  int error= 0;
  LIST *list_element, *next_open;
  MARIA_HA *info;
  pthread_mutex_lock(&THR_LOCK_maria);
  if (maria_open_list == NULL)
    goto end;
  tprint(tracef, "Closing all tables\n");
  if (tracef != stdout && redo_phase_message_printed)
  {
    /** @todo RECOVERY BUG all prints to stderr should go to error log */
    fprintf(stderr, "; flushing tables");
  }

  /*
    Since the end of end_of_redo_phase(), we may have written new records
    (if UNDO phase ran)  and thus the state is newer than at
    end_of_redo_phase(), we need to bump is_of_horizon again.
  */
  TRANSLOG_ADDRESS addr= translog_get_horizon();
  for (list_element= maria_open_list ; list_element ; list_element= next_open)
  {
    next_open= list_element->next;
    info= (MARIA_HA*)list_element->data;
    pthread_mutex_unlock(&THR_LOCK_maria); /* ok, UNDO phase not online yet */
    prepare_table_for_close(info, addr);
    error|= maria_close(info);
    pthread_mutex_lock(&THR_LOCK_maria);
  }
end:
  pthread_mutex_unlock(&THR_LOCK_maria);
  return error;
}

static void print_redo_phase_progress(TRANSLOG_ADDRESS addr)
{
  static int end_logno= FILENO_IMPOSSIBLE, end_offset, percentage_printed= 0;
  static ulonglong initial_remainder= -1;
  if (tracef == stdout)
    return;
  if (!redo_phase_message_printed)
  {
    /** @todo RECOVERY BUG all prints to stderr should go to error log */
    fprintf(stderr, "Maria engine: starting recovery; recovered pages: 0%%");
    redo_phase_message_printed= TRUE;
  }
  if (end_logno == FILENO_IMPOSSIBLE)
  {
    LSN end_addr= translog_get_horizon();
    end_logno= LSN_FILE_NO(end_addr);
    end_offset= LSN_OFFSET(end_addr);
  }
  int cur_logno= LSN_FILE_NO(addr);
  int cur_offset= LSN_OFFSET(addr);
  ulonglong remainder;
  remainder= (cur_logno == end_logno) ? (end_offset - cur_offset) :
    (TRANSLOG_FILE_SIZE - cur_offset +
     max(end_logno - cur_logno - 1, 0) * TRANSLOG_FILE_SIZE + end_offset);
  if (initial_remainder == (ulonglong)(-1))
    initial_remainder= remainder;
  int percentage_done=
    (initial_remainder - remainder) * ULL(100) / initial_remainder;
  if ((percentage_done - percentage_printed) >= 10)
  {
    percentage_printed= percentage_done;
    fprintf(stderr, " %d%%", percentage_done);
  }
}

#ifdef MARIA_EXTERNAL_LOCKING
#error Maria's Checkpoint and Recovery are really not ready for it
#endif

/*
Recovery of the state :  how it works
=====================================

Here we ignore Checkpoints for a start.

The state (MARIA_HA::MARIA_SHARE::MARIA_STATE_INFO) is updated in
memory frequently (at least at every row write/update/delete) but goes
to disk at few moments: maria_close() when closing the last open
instance, and a few rare places like CHECK/REPAIR/ALTER
(non-transactional tables also do it at maria_lock_database() but we
needn't cover them here).

In case of crash, state on disk is likely to be older than what it was
in memory, the REDO phase needs to recreate the state as it was in
memory at the time of crash. When we say Recovery here we will always
mean "REDO phase".

For example MARIA_STATUS_INFO::records (count of records). It is updated at
the end of every row write/update/delete/delete_all. When Recovery sees the
sign of such row operation (UNDO or REDO), it may need to update the records'
count if that count does not reflect that operation (is older). How to know
the age of the state compared to the log record: every time the state
goes to disk at runtime, its member "is_of_horizon" is updated to the
current end-of-log horizon. So Recovery just needs to compare is_of_horizon
and the record's LSN to know if it should modify "records".

Other operations like ALTER TABLE DISABLE KEYS update the state but
don't write log records, thus the REDO phase cannot repeat their
effect on the state in case of crash. But we make them sync the state
as soon as they have finished. This reduces the window for a problem.

It looks like only one thread at a time updates the state in memory or
on disk. However there is not 100% certainty when it comes to
HA_EXTRA_(FORCE_REOPEN|PREPARE_FOR_RENAME): can they read the state
from memory while some other thread is updating "records" in memory?
If yes, they may write a corrupted state to disk.
We assume that no for now: ASK_MONTY.

With checkpoints
================

Checkpoint module needs to read the state in memory and write it to
disk. This may happen while some other thread is modifying the state
in memory or on disk. Checkpoint thus may be reading changing data, it
needs a mutex to not have it corrupted, and concurrent modifiers of
the state need that mutex too for the same reason.
"records" is modified for every row write/update/delete, we don't want
to add a mutex lock/unlock there. So we re-use the mutex lock/unlock
which is already present in these moments, namely the log's mutex which is
taken when UNDO_ROW_INSERT|UPDATE|DELETE is written: we update "records" in
under-log-mutex hooks when writing these records (thus "records" is
not updated at the end of maria_write/update/delete() anymore).
Thus Checkpoint takes the log's lock and can read "records" from
memory an write it to disk and release log's lock.
We however want to avoid having the disk write under the log's
lock. So it has to be under another mutex, natural choice is
intern_lock (as Checkpoint needs it anyway to read MARIA_SHARE::kfile,
and as maria_close() takes it too). All state writes to disk are
changed to be protected with intern_lock.
So Checkpoint takes intern_lock, log's lock, reads "records" from
memory, releases log's lock, updates is_of_horizon and writes "records" to
disk, release intern_lock.
In practice, not only "records" needs to be written but the full
state. So, Checkpoint reads the full state from memory. Some other
thread may at this moment be modifying in memory some pieces of the
state which are not protected by the lock's log (see ma_extra.c
HA_EXTRA_NO_KEYS), and Checkpoint would be reading a corrupted state
from memory; to guard against that we extend the intern_lock-zone to
changes done to the state in memory by HA_EXTRA_NO_KEYS et al, and
also any change made in memory to create_rename_lsn/state_is_of_horizon.
Last, we don't want in Checkpoint to do
 log lock; read state from memory; release log lock;
for each table, it may hold the log's lock too much in total.
So, we instead do
 log lock; read N states from memory; release log lock;
Thus, the sequence above happens outside of any intern_lock.
But this re-introduces the problem that some other thread may be changing the
state in memory and on disk under intern_lock, without log's lock, like
HA_EXTRA_NO_KEYS, while we read the N states. However, when Checkpoint later
comes to handling the table under intern_lock, which is serialized with
HA_EXTRA_NO_KEYS, it can see that is_of_horizon is higher then when the state
was read from memory under log's lock, and thus can decide to not flush the
obsolete state it has, knowing that the other thread flushed a more recent
state already. If on the other hand is_of_horizon is not higher, the read
state is current and can be flushed. So we have a per-table sequence:
 lock intern_lock; test if is_of_horizon is higher than when we read the state
 under log's lock; if no then flush the read state to disk.
*/

/* some comments and pseudo-code which we keep for later */
#if 0
  /*
    MikaelR suggests: support checkpoints during REDO phase too: do checkpoint
    after a certain amount of log records have been executed. This helps
    against repeated crashes. Those checkpoints could not be user-requested
    (as engine is not communicating during the REDO phase), so they would be
    automatic: this changes the original assumption that we don't write to the
    log while in the REDO phase, but why not. How often should we checkpoint?
  */

  /*
    We want to have two steps:
    engine->recover_with_max_memory();
    next_engine->recover_with_max_memory();
    engine->init_with_normal_memory();
    next_engine->init_with_normal_memory();
    So: in recover_with_max_memory() allocate a giant page cache, do REDO
    phase, then all page cache is flushed and emptied and freed (only retain
    small structures like TM): take full checkpoint, which is useful if
    next engine crashes in its recovery the next second.
    Destroy all shares (maria_close()), then at init_with_normal_memory() we
    do this:
  */

  /**** UNDO PHASE *****/

  /*
    Launch one or more threads to do the background rollback. Don't wait for
    them to complete their rollback (background rollback; for debugging, we
    can have an option which waits). Set a counter (total_of_rollback_threads)
    to the number of threads to lauch.

    Note that InnoDB's rollback-in-background works as long as InnoDB is the
    last engine to recover, otherwise MySQL will refuse new connections until
    the last engine has recovered so it's not "background" from the user's
    point of view. InnoDB is near top of sys_table_types so all others
    (e.g. BDB) recover after it... So it's really "online rollback" only if
    InnoDB is the only engine.
  */

  /* wake up delete/update handler */
  /* tell the TM that it can now accept new transactions */

  /*
    mark that checkpoint requests are now allowed.
  */
#endif
