/* Copyright (c) 2000, 2012, Oracle and/or its affiliates.
   Copyright (c) 2008, 2012, Monty Program Ab

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#include "sql_priv.h"
#include "unireg.h"
#include "sql_base.h"
#include "sql_parse.h"                          // check_access
#ifdef HAVE_REPLICATION

#include "rpl_mi.h"
#include "rpl_rli.h"
#include "sql_repl.h"
#include "sql_acl.h"                            // SUPER_ACL
#include "log_event.h"
#include "rpl_filter.h"
#include <my_dir.h>
#include "rpl_handler.h"
#include "debug_sync.h"

int max_binlog_dump_events = 0; // unlimited
my_bool opt_sporadic_binlog_dump_fail = 0;
#ifndef DBUG_OFF
static int binlog_dump_count = 0;
#endif

extern TYPELIB binlog_checksum_typelib;

/*
    fake_rotate_event() builds a fake (=which does not exist physically in any
    binlog) Rotate event, which contains the name of the binlog we are going to
    send to the slave (because the slave may not know it if it just asked for
    MASTER_LOG_FILE='', MASTER_LOG_POS=4).
    < 4.0.14, fake_rotate_event() was called only if the requested pos was 4.
    After this version we always call it, so that a 3.23.58 slave can rely on
    it to detect if the master is 4.0 (and stop) (the _fake_ Rotate event has
    zeros in the good positions which, by chance, make it possible for the 3.23
    slave to detect that this event is unexpected) (this is luck which happens
    because the master and slave disagree on the size of the header of
    Log_event).

    Relying on the event length of the Rotate event instead of these
    well-placed zeros was not possible as Rotate events have a variable-length
    part.
*/

static int fake_rotate_event(NET* net, String* packet, char* log_file_name,
                             ulonglong position, const char** errmsg,
                             uint8 checksum_alg_arg)
{
  DBUG_ENTER("fake_rotate_event");
  char header[LOG_EVENT_HEADER_LEN], buf[ROTATE_HEADER_LEN+100];

  /*
    this Rotate is to be sent with checksum if and only if
    slave's get_master_version_and_clock time handshake value 
    of master's @@global.binlog_checksum was TRUE
  */

  my_bool do_checksum= checksum_alg_arg != BINLOG_CHECKSUM_ALG_OFF &&
    checksum_alg_arg != BINLOG_CHECKSUM_ALG_UNDEF;

  /*
    'when' (the timestamp) is set to 0 so that slave could distinguish between
    real and fake Rotate events (if necessary)
  */
  memset(header, 0, 4);
  header[EVENT_TYPE_OFFSET] = ROTATE_EVENT;

  char* p = log_file_name+dirname_length(log_file_name);
  uint ident_len = (uint) strlen(p);
  ulong event_len = ident_len + LOG_EVENT_HEADER_LEN + ROTATE_HEADER_LEN +
    (do_checksum ? BINLOG_CHECKSUM_LEN : 0);
  int4store(header + SERVER_ID_OFFSET, global_system_variables.server_id);
  int4store(header + EVENT_LEN_OFFSET, event_len);
  int2store(header + FLAGS_OFFSET, LOG_EVENT_ARTIFICIAL_F);

  // TODO: check what problems this may cause and fix them
  int4store(header + LOG_POS_OFFSET, 0);

  packet->append(header, sizeof(header));
  int8store(buf+R_POS_OFFSET,position);
  packet->append(buf, ROTATE_HEADER_LEN);
  packet->append(p, ident_len);

  if (do_checksum)
  {
    char b[BINLOG_CHECKSUM_LEN];
    ha_checksum crc= my_checksum(0L, NULL, 0);
    crc= my_checksum(crc, (uchar*)header, sizeof(header));
    crc= my_checksum(crc, (uchar*)buf, ROTATE_HEADER_LEN);
    crc= my_checksum(crc, (uchar*)p, ident_len);
    int4store(b, crc);
    packet->append(b, sizeof(b));
  }

  if (my_net_write(net, (uchar*) packet->ptr(), packet->length()))
  {
    *errmsg = "failed on my_net_write()";
    DBUG_RETURN(-1);
  }
  DBUG_RETURN(0);
}

/*
  Reset thread transmit packet buffer for event sending

  This function allocates header bytes for event transmission, and
  should be called before store the event data to the packet buffer.
*/
static int reset_transmit_packet(THD *thd, ushort flags,
                                 ulong *ev_offset, const char **errmsg)
{
  int ret= 0;
  String *packet= &thd->packet;

  /* reserve and set default header */
  packet->length(0);
  packet->set("\0", 1, &my_charset_bin);

  if (RUN_HOOK(binlog_transmit, reserve_header, (thd, flags, packet)))
  {
    *errmsg= "Failed to run hook 'reserve_header'";
    my_errno= ER_UNKNOWN_ERROR;
    ret= 1;
  }
  *ev_offset= packet->length();
  return ret;
}

static int send_file(THD *thd)
{
  NET* net = &thd->net;
  int fd = -1, error = 1;
  size_t bytes;
  char fname[FN_REFLEN+1];
  const char *errmsg = 0;
  int old_timeout;
  unsigned long packet_len;
  uchar buf[IO_SIZE];				// It's safe to alloc this
  DBUG_ENTER("send_file");

  /*
    The client might be slow loading the data, give him wait_timeout to do
    the job
  */
  old_timeout= net->read_timeout;
  my_net_set_read_timeout(net, thd->variables.net_wait_timeout);

  /*
    We need net_flush here because the client will not know it needs to send
    us the file name until it has processed the load event entry
  */
  if (net_flush(net) || (packet_len = my_net_read(net)) == packet_error)
  {
    errmsg = "while reading file name";
    goto err;
  }

  // terminate with \0 for fn_format
  *((char*)net->read_pos +  packet_len) = 0;
  fn_format(fname, (char*) net->read_pos + 1, "", "", 4);
  // this is needed to make replicate-ignore-db
  if (!strcmp(fname,"/dev/null"))
    goto end;

  if ((fd= mysql_file_open(key_file_send_file,
                           fname, O_RDONLY, MYF(0))) < 0)
  {
    errmsg = "on open of file";
    goto err;
  }

  while ((long) (bytes= mysql_file_read(fd, buf, IO_SIZE, MYF(0))) > 0)
  {
    if (my_net_write(net, buf, bytes))
    {
      errmsg = "while writing data to client";
      goto err;
    }
  }

 end:
  if (my_net_write(net, (uchar*) "", 0) || net_flush(net) ||
      (my_net_read(net) == packet_error))
  {
    errmsg = "while negotiating file transfer close";
    goto err;
  }
  error = 0;

 err:
  my_net_set_read_timeout(net, old_timeout);
  if (fd >= 0)
    mysql_file_close(fd, MYF(0));
  if (errmsg)
  {
    sql_print_error("Failed in send_file() %s", errmsg);
    DBUG_PRINT("error", ("%s", errmsg));
  }
  DBUG_RETURN(error);
}


/**
   Internal to mysql_binlog_send() routine that recalculates checksum for
   a FD event (asserted) that needs additional arranment prior sending to slave.
*/
inline void fix_checksum(String *packet, ulong ev_offset)
{
  /* recalculate the crc for this event */
  uint data_len = uint4korr(packet->ptr() + ev_offset + EVENT_LEN_OFFSET);
  ha_checksum crc= my_checksum(0L, NULL, 0);
  DBUG_ASSERT(data_len == 
              LOG_EVENT_MINIMAL_HEADER_LEN + FORMAT_DESCRIPTION_HEADER_LEN +
              BINLOG_CHECKSUM_ALG_DESC_LEN + BINLOG_CHECKSUM_LEN);
  crc= my_checksum(crc, (uchar *)packet->ptr() + ev_offset, data_len -
                   BINLOG_CHECKSUM_LEN);
  int4store(packet->ptr() + ev_offset + data_len - BINLOG_CHECKSUM_LEN, crc);
}


static user_var_entry * get_binlog_checksum_uservar(THD * thd)
{
  LEX_STRING name=  { C_STRING_WITH_LEN("master_binlog_checksum")};
  user_var_entry *entry= 
    (user_var_entry*) my_hash_search(&thd->user_vars, (uchar*) name.str,
                                  name.length);
  return entry;
}

/**
  Function for calling in mysql_binlog_send
  to check if slave initiated checksum-handshake.

  @param[in]    thd  THD to access a user variable

  @return        TRUE if handshake took place, FALSE otherwise
*/

static bool is_slave_checksum_aware(THD * thd)
{
  DBUG_ENTER("is_slave_checksum_aware");
  user_var_entry *entry= get_binlog_checksum_uservar(thd);
  DBUG_RETURN(entry? true  : false);
}

/**
  Function for calling in mysql_binlog_send
  to get the value of @@binlog_checksum of the master at
  time of checksum-handshake.

  The value tells the master whether to compute or not, and the slave
  to verify or not the first artificial Rotate event's checksum.

  @param[in]    thd  THD to access a user variable

  @return       value of @@binlog_checksum alg according to
                @c enum enum_binlog_checksum_alg
*/

static uint8 get_binlog_checksum_value_at_connect(THD * thd)
{
  uint8 ret;

  DBUG_ENTER("get_binlog_checksum_value_at_connect");
  user_var_entry *entry= get_binlog_checksum_uservar(thd);
  if (!entry)
  {
    ret= BINLOG_CHECKSUM_ALG_UNDEF;
  }
  else
  {
    DBUG_ASSERT(entry->type == STRING_RESULT);
    String str;
    uint dummy_errors;
    str.copy(entry->value, entry->length, &my_charset_bin, &my_charset_bin,
             &dummy_errors);
    ret= (uint8) find_type ((char*) str.ptr(), &binlog_checksum_typelib, 1) - 1;
    DBUG_ASSERT(ret <= BINLOG_CHECKSUM_ALG_CRC32); // while it's just on CRC32 alg
  }
  DBUG_RETURN(ret);
}

/*
  Adjust the position pointer in the binary log file for all running slaves

  SYNOPSIS
    adjust_linfo_offsets()
    purge_offset	Number of bytes removed from start of log index file

  NOTES
    - This is called when doing a PURGE when we delete lines from the
      index log file

  REQUIREMENTS
    - Before calling this function, we have to ensure that no threads are
      using any binary log file before purge_offset.a

  TODO
    - Inform the slave threads that they should sync the position
      in the binary log file with flush_relay_log_info.
      Now they sync is done for next read.
*/

void adjust_linfo_offsets(my_off_t purge_offset)
{
  THD *tmp;

  mysql_mutex_lock(&LOCK_thread_count);
  I_List_iterator<THD> it(threads);

  while ((tmp=it++))
  {
    LOG_INFO* linfo;
    if ((linfo = tmp->current_linfo))
    {
      mysql_mutex_lock(&linfo->lock);
      /*
	Index file offset can be less that purge offset only if
	we just started reading the index file. In that case
	we have nothing to adjust
      */
      if (linfo->index_file_offset < purge_offset)
	linfo->fatal = (linfo->index_file_offset != 0);
      else
	linfo->index_file_offset -= purge_offset;
      mysql_mutex_unlock(&linfo->lock);
    }
  }
  mysql_mutex_unlock(&LOCK_thread_count);
}


bool log_in_use(const char* log_name)
{
  size_t log_name_len = strlen(log_name) + 1;
  THD *tmp;
  bool result = 0;

  mysql_mutex_lock(&LOCK_thread_count);
  I_List_iterator<THD> it(threads);

  while ((tmp=it++))
  {
    LOG_INFO* linfo;
    if ((linfo = tmp->current_linfo))
    {
      mysql_mutex_lock(&linfo->lock);
      result = !memcmp(log_name, linfo->log_file_name, log_name_len);
      mysql_mutex_unlock(&linfo->lock);
      if (result)
	break;
    }
  }

  mysql_mutex_unlock(&LOCK_thread_count);
  return result;
}

bool purge_error_message(THD* thd, int res)
{
  uint errcode;

  if ((errcode= purge_log_get_error_code(res)) != 0)
  {
    my_message(errcode, ER(errcode), MYF(0));
    return TRUE;
  }
  my_ok(thd);
  return FALSE;
}


/**
  Execute a PURGE BINARY LOGS TO <log> command.

  @param thd Pointer to THD object for the client thread executing the
  statement.

  @param to_log Name of the last log to purge.

  @retval FALSE success
  @retval TRUE failure
*/
bool purge_master_logs(THD* thd, const char* to_log)
{
  char search_file_name[FN_REFLEN];
  if (!mysql_bin_log.is_open())
  {
    my_ok(thd);
    return FALSE;
  }

  mysql_bin_log.make_log_name(search_file_name, to_log);
  return purge_error_message(thd,
			     mysql_bin_log.purge_logs(search_file_name, 0, 1,
						      1, NULL));
}


/**
  Execute a PURGE BINARY LOGS BEFORE <date> command.

  @param thd Pointer to THD object for the client thread executing the
  statement.

  @param purge_time Date before which logs should be purged.

  @retval FALSE success
  @retval TRUE failure
*/
bool purge_master_logs_before_date(THD* thd, time_t purge_time)
{
  if (!mysql_bin_log.is_open())
  {
    my_ok(thd);
    return 0;
  }
  return purge_error_message(thd,
                             mysql_bin_log.purge_logs_before_date(purge_time));
}

int test_for_non_eof_log_read_errors(int error, const char **errmsg)
{
  if (error == LOG_READ_EOF)
    return 0;
  my_errno= ER_MASTER_FATAL_ERROR_READING_BINLOG;
  switch (error) {
  case LOG_READ_BOGUS:
    *errmsg = "bogus data in log event";
    break;
  case LOG_READ_TOO_LARGE:
    *errmsg = "log event entry exceeded max_allowed_packet; \
Increase max_allowed_packet on master";
    break;
  case LOG_READ_IO:
    *errmsg = "I/O error reading log event";
    break;
  case LOG_READ_MEM:
    *errmsg = "memory allocation failed reading log event";
    break;
  case LOG_READ_TRUNC:
    *errmsg = "binlog truncated in the middle of event; consider out of disk space on master";
    break;
  case LOG_READ_CHECKSUM_FAILURE:
    *errmsg = "event read from binlog did not pass crc check";
    break;
  default:
    *errmsg = "unknown error reading log event on the master";
    break;
  }
  return error;
}


/**
  An auxiliary function for calling in mysql_binlog_send
  to initialize the heartbeat timeout in waiting for a binlogged event.

  @param[in]    thd  THD to access a user variable

  @return        heartbeat period an ulonglong of nanoseconds
                 or zero if heartbeat was not demanded by slave
*/ 
static ulonglong get_heartbeat_period(THD * thd)
{
  bool null_value;
  LEX_STRING name=  { C_STRING_WITH_LEN("master_heartbeat_period")};
  user_var_entry *entry= 
    (user_var_entry*) my_hash_search(&thd->user_vars, (uchar*) name.str,
                                  name.length);
  return entry? entry->val_int(&null_value) : 0;
}

/*
  Lookup the capabilities of the slave, which it announces by setting a value
  MARIA_SLAVE_CAPABILITY_XXX in @mariadb_slave_capability.

  Older MariaDB slaves, and other MySQL slaves, do not set
  @mariadb_slave_capability, corresponding to a capability of
  MARIA_SLAVE_CAPABILITY_UNKNOWN (0).
*/
static int
get_mariadb_slave_capability(THD *thd)
{
  bool null_value;
  const LEX_STRING name= { C_STRING_WITH_LEN("mariadb_slave_capability") };
  const user_var_entry *entry=
    (user_var_entry*) my_hash_search(&thd->user_vars, (uchar*) name.str,
                                  name.length);
  return entry ?
    (int)(entry->val_int(&null_value)) : MARIA_SLAVE_CAPABILITY_UNKNOWN;
}


/*
  Get the value of the @slave_connect_state user variable into the supplied
  String (this is the GTID connect state requested by the connecting slave).

  Returns false if error (ie. slave did not set the variable and does not
  want to use GTID to set start position), true if success.
*/
static bool
get_slave_connect_state(THD *thd, String *out_str)
{
  bool null_value;

  const LEX_STRING name= { C_STRING_WITH_LEN("slave_connect_state") };
  user_var_entry *entry=
    (user_var_entry*) my_hash_search(&thd->user_vars, (uchar*) name.str,
                                  name.length);
  return entry && entry->val_str(&null_value, out_str, 0) && !null_value;
}


/*
  Function prepares and sends repliation heartbeat event.

  @param net                net object of THD
  @param packet             buffer to store the heartbeat instance
  @param event_coordinates  binlog file name and position of the last
                            real event master sent from binlog

  @note 
    Among three essential pieces of heartbeat data Log_event::when
    is computed locally.
    The  error to send is serious and should force terminating
    the dump thread.
*/
static int send_heartbeat_event(NET* net, String* packet,
                                const struct event_coordinates *coord,
                                uint8 checksum_alg_arg)
{
  DBUG_ENTER("send_heartbeat_event");
  char header[LOG_EVENT_HEADER_LEN];
  my_bool do_checksum= checksum_alg_arg != BINLOG_CHECKSUM_ALG_OFF &&
    checksum_alg_arg != BINLOG_CHECKSUM_ALG_UNDEF;
  /*
    'when' (the timestamp) is set to 0 so that slave could distinguish between
    real and fake Rotate events (if necessary)
  */
  memset(header, 0, 4);  // when

  header[EVENT_TYPE_OFFSET] = HEARTBEAT_LOG_EVENT;

  char* p= coord->file_name + dirname_length(coord->file_name);

  uint ident_len = strlen(p);
  ulong event_len = ident_len + LOG_EVENT_HEADER_LEN +
    (do_checksum ? BINLOG_CHECKSUM_LEN : 0);
  int4store(header + SERVER_ID_OFFSET, global_system_variables.server_id);
  int4store(header + EVENT_LEN_OFFSET, event_len);
  int2store(header + FLAGS_OFFSET, 0);

  int4store(header + LOG_POS_OFFSET, coord->pos);  // log_pos

  packet->append(header, sizeof(header));
  packet->append(p, ident_len);             // log_file_name

  if (do_checksum)
  {
    char b[BINLOG_CHECKSUM_LEN];
    ha_checksum crc= my_checksum(0L, NULL, 0);
    crc= my_checksum(crc, (uchar*) header, sizeof(header));
    crc= my_checksum(crc, (uchar*) p, ident_len);
    int4store(b, crc);
    packet->append(b, sizeof(b));
  }

  if (my_net_write(net, (uchar*) packet->ptr(), packet->length()) ||
      net_flush(net))
  {
    DBUG_RETURN(-1);
  }
  DBUG_RETURN(0);
}


struct binlog_file_entry
{
  binlog_file_entry *next;
  char *name;
};

static binlog_file_entry *
get_binlog_list(MEM_ROOT *memroot)
{
  IO_CACHE *index_file;
  char fname[FN_REFLEN];
  size_t length;
  binlog_file_entry *current_list= NULL, *e;
  DBUG_ENTER("get_binlog_list");

  if (!mysql_bin_log.is_open())
  {
    my_error(ER_NO_BINARY_LOGGING, MYF(0));
    DBUG_RETURN(NULL);
  }

  mysql_bin_log.lock_index();
  index_file=mysql_bin_log.get_index_file();
  reinit_io_cache(index_file, READ_CACHE, (my_off_t) 0, 0, 0);

  /* The file ends with EOF or empty line */
  while ((length=my_b_gets(index_file, fname, sizeof(fname))) > 1)
  {
    --length;                                   /* Remove the newline */
    if (!(e= (binlog_file_entry *)alloc_root(memroot, sizeof(*e))) ||
        !(e->name= strmake_root(memroot, fname, length)))
    {
      mysql_bin_log.unlock_index();
      my_error(ER_OUTOFMEMORY, MYF(0), length + 1 + sizeof(*e));
      DBUG_RETURN(NULL);
    }
    e->next= current_list;
    current_list= e;
  }
  mysql_bin_log.unlock_index();

  DBUG_RETURN(current_list);
}

/*
  Find the Gtid_list_log_event at the start of a binlog.

  NULL for ok, non-NULL error message for error.

  If ok, then the event is returned in *out_gtid_list. This can be NULL if we
  get back to binlogs written by old server version without GTID support. If
  so, it means we have reached the point to start from, as no GTID events can
  exist in earlier binlogs.
*/
static const char *
get_gtid_list_event(IO_CACHE *cache, Gtid_list_log_event **out_gtid_list)
{
  Format_description_log_event init_fdle(BINLOG_VERSION);
  Format_description_log_event *fdle;
  Log_event *ev;
  const char *errormsg = NULL;

  *out_gtid_list= NULL;

  if (!(ev= Log_event::read_log_event(cache, 0, &init_fdle,
                                      opt_master_verify_checksum)) ||
      ev->get_type_code() != FORMAT_DESCRIPTION_EVENT)
  {
    if (ev)
      delete ev;
    return "Could not read format description log event while looking for "
      "GTID position in binlog";
  }

  fdle= static_cast<Format_description_log_event *>(ev);

  for (;;)
  {
    Log_event_type typ;

    ev= Log_event::read_log_event(cache, 0, fdle, opt_master_verify_checksum);
    if (!ev)
    {
      errormsg= "Could not read GTID list event while looking for GTID "
        "position in binlog";
      break;
    }
    typ= ev->get_type_code();
    if (typ == GTID_LIST_EVENT)
      break;                                    /* Done, found it */
    if (typ == ROTATE_EVENT || typ == STOP_EVENT ||
        typ == FORMAT_DESCRIPTION_EVENT)
      continue;                                 /* Continue looking */

    /* We did not find any Gtid_list_log_event, must be old binlog. */
    ev= NULL;
    break;
  }

  delete fdle;
  *out_gtid_list= static_cast<Gtid_list_log_event *>(ev);
  return errormsg;
}


/*
  Check if every GTID requested by the slave is contained in this (or a later)
  binlog file. Return true if so, false if not.
*/
static bool
contains_all_slave_gtid(slave_connection_state *st, Gtid_list_log_event *glev)
{
  uint32 i;

  for (i= 0; i < glev->count; ++i)
  {
    const rpl_gtid *gtid= st->find(glev->list[i].domain_id);
    if (gtid != NULL &&
        gtid->server_id == glev->list[i].server_id &&
        gtid->seq_no <= glev->list[i].seq_no)
    {
      /*
        The slave needs to receive gtid, but it is contained in an earlier
        binlog file. So we need to serch back further.
      */
      return false;
    }
  }
  return true;
}


/*
  Find the name of the binlog file to start reading for a slave that connects
  using GTID state.

  Returns the file name in out_name, which must be of size at least FN_REFLEN.

  Returns NULL on ok, error message on error.
*/
static const char *
gtid_find_binlog_file(slave_connection_state *state, char *out_name)
{
  MEM_ROOT memroot;
  binlog_file_entry *list;
  Gtid_list_log_event *glev;
  const char *errormsg= NULL;
  IO_CACHE cache;
  File file = (File)-1;
  char buf[FN_REFLEN];

  bzero((char*) &cache, sizeof(cache));
  init_alloc_root(&memroot, 10*(FN_REFLEN+sizeof(binlog_file_entry)), 0);
  if (!(list= get_binlog_list(&memroot)))
  {
    errormsg= "Out of memory while looking for GTID position in binlog";
    goto end;
  }

  while (list)
  {
    if (!list->next)
    {
      /*
        It should be safe to read the currently used binlog, as we will only
        read the header part that is already written.

        But if that does not work on windows, then we will need to cache the
        event somewhere in memory I suppose - that could work too.
      */
    }
    /*
      Read the Gtid_list_log_event at the start of the binlog file to
      get the binlog state.
    */
    if (normalize_binlog_name(buf, list->name, false))
    {
      errormsg= "Failed to determine binlog file name while looking for "
        "GTID position in binlog";
      goto end;
    }
    if ((file= open_binlog(&cache, buf, &errormsg)) == (File)-1 ||
        (errormsg= get_gtid_list_event(&cache, &glev)))
      goto end;

    if (!glev || contains_all_slave_gtid(state, glev))
    {
      strmake(out_name, buf, FN_REFLEN);
      goto end;
    }
    list= list->next;
  }

  /* We reached the end without finding anything. */
  errormsg= "Could not find GTID state requested by slave in any binlog "
    "files. Probably the slave state is too old and required binlog files "
    "have been purged.";

end:
  if (file != (File)-1)
  {
    end_io_cache(&cache);
    mysql_file_close(file, MYF(MY_WME));
  }

  free_root(&memroot, MYF(0));
  return errormsg;
}


/*
  Given an old-style binlog position with file name and file offset, find the
  corresponding gtid position. If the offset is not at an event boundary, give
  an error.

  Return NULL on ok, error message string on error.

  ToDo: Improve the performance of this by using binlog index files.
*/
static const char *
gtid_state_from_pos(const char *name, uint32 offset,
                    slave_connection_state *gtid_state)
{
  IO_CACHE cache;
  File file;
  const char *errormsg= NULL;
  bool found_gtid_list_event= false;
  bool found_format_description_event= false;
  bool valid_pos= false;
  uint8 current_checksum_alg= BINLOG_CHECKSUM_ALG_UNDEF;
  int err;
  String packet;

  if (gtid_state->load((const rpl_gtid *)NULL, 0))
  {
    errormsg= "Internal error (out of memory?) initializing slave state "
      "while scanning binlog to find start position";
    return errormsg;
  }

  if ((file= open_binlog(&cache, name, &errormsg)) == (File)-1)
    return errormsg;

  /*
    First we need to find the initial GTID_LIST_EVENT. We need this even
    if the offset is at the very start of the binlog file.

    But if we do not find any GTID_LIST_EVENT, then this is an old binlog
    with no GTID information, so we return empty GTID state.
  */
  for (;;)
  {
    Log_event_type typ;
    uint32 cur_pos;

    cur_pos= (uint32)my_b_tell(&cache);
    if (cur_pos == offset)
      valid_pos= true;
    if (found_format_description_event && found_gtid_list_event &&
        cur_pos >= offset)
      break;

    packet.length(0);
    err= Log_event::read_log_event(&cache, &packet, NULL,
                                   current_checksum_alg);
    if (err)
    {
      errormsg= "Could not read binlog while searching for slave start "
        "position on master";
      goto end;
    }
    /*
      The cast to uchar is needed to avoid a signed char being converted to a
      negative number.
    */
    typ= (Log_event_type)(uchar)packet[EVENT_TYPE_OFFSET];
    if (typ == FORMAT_DESCRIPTION_EVENT)
    {
      if (found_format_description_event)
      {
        errormsg= "Duplicate format description log event found while "
          "searching for old-style position in binlog";
        goto end;
      }

      current_checksum_alg= get_checksum_alg(packet.ptr(), packet.length());
      found_format_description_event= true;
    }
    else if (typ != FORMAT_DESCRIPTION_EVENT && !found_format_description_event)
    {
      errormsg= "Did not find format description log event while searching "
        "for old-style position in binlog";
      goto end;
    }
    else if (typ == ROTATE_EVENT || typ == STOP_EVENT ||
             typ == BINLOG_CHECKPOINT_EVENT)
      continue;                                 /* Continue looking */
    else if (typ == GTID_LIST_EVENT)
    {
      rpl_gtid *gtid_list;
      bool status;
      uint32 list_len;

      if (found_gtid_list_event)
      {
        errormsg= "Found duplicate Gtid_list_log_event while scanning binlog "
          "to find slave start position";
        goto end;
      }
      status= Gtid_list_log_event::peek(packet.ptr(), packet.length(),
                                        &gtid_list, &list_len);
      if (status)
      {
        errormsg= "Error reading Gtid_list_log_event while searching "
          "for old-style position in binlog";
        goto end;
      }
      err= gtid_state->load(gtid_list, list_len);
      my_free(gtid_list);
      if (err)
      {
        errormsg= "Internal error (out of memory?) initialising slave state "
          "while scanning binlog to find start position";
        goto end;
      }
      found_gtid_list_event= true;
    }
    else if (!found_gtid_list_event)
    {
      /* We did not find any Gtid_list_log_event, must be old binlog. */
      goto end;
    }
    else if (typ == GTID_EVENT)
    {
      rpl_gtid gtid;
      uchar flags2;
      if (Gtid_log_event::peek(packet.ptr(), packet.length(), &gtid.domain_id,
                               &gtid.server_id, &gtid.seq_no, &flags2))
      {
        errormsg= "Corrupt gtid_log_event found while scanning binlog to find "
          "initial slave position";
        goto end;
      }
      if (gtid_state->update(&gtid))
      {
        errormsg= "Internal error (out of memory?) updating slave state while "
          "scanning binlog to find start position";
        goto end;
      }
    }
  }

  if (!valid_pos)
  {
    errormsg= "Slave requested incorrect position in master binlog. "
      "Requested position %u in file '%s', but this position does not "
      "correspond to the location of any binlog event.";
  }

end:
  end_io_cache(&cache);
  mysql_file_close(file, MYF(MY_WME));

  return errormsg;
}


int
gtid_state_from_binlog_pos(const char *in_name, uint32 pos, String *out_str)
{
  slave_connection_state gtid_state;
  const char *lookup_name;
  char name_buf[FN_REFLEN];
  LOG_INFO linfo;

  if (in_name && in_name[0])
  {
    mysql_bin_log.make_log_name(name_buf, in_name);
    lookup_name= name_buf;
  }
  else
    lookup_name= NULL;
  linfo.index_file_offset= 0;
  if (mysql_bin_log.find_log_pos(&linfo, lookup_name, 1))
    return 1;

  if (pos < 4)
    pos= 4;

  if (gtid_state_from_pos(linfo.log_file_name, pos, &gtid_state) ||
      gtid_state.to_string(out_str))
    return 1;
  return 0;
}


enum enum_gtid_skip_type {
  GTID_SKIP_NOT, GTID_SKIP_STANDALONE, GTID_SKIP_TRANSACTION
};

/*
  Helper function for mysql_binlog_send() to write an event down the slave
  connection.

  Returns NULL on success, error message string on error.
*/
static const char *
send_event_to_slave(THD *thd, NET *net, String* const packet, ushort flags,
                    Log_event_type event_type, char *log_file_name,
                    IO_CACHE *log, int mariadb_slave_capability,
                    ulong ev_offset, uint8 current_checksum_alg,
                    bool using_gtid_state, slave_connection_state *gtid_state,
                    enum_gtid_skip_type *gtid_skip_group)
{
  my_off_t pos;

  /* Skip GTID event groups until we reach slave position within a domain_id. */
  if (event_type == GTID_EVENT && using_gtid_state && gtid_state->count() > 0)
  {
    uint32 server_id, domain_id;
    uint64 seq_no;
    uchar flags2;
    rpl_gtid *gtid;
    size_t len= packet->length();

    if (ev_offset > len ||
        Gtid_log_event::peek(packet->ptr()+ev_offset, len - ev_offset,
                             &domain_id, &server_id, &seq_no, &flags2))
      return "Failed to read Gtid_log_event: corrupt binlog";
    gtid= gtid_state->find(domain_id);
    if (gtid != NULL)
    {
      /* Skip this event group if we have not yet reached slave start pos. */
      if (server_id != gtid->server_id || seq_no <= gtid->seq_no)
        *gtid_skip_group = (flags2 & Gtid_log_event::FL_STANDALONE ?
                            GTID_SKIP_STANDALONE : GTID_SKIP_TRANSACTION);
      /*
        Delete this entry if we have reached slave start position (so we will
        not skip subsequent events and won't have to look them up and check).
      */
      if (server_id == gtid->server_id && seq_no >= gtid->seq_no)
        gtid_state->remove(gtid);
    }
  }

  /*
    Skip event group if we have not yet reached the correct slave GTID position.

    Note that slave that understands GTID can also tolerate holes, so there is
    no need to supply dummy event.
  */
  switch (*gtid_skip_group)
  {
  case GTID_SKIP_STANDALONE:
    if (event_type != INTVAR_EVENT &&
        event_type != RAND_EVENT &&
        event_type != USER_VAR_EVENT &&
        event_type != TABLE_MAP_EVENT &&
        event_type != ANNOTATE_ROWS_EVENT)
      *gtid_skip_group= GTID_SKIP_NOT;
    return NULL;
  case GTID_SKIP_TRANSACTION:
    if (event_type == XID_EVENT /* ToDo || is_COMMIT_query_event() */)
      *gtid_skip_group= GTID_SKIP_NOT;
    return NULL;
  case GTID_SKIP_NOT:
    break;
  }

  /* Do not send annotate_rows events unless slave requested it. */
  if (event_type == ANNOTATE_ROWS_EVENT && !(flags & BINLOG_SEND_ANNOTATE_ROWS_EVENT))
  {
    if (mariadb_slave_capability >= MARIA_SLAVE_CAPABILITY_TOLERATE_HOLES)
    {
      /* This slave can tolerate events omitted from the binlog stream. */
      return NULL;
    }
    else if (mariadb_slave_capability >= MARIA_SLAVE_CAPABILITY_ANNOTATE)
    {
      /*
        The slave did not request ANNOTATE_ROWS_EVENT (it does not need them as
        it will not log them in its own binary log). However, it understands the
        event and will just ignore it, and it would break if we omitted it,
        leaving a hole in the binlog stream. So just send the event as-is.
      */
    }
    else
    {
      /*
        The slave does not understand ANNOTATE_ROWS_EVENT.

        Older MariaDB slaves (and MySQL slaves) will break replication if there
        are holes in the binlog stream (they will miscompute the binlog offset
        and request the wrong position when reconnecting).

        So replace the event with a dummy event of the same size that will be
        a no-operation on the slave.
      */
      if (Query_log_event::dummy_event(packet, ev_offset, current_checksum_alg))
        return "Failed to replace row annotate event with dummy: too small event.";
    }
  }

  /*
    Replace GTID events with old-style BEGIN events for slaves that do not
    understand global transaction IDs. For stand-alone events, where there is
    no terminating COMMIT query event, omit the GTID event or replace it with
    a dummy event, as appropriate.
  */
  if (event_type == GTID_EVENT &&
      mariadb_slave_capability < MARIA_SLAVE_CAPABILITY_GTID)
  {
    bool need_dummy=
      mariadb_slave_capability < MARIA_SLAVE_CAPABILITY_TOLERATE_HOLES;
    bool err= Gtid_log_event::make_compatible_event(packet, &need_dummy,
                                                    ev_offset,
                                                    current_checksum_alg);
    if (err)
      return "Failed to replace GTID event with backwards-compatible event: "
             "currupt event.";
    if (!need_dummy)
      return NULL;
  }

  /*
    Do not send binlog checkpoint or gtid list events to a slave that does not
    understand it.
  */
  if ((unlikely(event_type == BINLOG_CHECKPOINT_EVENT) &&
       mariadb_slave_capability < MARIA_SLAVE_CAPABILITY_BINLOG_CHECKPOINT) ||
      (unlikely(event_type == GTID_LIST_EVENT) &&
       mariadb_slave_capability < MARIA_SLAVE_CAPABILITY_GTID))
  {
    if (mariadb_slave_capability >= MARIA_SLAVE_CAPABILITY_TOLERATE_HOLES)
    {
      /* This slave can tolerate events omitted from the binlog stream. */
      return NULL;
    }
    else
    {
      /*
        The slave does not understand BINLOG_CHECKPOINT_EVENT. Send a dummy
        event instead, with same length so slave does not get confused about
        binlog positions.
      */
      if (Query_log_event::dummy_event(packet, ev_offset, current_checksum_alg))
        return "Failed to replace binlog checkpoint or gtid list event with "
               "dummy: too small event.";
    }
  }

  /*
    Skip events with the @@skip_replication flag set, if slave requested
    skipping of such events.
  */
  if (thd->variables.option_bits & OPTION_SKIP_REPLICATION)
  {
    /*
      The first byte of the packet is a '\0' to distinguish it from an error
      packet. So the actual event starts at offset +1.
    */
    uint16 event_flags= uint2korr(&((*packet)[FLAGS_OFFSET+1]));
    if (event_flags & LOG_EVENT_SKIP_REPLICATION_F)
      return NULL;
  }

  thd_proc_info(thd, "Sending binlog event to slave");

  pos= my_b_tell(log);
  if (RUN_HOOK(binlog_transmit, before_send_event,
               (thd, flags, packet, log_file_name, pos)))
    return "run 'before_send_event' hook failed";

  if (my_net_write(net, (uchar*) packet->ptr(), packet->length()))
    return "Failed on my_net_write()";

  DBUG_PRINT("info", ("log event code %d", (*packet)[LOG_EVENT_OFFSET+1] ));
  if (event_type == LOAD_EVENT)
  {
    if (send_file(thd))
      return "failed in send_file()";
  }

  if (RUN_HOOK(binlog_transmit, after_send_event, (thd, flags, packet)))
    return "Failed to run hook 'after_send_event'";

  return NULL;    /* Success */
}

void mysql_binlog_send(THD* thd, char* log_ident, my_off_t pos,
		       ushort flags)
{
  LOG_INFO linfo;
  char *log_file_name = linfo.log_file_name;
  char search_file_name[FN_REFLEN], *name;

  ulong ev_offset;

  IO_CACHE log;
  File file = -1;
  String* const packet = &thd->packet;
  int error;
  const char *errmsg = "Unknown error", *tmp_msg;
  char error_text[MAX_SLAVE_ERRMSG]; // to be send to slave via my_message()
  NET* net = &thd->net;
  mysql_mutex_t *log_lock;
  mysql_cond_t *log_cond;
  int mariadb_slave_capability;
  char str_buf[256];
  String connect_gtid_state(str_buf, sizeof(str_buf), system_charset_info);
  bool using_gtid_state;
  slave_connection_state gtid_state, return_gtid_state;
  enum_gtid_skip_type gtid_skip_group= GTID_SKIP_NOT;

  uint8 current_checksum_alg= BINLOG_CHECKSUM_ALG_UNDEF;
  int old_max_allowed_packet= thd->variables.max_allowed_packet;
#ifndef DBUG_OFF
  int left_events = max_binlog_dump_events;
#endif
  DBUG_ENTER("mysql_binlog_send");
  DBUG_PRINT("enter",("log_ident: '%s'  pos: %ld", log_ident, (long) pos));

  bzero((char*) &log,sizeof(log));
  /* 
     heartbeat_period from @master_heartbeat_period user variable
  */
  ulonglong heartbeat_period= get_heartbeat_period(thd);
  struct timespec heartbeat_buf;
  struct timespec *heartbeat_ts= NULL;
  const LOG_POS_COORD start_coord= { log_ident, pos },
    *p_start_coord= &start_coord;
  LOG_POS_COORD coord_buf= { log_file_name, BIN_LOG_HEADER_SIZE },
    *p_coord= &coord_buf;
  if (heartbeat_period != LL(0))
  {
    heartbeat_ts= &heartbeat_buf;
    set_timespec_nsec(*heartbeat_ts, 0);
  }
  mariadb_slave_capability= get_mariadb_slave_capability(thd);

  connect_gtid_state.length(0);
  using_gtid_state= get_slave_connect_state(thd, &connect_gtid_state);

  if (global_system_variables.log_warnings > 1)
    sql_print_information("Start binlog_dump to slave_server(%d), pos(%s, %lu)",
                          (int)thd->variables.server_id, log_ident, (ulong)pos);
  if (RUN_HOOK(binlog_transmit, transmit_start, (thd, flags, log_ident, pos)))
  {
    errmsg= "Failed to run hook 'transmit_start'";
    my_errno= ER_UNKNOWN_ERROR;
    goto err;
  }

#ifndef DBUG_OFF
  if (opt_sporadic_binlog_dump_fail && (binlog_dump_count++ % 2))
  {
    errmsg = "Master failed COM_BINLOG_DUMP to test if slave can recover";
    my_errno= ER_UNKNOWN_ERROR;
    goto err;
  }
#endif

  if (!mysql_bin_log.is_open())
  {
    errmsg = "Binary log is not open";
    my_errno= ER_MASTER_FATAL_ERROR_READING_BINLOG;
    goto err;
  }
  if (!server_id_supplied)
  {
    errmsg = "Misconfigured master - server id was not set";
    my_errno= ER_MASTER_FATAL_ERROR_READING_BINLOG;
    goto err;
  }

  name=search_file_name;
  if (using_gtid_state)
  {
    if (gtid_state.load(connect_gtid_state.c_ptr_quick(),
                        connect_gtid_state.length()))
    {
      errmsg= "Out of memory or malformed slave request when obtaining start "
        "position from GTID state";
      my_errno= ER_UNKNOWN_ERROR;
      goto err;
    }
    if ((errmsg= gtid_find_binlog_file(&gtid_state, search_file_name)))
    {
      my_errno= ER_UNKNOWN_ERROR;
      goto err;
    }
    pos= 4;
  }
  else
  {
    if (log_ident[0])
      mysql_bin_log.make_log_name(search_file_name, log_ident);
    else
      name=0;					// Find first log
  }

  linfo.index_file_offset = 0;

  if (mysql_bin_log.find_log_pos(&linfo, name, 1))
  {
    errmsg = "Could not find first log file name in binary log index file";
    my_errno= ER_MASTER_FATAL_ERROR_READING_BINLOG;
    goto err;
  }

  mysql_mutex_lock(&LOCK_thread_count);
  thd->current_linfo = &linfo;
  mysql_mutex_unlock(&LOCK_thread_count);

  if ((file=open_binlog(&log, log_file_name, &errmsg)) < 0)
  {
    my_errno= ER_MASTER_FATAL_ERROR_READING_BINLOG;
    goto err;
  }
  if (pos < BIN_LOG_HEADER_SIZE || pos > my_b_filelength(&log))
  {
    errmsg= "Client requested master to start replication from \
impossible position";
    my_errno= ER_MASTER_FATAL_ERROR_READING_BINLOG;
    goto err;
  }

  /* reset transmit packet for the fake rotate event below */
  if (reset_transmit_packet(thd, flags, &ev_offset, &errmsg))
    goto err;

  /*
    Tell the client about the log name with a fake Rotate event;
    this is needed even if we also send a Format_description_log_event
    just after, because that event does not contain the binlog's name.
    Note that as this Rotate event is sent before
    Format_description_log_event, the slave cannot have any info to
    understand this event's format, so the header len of
    Rotate_log_event is FROZEN (so in 5.0 it will have a header shorter
    than other events except FORMAT_DESCRIPTION_EVENT).
    Before 4.0.14 we called fake_rotate_event below only if (pos ==
    BIN_LOG_HEADER_SIZE), because if this is false then the slave
    already knows the binlog's name.
    Since, we always call fake_rotate_event; if the slave already knew
    the log's name (ex: CHANGE MASTER TO MASTER_LOG_FILE=...) this is
    useless but does not harm much. It is nice for 3.23 (>=.58) slaves
    which test Rotate events to see if the master is 4.0 (then they
    choose to stop because they can't replicate 4.0); by always calling
    fake_rotate_event we are sure that 3.23.58 and newer will detect the
    problem as soon as replication starts (BUG#198).
    Always calling fake_rotate_event makes sending of normal
    (=from-binlog) Rotate events a priori unneeded, but it is not so
    simple: the 2 Rotate events are not equivalent, the normal one is
    before the Stop event, the fake one is after. If we don't send the
    normal one, then the Stop event will be interpreted (by existing 4.0
    slaves) as "the master stopped", which is wrong. So for safety,
    given that we want minimum modification of 4.0, we send the normal
    and fake Rotates.
  */
  if (fake_rotate_event(net, packet, log_file_name, pos, &errmsg,
                        get_binlog_checksum_value_at_connect(thd)))
  {
    /*
       This error code is not perfect, as fake_rotate_event() does not
       read anything from the binlog; if it fails it's because of an
       error in my_net_write(), fortunately it will say so in errmsg.
    */
    my_errno= ER_MASTER_FATAL_ERROR_READING_BINLOG;
    goto err;
  }

  /*
    Adding MAX_LOG_EVENT_HEADER_LEN, since a binlog event can become
    this larger than the corresponding packet (query) sent 
    from client to master.
  */
  thd->variables.max_allowed_packet= MAX_MAX_ALLOWED_PACKET;

  /*
    We can set log_lock now, it does not move (it's a member of
    mysql_bin_log, and it's already inited, and it will be destroyed
    only at shutdown).
  */
  p_coord->pos= pos; // the first hb matches the slave's last seen value
  log_lock= mysql_bin_log.get_log_lock();
  log_cond= mysql_bin_log.get_log_cond();
  if (pos > BIN_LOG_HEADER_SIZE)
  {
    /* reset transmit packet for the event read from binary log
       file */
    if (reset_transmit_packet(thd, flags, &ev_offset, &errmsg))
      goto err;

     /*
       Try to find a Format_description_log_event at the beginning of
       the binlog
     */
    if (!(error = Log_event::read_log_event(&log, packet, log_lock, 0)))
    { 
       /*
         The packet has offsets equal to the normal offsets in a
         binlog event + ev_offset (the first ev_offset characters are
         the header (default \0)).
       */
       DBUG_PRINT("info",
                  ("Looked for a Format_description_log_event, found event type %d",
                   (*packet)[EVENT_TYPE_OFFSET+ev_offset]));
       if ((*packet)[EVENT_TYPE_OFFSET+ev_offset] == FORMAT_DESCRIPTION_EVENT)
       {
         current_checksum_alg= get_checksum_alg(packet->ptr() + ev_offset,
                                                packet->length() - ev_offset);
         DBUG_ASSERT(current_checksum_alg == BINLOG_CHECKSUM_ALG_OFF ||
                     current_checksum_alg == BINLOG_CHECKSUM_ALG_UNDEF ||
                     current_checksum_alg == BINLOG_CHECKSUM_ALG_CRC32);
         if (!is_slave_checksum_aware(thd) &&
             current_checksum_alg != BINLOG_CHECKSUM_ALG_OFF &&
             current_checksum_alg != BINLOG_CHECKSUM_ALG_UNDEF)
         {
           my_errno= ER_MASTER_FATAL_ERROR_READING_BINLOG;
           errmsg= "Slave can not handle replication events with the checksum "
             "that master is configured to log";
           sql_print_warning("Master is configured to log replication events "
                             "with checksum, but will not send such events to "
                             "slaves that cannot process them");
           goto err;
         }
         (*packet)[FLAGS_OFFSET+ev_offset] &= ~LOG_EVENT_BINLOG_IN_USE_F;
         /*
           mark that this event with "log_pos=0", so the slave
           should not increment master's binlog position
           (rli->group_master_log_pos)
         */
         int4store((char*) packet->ptr()+LOG_POS_OFFSET+ev_offset, 0);
         /*
           if reconnect master sends FD event with `created' as 0
           to avoid destroying temp tables.
          */
         int4store((char*) packet->ptr()+LOG_EVENT_MINIMAL_HEADER_LEN+
                   ST_CREATED_OFFSET+ev_offset, (ulong) 0);

	 /* fix the checksum due to latest changes in header */
	 if (current_checksum_alg != BINLOG_CHECKSUM_ALG_OFF &&
             current_checksum_alg != BINLOG_CHECKSUM_ALG_UNDEF)
           fix_checksum(packet, ev_offset);

         /* send it */
         if (my_net_write(net, (uchar*) packet->ptr(), packet->length()))
         {
           errmsg = "Failed on my_net_write()";
           my_errno= ER_UNKNOWN_ERROR;
           goto err;
         }

         /*
           No need to save this event. We are only doing simple reads
           (no real parsing of the events) so we don't need it. And so
           we don't need the artificial Format_description_log_event of
           3.23&4.x.
         */
       }
     }
     else
     {
       if (test_for_non_eof_log_read_errors(error, &errmsg))
         goto err;
       /*
         It's EOF, nothing to do, go on reading next events, the
         Format_description_log_event will be found naturally if it is written.
       */
     }
  } /* end of if (pos > BIN_LOG_HEADER_SIZE); */
  else
  {
    /* The Format_description_log_event event will be found naturally. */
  }

  /* seek to the requested position, to start the requested dump */
  my_b_seek(&log, pos);			// Seek will done on next read

  while (!net->error && net->vio != 0 && !thd->killed)
  {
    Log_event_type event_type= UNKNOWN_EVENT;

    /* reset the transmit packet for the event read from binary log
       file */
    if (reset_transmit_packet(thd, flags, &ev_offset, &errmsg))
      goto err;

    while (!(error = Log_event::read_log_event(&log, packet, log_lock,
                                               current_checksum_alg)))
    {
#ifndef DBUG_OFF
      if (max_binlog_dump_events && !left_events--)
      {
	net_flush(net);
	errmsg = "Debugging binlog dump abort";
	my_errno= ER_UNKNOWN_ERROR;
	goto err;
      }
#endif
      /*
        log's filename does not change while it's active
      */
      p_coord->pos= uint4korr(packet->ptr() + ev_offset + LOG_POS_OFFSET);

      event_type=
        (Log_event_type)((uchar)(*packet)[LOG_EVENT_OFFSET+ev_offset]);
      DBUG_EXECUTE_IF("dump_thread_wait_before_send_xid",
                      {
                        if (event_type == XID_EVENT)
                        {
                          net_flush(net);
                          const char act[]=
                            "now "
                            "wait_for signal.continue";
                          DBUG_ASSERT(debug_sync_service);
                          DBUG_ASSERT(!debug_sync_set_action(thd,
                                                             STRING_WITH_LEN(act)));
                          const char act2[]=
                            "now "
                            "signal signal.continued";
                          DBUG_ASSERT(!debug_sync_set_action(current_thd,
                                                             STRING_WITH_LEN(act2)));
                        }
                      });
      if (event_type == FORMAT_DESCRIPTION_EVENT)
      {
        current_checksum_alg= get_checksum_alg(packet->ptr() + ev_offset,
                                               packet->length() - ev_offset);
        DBUG_ASSERT(current_checksum_alg == BINLOG_CHECKSUM_ALG_OFF ||
                    current_checksum_alg == BINLOG_CHECKSUM_ALG_UNDEF ||
                    current_checksum_alg == BINLOG_CHECKSUM_ALG_CRC32);
        if (!is_slave_checksum_aware(thd) &&
            current_checksum_alg != BINLOG_CHECKSUM_ALG_OFF &&
            current_checksum_alg != BINLOG_CHECKSUM_ALG_UNDEF)
        {
          my_errno= ER_MASTER_FATAL_ERROR_READING_BINLOG;
          errmsg= "Slave can not handle replication events with the checksum "
            "that master is configured to log";
          sql_print_warning("Master is configured to log replication events "
                            "with checksum, but will not send such events to "
                            "slaves that cannot process them");
          goto err;
        }

        (*packet)[FLAGS_OFFSET+ev_offset] &= ~LOG_EVENT_BINLOG_IN_USE_F;
      }

      if ((tmp_msg= send_event_to_slave(thd, net, packet, flags, event_type,
                                        log_file_name, &log,
                                        mariadb_slave_capability, ev_offset,
                                        current_checksum_alg, using_gtid_state,
                                        &gtid_state, &gtid_skip_group)))
      {
        errmsg= tmp_msg;
        my_errno= ER_UNKNOWN_ERROR;
        goto err;
      }

      DBUG_EXECUTE_IF("dump_thread_wait_before_send_xid",
                      {
                        if (event_type == XID_EVENT)
                        {
                          net_flush(net);
                        }
                      });

      /* reset transmit packet for next loop */
      if (reset_transmit_packet(thd, flags, &ev_offset, &errmsg))
        goto err;
    }

    /*
      TODO: now that we are logging the offset, check to make sure
      the recorded offset and the actual match.
      Guilhem 2003-06: this is not true if this master is a slave
      <4.0.15 running with --log-slave-updates, because then log_pos may
      be the offset in the-master-of-this-master's binlog.
    */
    if (test_for_non_eof_log_read_errors(error, &errmsg))
      goto err;

    if (!(flags & BINLOG_DUMP_NON_BLOCK) &&
        mysql_bin_log.is_active(log_file_name))
    {
      /*
	Block until there is more data in the log
      */
      if (net_flush(net))
      {
	errmsg = "failed on net_flush()";
	my_errno= ER_UNKNOWN_ERROR;
	goto err;
      }

      /*
	We may have missed the update broadcast from the log
	that has just happened, let's try to catch it if it did.
	If we did not miss anything, we just wait for other threads
	to signal us.
      */
      {
	log.error=0;
	bool read_packet = 0;

#ifndef DBUG_OFF
	if (max_binlog_dump_events && !left_events--)
	{
	  errmsg = "Debugging binlog dump abort";
	  my_errno= ER_UNKNOWN_ERROR;
	  goto err;
	}
#endif

        /* reset the transmit packet for the event read from binary log
           file */
        if (reset_transmit_packet(thd, flags, &ev_offset, &errmsg))
          goto err;
        
	/*
	  No one will update the log while we are reading
	  now, but we'll be quick and just read one record

	  TODO:
          Add an counter that is incremented for each time we update the
          binary log.  We can avoid the following read if the counter
          has not been updated since last read.
	*/

        mysql_mutex_lock(log_lock);
        switch (error= Log_event::read_log_event(&log, packet, (mysql_mutex_t*) 0,
                                                 current_checksum_alg)) {
	case 0:
	  /* we read successfully, so we'll need to send it to the slave */
          mysql_mutex_unlock(log_lock);
	  read_packet = 1;
          p_coord->pos= uint4korr(packet->ptr() + ev_offset + LOG_POS_OFFSET);
          event_type= (Log_event_type)((*packet)[LOG_EVENT_OFFSET+ev_offset]);
	  break;

	case LOG_READ_EOF:
        {
          int ret;
          ulong signal_cnt;
	  DBUG_PRINT("wait",("waiting for data in binary log"));
          /* For mysqlbinlog (mysqlbinlog.server_id==0). */
	  if (thd->variables.server_id==0)
	  {
            mysql_mutex_unlock(log_lock);
	    goto end;
	  }

#ifndef DBUG_OFF
          ulong hb_info_counter= 0;
#endif
          const char* old_msg= thd->proc_info;
          signal_cnt= mysql_bin_log.signal_cnt;
          do 
          {
            if (heartbeat_period != 0)
            {
              DBUG_ASSERT(heartbeat_ts);
              set_timespec_nsec(*heartbeat_ts, heartbeat_period);
            }
            thd->enter_cond(log_cond, log_lock,
                            "Master has sent all binlog to slave; "
                            "waiting for binlog to be updated");
            ret= mysql_bin_log.wait_for_update_bin_log(thd, heartbeat_ts);
            DBUG_ASSERT(ret == 0 || (heartbeat_period != 0));
            if (ret == ETIMEDOUT || ret == ETIME)
            {
#ifndef DBUG_OFF
              if (hb_info_counter < 3)
              {
                sql_print_information("master sends heartbeat message");
                hb_info_counter++;
                if (hb_info_counter == 3)
                  sql_print_information("the rest of heartbeat info skipped ...");
              }
#endif
              /* reset transmit packet for the heartbeat event */
              if (reset_transmit_packet(thd, flags, &ev_offset, &errmsg))
              {
                thd->exit_cond(old_msg);
                goto err;
              }
              if (send_heartbeat_event(net, packet, p_coord, current_checksum_alg))
              {
                errmsg = "Failed on my_net_write()";
                my_errno= ER_UNKNOWN_ERROR;
                thd->exit_cond(old_msg);
                goto err;
              }
            }
            else
            {
              DBUG_PRINT("wait",("binary log received update or a broadcast signal caught"));
            }
          } while (signal_cnt == mysql_bin_log.signal_cnt && !thd->killed);
          thd->exit_cond(old_msg);
        }
        break;
            
        default:
          mysql_mutex_unlock(log_lock);
          test_for_non_eof_log_read_errors(error, &errmsg);
          goto err;
	}

        if (read_packet &&
            (tmp_msg= send_event_to_slave(thd, net, packet, flags, event_type,
                                          log_file_name, &log,
                                          mariadb_slave_capability, ev_offset,
                                          current_checksum_alg,
                                          using_gtid_state, &gtid_state,
                                          &gtid_skip_group)))
        {
          errmsg= tmp_msg;
          my_errno= ER_UNKNOWN_ERROR;
          goto err;
	}

	log.error=0;
      }
    }
    else
    {
      bool loop_breaker = 0;
      /* need this to break out of the for loop from switch */

      thd_proc_info(thd, "Finished reading one binlog; switching to next binlog");
      switch (mysql_bin_log.find_next_log(&linfo, 1)) {
      case 0:
	break;
      case LOG_INFO_EOF:
        if (mysql_bin_log.is_active(log_file_name))
        {
          loop_breaker = (flags & BINLOG_DUMP_NON_BLOCK);
          break;
        }
      default:
	errmsg = "could not find next log";
	my_errno= ER_MASTER_FATAL_ERROR_READING_BINLOG;
	goto err;
      }

      if (loop_breaker)
        break;

      end_io_cache(&log);
      mysql_file_close(file, MYF(MY_WME));

      /* reset transmit packet for the possible fake rotate event */
      if (reset_transmit_packet(thd, flags, &ev_offset, &errmsg))
        goto err;
      
      /*
        Call fake_rotate_event() in case the previous log (the one which
        we have just finished reading) did not contain a Rotate event
        (for example (I don't know any other example) the previous log
        was the last one before the master was shutdown & restarted).
        This way we tell the slave about the new log's name and
        position.  If the binlog is 5.0, the next event we are going to
        read and send is Format_description_log_event.
      */
      if ((file=open_binlog(&log, log_file_name, &errmsg)) < 0 ||
	  fake_rotate_event(net, packet, log_file_name, BIN_LOG_HEADER_SIZE,
                            &errmsg, current_checksum_alg))
      {
	my_errno= ER_MASTER_FATAL_ERROR_READING_BINLOG;
	goto err;
      }

      p_coord->file_name= log_file_name; // reset to the next
    }
  }

end:
  end_io_cache(&log);
  mysql_file_close(file, MYF(MY_WME));

  RUN_HOOK(binlog_transmit, transmit_stop, (thd, flags));
  my_eof(thd);
  thd_proc_info(thd, "Waiting to finalize termination");
  mysql_mutex_lock(&LOCK_thread_count);
  thd->current_linfo = 0;
  mysql_mutex_unlock(&LOCK_thread_count);
  thd->variables.max_allowed_packet= old_max_allowed_packet;
  DBUG_VOID_RETURN;

err:
  thd_proc_info(thd, "Waiting to finalize termination");
  if (my_errno == ER_MASTER_FATAL_ERROR_READING_BINLOG && my_b_inited(&log))
  {
    /* 
       detailing the fatal error message with coordinates 
       of the last position read.
    */
    my_snprintf(error_text, sizeof(error_text),
                "%s; the first event '%s' at %lld, "
                "the last event read from '%s' at %lld, "
                "the last byte read from '%s' at %lld.",
                errmsg,
                my_basename(p_start_coord->file_name), p_start_coord->pos,
                my_basename(p_coord->file_name), p_coord->pos,
                my_basename(log_file_name), my_b_tell(&log));
  }
  else
    strcpy(error_text, errmsg);
  end_io_cache(&log);
  RUN_HOOK(binlog_transmit, transmit_stop, (thd, flags));
  /*
    Exclude  iteration through thread list
    this is needed for purge_logs() - it will iterate through
    thread list and update thd->current_linfo->index_file_offset
    this mutex will make sure that it never tried to update our linfo
    after we return from this stack frame
  */
  mysql_mutex_lock(&LOCK_thread_count);
  thd->current_linfo = 0;
  mysql_mutex_unlock(&LOCK_thread_count);
  if (file >= 0)
    mysql_file_close(file, MYF(MY_WME));
  thd->variables.max_allowed_packet= old_max_allowed_packet;

  my_message(my_errno, error_text, MYF(0));
  DBUG_VOID_RETURN;
}


/**
  Execute a START SLAVE statement.

  @param thd Pointer to THD object for the client thread executing the
  statement.

  @param mi Pointer to Master_info object for the slave's IO thread.

  @param net_report If true, saves the exit status into thd->stmt_da.

  @retval 0 success
  @retval 1 error
  @retval -1 fatal error
*/

int start_slave(THD* thd , Master_info* mi,  bool net_report)
{
  int slave_errno= 0;
  int thread_mask;
  char master_info_file_tmp[FN_REFLEN];
  char relay_log_info_file_tmp[FN_REFLEN];
  DBUG_ENTER("start_slave");

  if (check_access(thd, SUPER_ACL, any_db, NULL, NULL, 0, 0))
    DBUG_RETURN(-1);

  create_logfile_name_with_suffix(master_info_file_tmp,
                                  sizeof(master_info_file_tmp),
                                  master_info_file, 0, &mi->connection_name);
  create_logfile_name_with_suffix(relay_log_info_file_tmp,
                                  sizeof(relay_log_info_file_tmp),
                                  relay_log_info_file, 0,
                                  &mi->connection_name);

  lock_slave_threads(mi);  // this allows us to cleanly read slave_running
  // Get a mask of _stopped_ threads
  init_thread_mask(&thread_mask,mi,1 /* inverse */);
  /*
    Below we will start all stopped threads.  But if the user wants to
    start only one thread, do as if the other thread was running (as we
    don't wan't to touch the other thread), so set the bit to 0 for the
    other thread
  */
  if (thd->lex->slave_thd_opt)
    thread_mask&= thd->lex->slave_thd_opt;
  if (thread_mask) //some threads are stopped, start them
  {
    if (init_master_info(mi,master_info_file_tmp,relay_log_info_file_tmp, 0,
			 thread_mask))
      slave_errno=ER_MASTER_INFO;
    else if (server_id_supplied && *mi->host)
    {
      /*
        If we will start SQL thread we will care about UNTIL options If
        not and they are specified we will ignore them and warn user
        about this fact.
      */
      if (thread_mask & SLAVE_SQL)
      {
        mysql_mutex_lock(&mi->rli.data_lock);

        if (thd->lex->mi.pos)
        {
          mi->rli.until_condition= Relay_log_info::UNTIL_MASTER_POS;
          mi->rli.until_log_pos= thd->lex->mi.pos;
          /*
             We don't check thd->lex->mi.log_file_name for NULL here
             since it is checked in sql_yacc.yy
          */
          strmake(mi->rli.until_log_name, thd->lex->mi.log_file_name,
                  sizeof(mi->rli.until_log_name)-1);
        }
        else if (thd->lex->mi.relay_log_pos)
        {
          mi->rli.until_condition= Relay_log_info::UNTIL_RELAY_POS;
          mi->rli.until_log_pos= thd->lex->mi.relay_log_pos;
          strmake(mi->rli.until_log_name, thd->lex->mi.relay_log_name,
                  sizeof(mi->rli.until_log_name)-1);
        }
        else
          mi->rli.clear_until_condition();

        if (mi->rli.until_condition != Relay_log_info::UNTIL_NONE)
        {
          /* Preparing members for effective until condition checking */
          const char *p= fn_ext(mi->rli.until_log_name);
          char *p_end;
          if (*p)
          {
            //p points to '.'
            mi->rli.until_log_name_extension= strtoul(++p,&p_end, 10);
            /*
              p_end points to the first invalid character. If it equals
              to p, no digits were found, error. If it contains '\0' it
              means  conversion went ok.
            */
            if (p_end==p || *p_end)
              slave_errno=ER_BAD_SLAVE_UNTIL_COND;
          }
          else
            slave_errno=ER_BAD_SLAVE_UNTIL_COND;

          /* mark the cached result of the UNTIL comparison as "undefined" */
          mi->rli.until_log_names_cmp_result=
            Relay_log_info::UNTIL_LOG_NAMES_CMP_UNKNOWN;

          /* Issuing warning then started without --skip-slave-start */
          if (!opt_skip_slave_start)
            push_warning(thd, MYSQL_ERROR::WARN_LEVEL_NOTE,
                         ER_MISSING_SKIP_SLAVE,
                         ER(ER_MISSING_SKIP_SLAVE));
        }

        mysql_mutex_unlock(&mi->rli.data_lock);
      }
      else if (thd->lex->mi.pos || thd->lex->mi.relay_log_pos)
        push_warning(thd, MYSQL_ERROR::WARN_LEVEL_NOTE, ER_UNTIL_COND_IGNORED,
                     ER(ER_UNTIL_COND_IGNORED));

      if (!slave_errno)
        slave_errno = start_slave_threads(0 /*no mutex */,
                                          1 /* wait for start */,
                                          mi,
                                          master_info_file_tmp,
                                          relay_log_info_file_tmp,
                                          thread_mask);
    }
    else
      slave_errno = ER_BAD_SLAVE;
  }
  else
  {
    /* no error if all threads are already started, only a warning */
    push_warning(thd, MYSQL_ERROR::WARN_LEVEL_NOTE, ER_SLAVE_WAS_RUNNING,
                 ER(ER_SLAVE_WAS_RUNNING));
  }

  unlock_slave_threads(mi);

  if (slave_errno)
  {
    if (net_report)
      my_error(slave_errno, MYF(0),
               (int) mi->connection_name.length,
               mi->connection_name.str);
    DBUG_RETURN(slave_errno == ER_BAD_SLAVE ? -1 : 1);
  }

  DBUG_RETURN(0);
}


/**
  Execute a STOP SLAVE statement.

  @param thd Pointer to THD object for the client thread executing the
  statement.

  @param mi Pointer to Master_info object for the slave's IO thread.

  @param net_report If true, saves the exit status into thd->stmt_da.

  @retval 0 success
  @retval 1 error
  @retval -1 error
*/

int stop_slave(THD* thd, Master_info* mi, bool net_report )
{
  int slave_errno;
  DBUG_ENTER("stop_slave");
  DBUG_PRINT("enter",("Connection: %s", mi->connection_name.str));

  if (check_access(thd, SUPER_ACL, any_db, NULL, NULL, 0, 0))
    DBUG_RETURN(-1);
  thd_proc_info(thd, "Killing slave");
  int thread_mask;
  lock_slave_threads(mi);
  // Get a mask of _running_ threads
  init_thread_mask(&thread_mask,mi,0 /* not inverse*/);
  /*
    Below we will stop all running threads.
    But if the user wants to stop only one thread, do as if the other thread
    was stopped (as we don't wan't to touch the other thread), so set the
    bit to 0 for the other thread
  */
  if (thd->lex->slave_thd_opt)
    thread_mask &= thd->lex->slave_thd_opt;

  if (thread_mask)
  {
    slave_errno= terminate_slave_threads(mi,thread_mask,
                                         1 /*skip lock */);
  }
  else
  {
    //no error if both threads are already stopped, only a warning
    slave_errno= 0;
    push_warning(thd, MYSQL_ERROR::WARN_LEVEL_NOTE, ER_SLAVE_WAS_NOT_RUNNING,
                 ER(ER_SLAVE_WAS_NOT_RUNNING));
  }
  unlock_slave_threads(mi);
  thd_proc_info(thd, 0);

  if (slave_errno)
  {
    if (net_report)
      my_message(slave_errno, ER(slave_errno), MYF(0));
    DBUG_RETURN(1);
  }

  DBUG_RETURN(0);
}


/**
  Execute a RESET SLAVE statement.

  @param thd Pointer to THD object of the client thread executing the
  statement.

  @param mi Pointer to Master_info object for the slave.

  @retval 0 success
  @retval 1 error
*/
int reset_slave(THD *thd, Master_info* mi)
{
  MY_STAT stat_area;
  char fname[FN_REFLEN];
  int thread_mask= 0, error= 0;
  uint sql_errno=ER_UNKNOWN_ERROR;
  const char* errmsg= "Unknown error occured while reseting slave";
  char master_info_file_tmp[FN_REFLEN];
  char relay_log_info_file_tmp[FN_REFLEN];
  DBUG_ENTER("reset_slave");

  lock_slave_threads(mi);
  init_thread_mask(&thread_mask,mi,0 /* not inverse */);
  if (thread_mask) // We refuse if any slave thread is running
  {
    unlock_slave_threads(mi);
    my_error(ER_SLAVE_MUST_STOP, MYF(0), (int) mi->connection_name.length,
             mi->connection_name.str);
    DBUG_RETURN(ER_SLAVE_MUST_STOP);
  }

  ha_reset_slave(thd);

  // delete relay logs, clear relay log coordinates
  if ((error= purge_relay_logs(&mi->rli, thd,
			       1 /* just reset */,
			       &errmsg)))
  {
    sql_errno= ER_RELAY_LOG_FAIL;
    goto err;
  }

  /* Clear master's log coordinates and associated information */
  mi->clear_in_memory_info(thd->lex->reset_slave_info.all);

  /*
     Reset errors (the idea is that we forget about the
     old master).
  */
  mi->clear_error();
  mi->rli.clear_error();
  mi->rli.clear_until_condition();

  // close master_info_file, relay_log_info_file, set mi->inited=rli->inited=0
  end_master_info(mi);

  // and delete these two files
  create_logfile_name_with_suffix(master_info_file_tmp,
                          sizeof(master_info_file_tmp),
                          master_info_file, 0, &mi->connection_name);
  create_logfile_name_with_suffix(relay_log_info_file_tmp,
                          sizeof(relay_log_info_file_tmp),
                          relay_log_info_file, 0, &mi->connection_name);

  fn_format(fname, master_info_file_tmp, mysql_data_home, "", 4+32);
  if (mysql_file_stat(key_file_master_info, fname, &stat_area, MYF(0)) &&
      mysql_file_delete(key_file_master_info, fname, MYF(MY_WME)))
  {
    error=1;
    goto err;
  }
  else if (global_system_variables.log_warnings > 1)
    sql_print_information("Deleted Master_info file '%s'.", fname);

  // delete relay_log_info_file
  fn_format(fname, relay_log_info_file_tmp, mysql_data_home, "", 4+32);
  if (mysql_file_stat(key_file_relay_log_info, fname, &stat_area, MYF(0)) &&
      mysql_file_delete(key_file_relay_log_info, fname, MYF(MY_WME)))
  {
    error=1;
    goto err;
  }
  else if (global_system_variables.log_warnings > 1)
    sql_print_information("Deleted Master_info file '%s'.", fname);

  RUN_HOOK(binlog_relay_io, after_reset_slave, (thd, mi));
err:
  unlock_slave_threads(mi);
  if (error)
    my_error(sql_errno, MYF(0), errmsg);
  DBUG_RETURN(error);
}

/*

  Kill all Binlog_dump threads which previously talked to the same slave
  ("same" means with the same server id). Indeed, if the slave stops, if the
  Binlog_dump thread is waiting (mysql_cond_wait) for binlog update, then it
  will keep existing until a query is written to the binlog. If the master is
  idle, then this could last long, and if the slave reconnects, we could have 2
  Binlog_dump threads in SHOW PROCESSLIST, until a query is written to the
  binlog. To avoid this, when the slave reconnects and sends COM_BINLOG_DUMP,
  the master kills any existing thread with the slave's server id (if this id
  is not zero; it will be true for real slaves, but false for mysqlbinlog when
  it sends COM_BINLOG_DUMP to get a remote binlog dump).

  SYNOPSIS
    kill_zombie_dump_threads()
    slave_server_id     the slave's server id

*/


void kill_zombie_dump_threads(uint32 slave_server_id)
{
  mysql_mutex_lock(&LOCK_thread_count);
  I_List_iterator<THD> it(threads);
  THD *tmp;

  while ((tmp=it++))
  {
    if (tmp->command == COM_BINLOG_DUMP &&
       tmp->variables.server_id == slave_server_id)
    {
      mysql_mutex_lock(&tmp->LOCK_thd_data);    // Lock from delete
      break;
    }
  }
  mysql_mutex_unlock(&LOCK_thread_count);
  if (tmp)
  {
    /*
      Here we do not call kill_one_thread() as
      it will be slow because it will iterate through the list
      again. We just to do kill the thread ourselves.
    */
    tmp->awake(KILL_QUERY);
    mysql_mutex_unlock(&tmp->LOCK_thd_data);
  }
}

/**
   Get value for a string parameter with error checking

   Note that in case of error the original string should not be updated!

   @ret 0 ok
   @ret 1 error
*/

static bool get_string_parameter(char *to, const char *from, size_t length,
                                 const char *name)
{
  if (from)                                     // Empty paramaters allowed
  {
    size_t from_length;
    if ((from_length= strlen(from)) > length)
    {
      my_error(ER_WRONG_STRING_LENGTH, MYF(0), from, name, (int) length);
      return 1;
    }
    memcpy(to, from, from_length+1);
  }
  return 0;
}


/**
  Execute a CHANGE MASTER statement.

  @param thd Pointer to THD object for the client thread executing the
  statement.

  @param mi Pointer to Master_info object belonging to the slave's IO
  thread.

  @param master_info_added Out parameter saying if the Master_info *mi was
  added to the global list of masters. This is useful in error conditions
  to know if caller should free Master_info *mi.

  @retval FALSE success
  @retval TRUE error
*/
bool change_master(THD* thd, Master_info* mi, bool *master_info_added)
{
  int thread_mask;
  const char* errmsg= 0;
  bool need_relay_log_purge= 1;
  bool ret= FALSE;
  char saved_host[HOSTNAME_LENGTH + 1];
  uint saved_port;
  char saved_log_name[FN_REFLEN];
  char master_info_file_tmp[FN_REFLEN];
  char relay_log_info_file_tmp[FN_REFLEN];
  my_off_t saved_log_pos;
  LEX_MASTER_INFO* lex_mi= &thd->lex->mi;
  slave_connection_state tmp_slave_state;
  DBUG_ENTER("change_master");

  *master_info_added= false;
  /* 
    We need to check if there is an empty master_host. Otherwise
    change master succeeds, a master.info file is created containing 
    empty master_host string and when issuing: start slave; an error
    is thrown stating that the server is not configured as slave.
    (See BUG#28796).
  */
  if (lex_mi->host && !*lex_mi->host) 
  {
    my_error(ER_WRONG_ARGUMENTS, MYF(0), "MASTER_HOST");
    DBUG_RETURN(TRUE);
  }
  if (master_info_index->check_duplicate_master_info(&lex_mi->connection_name,
                                                     lex_mi->host,
                                                     lex_mi->port))
    DBUG_RETURN(TRUE);

  lock_slave_threads(mi);
  init_thread_mask(&thread_mask,mi,0 /*not inverse*/);
  if (thread_mask) // We refuse if any slave thread is running
  {
    my_error(ER_SLAVE_MUST_STOP, MYF(0), (int) mi->connection_name.length,
             mi->connection_name.str);
    ret= TRUE;
    goto err;
  }

  if (lex_mi->gtid_pos_str.str)
  {
    if (master_info_index->give_error_if_slave_running())
    {
      ret= TRUE;
      goto err;
    }
    /*
      First load it into a dummy object, to check for parse errors.
      We do not want to wipe the previous state if there is an error
      in the syntax of the new state!
    */
    if (tmp_slave_state.load(lex_mi->gtid_pos_str.str,
                             lex_mi->gtid_pos_str.length))
    {
      my_error(ER_INCORRECT_GTID_STATE, MYF(0));
      ret= TRUE;
      goto err;
    }
  }

  thd_proc_info(thd, "Changing master");

  create_logfile_name_with_suffix(master_info_file_tmp,
                          sizeof(master_info_file_tmp),
                          master_info_file, 0, &mi->connection_name);
  create_logfile_name_with_suffix(relay_log_info_file_tmp,
                          sizeof(relay_log_info_file_tmp),
                          relay_log_info_file, 0, &mi->connection_name);

  /* if new Master_info doesn't exists, add it */
  if (!master_info_index->get_master_info(&mi->connection_name,
                                          MYSQL_ERROR::WARN_LEVEL_NOTE))
  {
    if (master_info_index->add_master_info(mi, TRUE))
    {
      my_error(ER_MASTER_INFO, MYF(0),
               (int) lex_mi->connection_name.length,
               lex_mi->connection_name.str);
      ret= TRUE;
      goto err;
    }
    *master_info_added= true;
  }
  if (global_system_variables.log_warnings > 1)
    sql_print_information("Master: '%.*s'  Master_info_file: '%s'  "
                          "Relay_info_file: '%s'",
                          (int) mi->connection_name.length,
                          mi->connection_name.str,
                          master_info_file_tmp, relay_log_info_file_tmp);

  if (init_master_info(mi, master_info_file_tmp, relay_log_info_file_tmp, 0,
		       thread_mask))
  {
    my_error(ER_MASTER_INFO, MYF(0),
             (int) lex_mi->connection_name.length,
             lex_mi->connection_name.str);
    ret= TRUE;
    goto err;
  }

  /*
    Data lock not needed since we have already stopped the running threads,
    and we have the hold on the run locks which will keep all threads that
    could possibly modify the data structures from running
  */

  /*
    Before processing the command, save the previous state.
  */
  strmake(saved_host, mi->host, HOSTNAME_LENGTH);
  saved_port= mi->port;
  strmake(saved_log_name, mi->master_log_name, FN_REFLEN - 1);
  saved_log_pos= mi->master_log_pos;

  /*
    If the user specified host or port without binlog or position,
    reset binlog's name to FIRST and position to 4.
  */

  if ((lex_mi->host || lex_mi->port) && !lex_mi->log_file_name && !lex_mi->pos)
  {
    mi->master_log_name[0] = 0;
    mi->master_log_pos= BIN_LOG_HEADER_SIZE;
  }

  if (lex_mi->log_file_name)
    strmake(mi->master_log_name, lex_mi->log_file_name,
	    sizeof(mi->master_log_name)-1);
  if (lex_mi->pos)
  {
    mi->master_log_pos= lex_mi->pos;
  }
  DBUG_PRINT("info", ("master_log_pos: %lu", (ulong) mi->master_log_pos));

  if (get_string_parameter(mi->host, lex_mi->host, sizeof(mi->host)-1,
                           "MASTER_HOST") ||
      get_string_parameter(mi->user, lex_mi->user, sizeof(mi->user)-1,
                           "MASTER_USER") ||
      get_string_parameter(mi->password, lex_mi->password,
                           sizeof(mi->password)-1, "MASTER_PASSWORD"))
  {
    ret= TRUE;
    goto err;
  }

  if (lex_mi->port)
    mi->port = lex_mi->port;
  if (lex_mi->connect_retry)
    mi->connect_retry = lex_mi->connect_retry;
  if (lex_mi->heartbeat_opt != LEX_MASTER_INFO::LEX_MI_UNCHANGED)
    mi->heartbeat_period = lex_mi->heartbeat_period;
  else
    mi->heartbeat_period= (float) min(SLAVE_MAX_HEARTBEAT_PERIOD,
                                      (slave_net_timeout/2.0));
  mi->received_heartbeats= LL(0); // counter lives until master is CHANGEd
  /*
    reset the last time server_id list if the current CHANGE MASTER 
    is mentioning IGNORE_SERVER_IDS= (...)
  */
  if (lex_mi->repl_ignore_server_ids_opt == LEX_MASTER_INFO::LEX_MI_ENABLE)
    reset_dynamic(&mi->ignore_server_ids);
  for (uint i= 0; i < lex_mi->repl_ignore_server_ids.elements; i++)
  {
    ulong s_id;
    get_dynamic(&lex_mi->repl_ignore_server_ids, (uchar*) &s_id, i);
    if (s_id == global_system_variables.server_id && replicate_same_server_id)
    {
      my_error(ER_SLAVE_IGNORE_SERVER_IDS, MYF(0), static_cast<int>(s_id));
      ret= TRUE;
      goto err;
    }
    else
    {
      if (bsearch((const ulong *) &s_id,
                  mi->ignore_server_ids.buffer,
                  mi->ignore_server_ids.elements, sizeof(ulong),
                  (int (*) (const void*, const void*))
                  change_master_server_id_cmp) == NULL)
        insert_dynamic(&mi->ignore_server_ids, (uchar*) &s_id);
    }
  }
  sort_dynamic(&mi->ignore_server_ids, (qsort_cmp) change_master_server_id_cmp);

  if (lex_mi->ssl != LEX_MASTER_INFO::LEX_MI_UNCHANGED)
    mi->ssl= (lex_mi->ssl == LEX_MASTER_INFO::LEX_MI_ENABLE);

  if (lex_mi->ssl_verify_server_cert != LEX_MASTER_INFO::LEX_MI_UNCHANGED)
    mi->ssl_verify_server_cert=
      (lex_mi->ssl_verify_server_cert == LEX_MASTER_INFO::LEX_MI_ENABLE);

  if (lex_mi->ssl_ca)
    strmake(mi->ssl_ca, lex_mi->ssl_ca, sizeof(mi->ssl_ca)-1);
  if (lex_mi->ssl_capath)
    strmake(mi->ssl_capath, lex_mi->ssl_capath, sizeof(mi->ssl_capath)-1);
  if (lex_mi->ssl_cert)
    strmake(mi->ssl_cert, lex_mi->ssl_cert, sizeof(mi->ssl_cert)-1);
  if (lex_mi->ssl_cipher)
    strmake(mi->ssl_cipher, lex_mi->ssl_cipher, sizeof(mi->ssl_cipher)-1);
  if (lex_mi->ssl_key)
    strmake(mi->ssl_key, lex_mi->ssl_key, sizeof(mi->ssl_key)-1);
#ifndef HAVE_OPENSSL
  if (lex_mi->ssl || lex_mi->ssl_ca || lex_mi->ssl_capath ||
      lex_mi->ssl_cert || lex_mi->ssl_cipher || lex_mi->ssl_key ||
      lex_mi->ssl_verify_server_cert )
    push_warning(thd, MYSQL_ERROR::WARN_LEVEL_NOTE,
                 ER_SLAVE_IGNORED_SSL_PARAMS, ER(ER_SLAVE_IGNORED_SSL_PARAMS));
#endif

  if (lex_mi->relay_log_name)
  {
    need_relay_log_purge= 0;
    char relay_log_name[FN_REFLEN];
    mi->rli.relay_log.make_log_name(relay_log_name, lex_mi->relay_log_name);
    strmake(mi->rli.group_relay_log_name, relay_log_name,
	    sizeof(mi->rli.group_relay_log_name)-1);
    strmake(mi->rli.event_relay_log_name, relay_log_name,
	    sizeof(mi->rli.event_relay_log_name)-1);
  }

  if (lex_mi->relay_log_pos)
  {
    need_relay_log_purge= 0;
    mi->rli.group_relay_log_pos= mi->rli.event_relay_log_pos= lex_mi->relay_log_pos;
  }

  if (lex_mi->gtid_pos_auto || lex_mi->gtid_pos_str.str)
    mi->gtid_pos_auto= true;
  else if (lex_mi->gtid_pos_str.str ||
           lex_mi->log_file_name || lex_mi->pos ||
           lex_mi->relay_log_name || lex_mi->relay_log_pos)
    mi->gtid_pos_auto= false;

  /*
    If user did specify neither host nor port nor any log name nor any log
    pos, i.e. he specified only user/password/master_connect_retry, he probably
    wants replication to resume from where it had left, i.e. from the
    coordinates of the **SQL** thread (imagine the case where the I/O is ahead
    of the SQL; restarting from the coordinates of the I/O would lose some
    events which is probably unwanted when you are just doing minor changes
    like changing master_connect_retry).
    A side-effect is that if only the I/O thread was started, this thread may
    restart from ''/4 after the CHANGE MASTER. That's a minor problem (it is a
    much more unlikely situation than the one we are fixing here).
    Note: coordinates of the SQL thread must be read here, before the
    'if (need_relay_log_purge)' block which resets them.
  */
  if (!lex_mi->host && !lex_mi->port &&
      !lex_mi->log_file_name && !lex_mi->pos &&
      need_relay_log_purge)
   {
     /*
       Sometimes mi->rli.master_log_pos == 0 (it happens when the SQL thread is
       not initialized), so we use a max().
       What happens to mi->rli.master_log_pos during the initialization stages
       of replication is not 100% clear, so we guard against problems using
       max().
      */
     mi->master_log_pos = max(BIN_LOG_HEADER_SIZE,
			      mi->rli.group_master_log_pos);
     strmake(mi->master_log_name, mi->rli.group_master_log_name,
             sizeof(mi->master_log_name)-1);
  }

  if (lex_mi->gtid_pos_str.str)
  {
    if (rpl_global_gtid_slave_state.load(thd, lex_mi->gtid_pos_str.str,
                                         lex_mi->gtid_pos_str.length, true))
    {
      my_error(ER_FAILED_GTID_STATE_INIT, MYF(0));
      ret= TRUE;
      goto err;
    }
  }

  /*
    Relay log's IO_CACHE may not be inited, if rli->inited==0 (server was never
    a slave before).
  */
  if (flush_master_info(mi, FALSE, FALSE))
  {
    my_error(ER_RELAY_LOG_INIT, MYF(0), "Failed to flush master info file");
    ret= TRUE;
    goto err;
  }
  if (need_relay_log_purge)
  {
    relay_log_purge= 1;
    thd_proc_info(thd, "Purging old relay logs");
    if (purge_relay_logs(&mi->rli, thd,
			 0 /* not only reset, but also reinit */,
			 &errmsg))
    {
      my_error(ER_RELAY_LOG_FAIL, MYF(0), errmsg);
      ret= TRUE;
      goto err;
    }
  }
  else
  {
    const char* msg;
    relay_log_purge= 0;
    /* Relay log is already initialized */
    if (init_relay_log_pos(&mi->rli,
			   mi->rli.group_relay_log_name,
			   mi->rli.group_relay_log_pos,
			   0 /*no data lock*/,
			   &msg, 0))
    {
      my_error(ER_RELAY_LOG_INIT, MYF(0), msg);
      ret= TRUE;
      goto err;
    }
  }
  /*
    Coordinates in rli were spoilt by the 'if (need_relay_log_purge)' block,
    so restore them to good values. If we left them to ''/0, that would work;
    but that would fail in the case of 2 successive CHANGE MASTER (without a
    START SLAVE in between): because first one would set the coords in mi to
    the good values of those in rli, the set those in rli to ''/0, then
    second CHANGE MASTER would set the coords in mi to those of rli, i.e. to
    ''/0: we have lost all copies of the original good coordinates.
    That's why we always save good coords in rli.
  */
  mi->rli.group_master_log_pos= mi->master_log_pos;
  DBUG_PRINT("info", ("master_log_pos: %lu", (ulong) mi->master_log_pos));
  strmake(mi->rli.group_master_log_name,mi->master_log_name,
	  sizeof(mi->rli.group_master_log_name)-1);

  if (!mi->rli.group_master_log_name[0]) // uninitialized case
    mi->rli.group_master_log_pos=0;

  mysql_mutex_lock(&mi->rli.data_lock);
  mi->rli.abort_pos_wait++; /* for MASTER_POS_WAIT() to abort */
  /* Clear the errors, for a clean start */
  mi->rli.clear_error();
  mi->rli.clear_until_condition();

  sql_print_information("'CHANGE MASTER TO executed'. "
    "Previous state master_host='%s', master_port='%u', master_log_file='%s', "
    "master_log_pos='%ld'. "
    "New state master_host='%s', master_port='%u', master_log_file='%s', "
    "master_log_pos='%ld'.", saved_host, saved_port, saved_log_name,
    (ulong) saved_log_pos, mi->host, mi->port, mi->master_log_name,
    (ulong) mi->master_log_pos);

  /*
    If we don't write new coordinates to disk now, then old will remain in
    relay-log.info until START SLAVE is issued; but if mysqld is shutdown
    before START SLAVE, then old will remain in relay-log.info, and will be the
    in-memory value at restart (thus causing errors, as the old relay log does
    not exist anymore).
  */
  flush_relay_log_info(&mi->rli);
  mysql_cond_broadcast(&mi->data_cond);
  mysql_mutex_unlock(&mi->rli.data_lock);

err:
  unlock_slave_threads(mi);
  thd_proc_info(thd, 0);
  if (ret == FALSE)
    my_ok(thd);
  DBUG_RETURN(ret);
}


/**
  Execute a RESET MASTER statement.

  @param thd Pointer to THD object of the client thread executing the
  statement.

  @retval 0 success
  @retval 1 error
*/
int reset_master(THD* thd)
{
  if (!mysql_bin_log.is_open())
  {
    my_message(ER_FLUSH_MASTER_BINLOG_CLOSED,
               ER(ER_FLUSH_MASTER_BINLOG_CLOSED), MYF(ME_BELL+ME_WAITTANG));
    return 1;
  }

  if (mysql_bin_log.reset_logs(thd, 1))
    return 1;
  RUN_HOOK(binlog_transmit, after_reset_master, (thd, 0 /* flags */));
  return 0;
}


/**
  Execute a SHOW BINLOG EVENTS statement.

  @param thd Pointer to THD object for the client thread executing the
  statement.

  @retval FALSE success
  @retval TRUE failure
*/
bool mysql_show_binlog_events(THD* thd)
{
  Protocol *protocol= thd->protocol;
  List<Item> field_list;
  const char *errmsg = 0;
  bool ret = TRUE;
  IO_CACHE log;
  File file = -1;
  MYSQL_BIN_LOG *binary_log= NULL;
  int old_max_allowed_packet= thd->variables.max_allowed_packet;
  Master_info *mi= 0;
  LOG_INFO linfo;

  DBUG_ENTER("mysql_show_binlog_events");

  Log_event::init_show_field_list(&field_list);
  if (protocol->send_result_set_metadata(&field_list,
                            Protocol::SEND_NUM_ROWS | Protocol::SEND_EOF))
    DBUG_RETURN(TRUE);

  Format_description_log_event *description_event= new
    Format_description_log_event(3); /* MySQL 4.0 by default */

  DBUG_ASSERT(thd->lex->sql_command == SQLCOM_SHOW_BINLOG_EVENTS ||
              thd->lex->sql_command == SQLCOM_SHOW_RELAYLOG_EVENTS);

  /* select wich binary log to use: binlog or relay */
  if ( thd->lex->sql_command == SQLCOM_SHOW_BINLOG_EVENTS )
  {
    /*
      Wait for handlers to insert any pending information
      into the binlog.  For e.g. ndb which updates the binlog asynchronously
      this is needed so that the uses sees all its own commands in the binlog
    */
    ha_binlog_wait(thd);

    binary_log= &mysql_bin_log;
  }
  else  /* showing relay log contents */
  {
    mysql_mutex_lock(&LOCK_active_mi);
    if (!(mi= master_info_index->
          get_master_info(&thd->variables.default_master_connection,
                          MYSQL_ERROR::WARN_LEVEL_ERROR)))
    {
      mysql_mutex_unlock(&LOCK_active_mi);
      DBUG_RETURN(TRUE);
    }
    binary_log= &(mi->rli.relay_log);
  }

  if (binary_log->is_open())
  {
    LEX_MASTER_INFO *lex_mi= &thd->lex->mi;
    SELECT_LEX_UNIT *unit= &thd->lex->unit;
    ha_rows event_count, limit_start, limit_end;
    my_off_t pos = max(BIN_LOG_HEADER_SIZE, lex_mi->pos); // user-friendly
    char search_file_name[FN_REFLEN], *name;
    const char *log_file_name = lex_mi->log_file_name;
    mysql_mutex_t *log_lock = binary_log->get_log_lock();
    Log_event* ev;

    if (mi)
    {
      /* We can unlock the mutex as we have a lock on the file */
      mysql_mutex_unlock(&LOCK_active_mi);
      mi= 0;
    }

    unit->set_limit(thd->lex->current_select);
    limit_start= unit->offset_limit_cnt;
    limit_end= unit->select_limit_cnt;

    name= search_file_name;
    if (log_file_name)
      binary_log->make_log_name(search_file_name, log_file_name);
    else
      name=0;					// Find first log

    linfo.index_file_offset = 0;

    if (binary_log->find_log_pos(&linfo, name, 1))
    {
      errmsg = "Could not find target log";
      goto err;
    }

    mysql_mutex_lock(&LOCK_thread_count);
    thd->current_linfo = &linfo;
    mysql_mutex_unlock(&LOCK_thread_count);

    if ((file=open_binlog(&log, linfo.log_file_name, &errmsg)) < 0)
      goto err;

    /*
      to account binlog event header size
    */
    thd->variables.max_allowed_packet += MAX_LOG_EVENT_HEADER;

    mysql_mutex_lock(log_lock);

    /*
      open_binlog() sought to position 4.
      Read the first event in case it's a Format_description_log_event, to
      know the format. If there's no such event, we are 3.23 or 4.x. This
      code, like before, can't read 3.23 binlogs.
      This code will fail on a mixed relay log (one which has Format_desc then
      Rotate then Format_desc).
    */
    ev= Log_event::read_log_event(&log, (mysql_mutex_t*)0, description_event,
                                   opt_master_verify_checksum);
    if (ev)
    {
      if (ev->get_type_code() == FORMAT_DESCRIPTION_EVENT)
      {
        delete description_event;
        description_event= (Format_description_log_event*) ev;
      }
      else
        delete ev;
    }

    my_b_seek(&log, pos);

    if (!description_event->is_valid())
    {
      errmsg="Invalid Format_description event; could be out of memory";
      goto err;
    }

    for (event_count = 0;
         (ev = Log_event::read_log_event(&log, (mysql_mutex_t*) 0,
                                         description_event,
                                         opt_master_verify_checksum)); )
    {
      if (ev->get_type_code() == FORMAT_DESCRIPTION_EVENT)
        description_event->checksum_alg= ev->checksum_alg;

      if (event_count >= limit_start &&
	  ev->net_send(thd, protocol, linfo.log_file_name, pos))
      {
	errmsg = "Net error";
	delete ev;
        mysql_mutex_unlock(log_lock);
	goto err;
      }

      pos = my_b_tell(&log);
      delete ev;

      if (++event_count >= limit_end)
	break;
    }

    if (event_count < limit_end && log.error)
    {
      errmsg = "Wrong offset or I/O error";
      mysql_mutex_unlock(log_lock);
      goto err;
    }

    mysql_mutex_unlock(log_lock);
  }
  else if (mi)
    mysql_mutex_unlock(&LOCK_active_mi);

  // Check that linfo is still on the function scope.
  DEBUG_SYNC(thd, "after_show_binlog_events");

  ret= FALSE;

err:
  delete description_event;
  if (file >= 0)
  {
    end_io_cache(&log);
    mysql_file_close(file, MYF(MY_WME));
  }

  if (errmsg)
    my_error(ER_ERROR_WHEN_EXECUTING_COMMAND, MYF(0),
             "SHOW BINLOG EVENTS", errmsg);
  else
    my_eof(thd);

  mysql_mutex_lock(&LOCK_thread_count);
  thd->current_linfo = 0;
  mysql_mutex_unlock(&LOCK_thread_count);
  thd->variables.max_allowed_packet= old_max_allowed_packet;
  DBUG_RETURN(ret);
}


/**
  Execute a SHOW MASTER STATUS statement.

  @param thd Pointer to THD object for the client thread executing the
  statement.

  @retval FALSE success
  @retval TRUE failure
*/
bool show_binlog_info(THD* thd)
{
  Protocol *protocol= thd->protocol;
  DBUG_ENTER("show_binlog_info");
  List<Item> field_list;
  field_list.push_back(new Item_empty_string("File", FN_REFLEN));
  field_list.push_back(new Item_return_int("Position",20,
					   MYSQL_TYPE_LONGLONG));
  field_list.push_back(new Item_empty_string("Binlog_Do_DB",255));
  field_list.push_back(new Item_empty_string("Binlog_Ignore_DB",255));

  if (protocol->send_result_set_metadata(&field_list,
                            Protocol::SEND_NUM_ROWS | Protocol::SEND_EOF))
    DBUG_RETURN(TRUE);
  protocol->prepare_for_resend();

  if (mysql_bin_log.is_open())
  {
    LOG_INFO li;
    mysql_bin_log.get_current_log(&li);
    int dir_len = dirname_length(li.log_file_name);
    protocol->store(li.log_file_name + dir_len, &my_charset_bin);
    protocol->store((ulonglong) li.pos);
    protocol->store(binlog_filter->get_do_db());
    protocol->store(binlog_filter->get_ignore_db());
    if (protocol->write())
      DBUG_RETURN(TRUE);
  }
  my_eof(thd);
  DBUG_RETURN(FALSE);
}


/**
  Execute a SHOW BINARY LOGS statement.

  @param thd Pointer to THD object for the client thread executing the
  statement.

  @retval FALSE success
  @retval TRUE failure
*/
bool show_binlogs(THD* thd)
{
  IO_CACHE *index_file;
  LOG_INFO cur;
  File file;
  char fname[FN_REFLEN];
  List<Item> field_list;
  uint length;
  int cur_dir_len;
  Protocol *protocol= thd->protocol;
  DBUG_ENTER("show_binlogs");

  if (!mysql_bin_log.is_open())
  {
    my_error(ER_NO_BINARY_LOGGING, MYF(0));
    DBUG_RETURN(TRUE);
  }

  field_list.push_back(new Item_empty_string("Log_name", 255));
  field_list.push_back(new Item_return_int("File_size", 20,
                                           MYSQL_TYPE_LONGLONG));
  if (protocol->send_result_set_metadata(&field_list,
                            Protocol::SEND_NUM_ROWS | Protocol::SEND_EOF))
    DBUG_RETURN(TRUE);
  
  mysql_mutex_lock(mysql_bin_log.get_log_lock());
  mysql_bin_log.lock_index();
  index_file=mysql_bin_log.get_index_file();
  
  mysql_bin_log.raw_get_current_log(&cur); // dont take mutex
  mysql_mutex_unlock(mysql_bin_log.get_log_lock()); // lockdep, OK
  
  cur_dir_len= dirname_length(cur.log_file_name);

  reinit_io_cache(index_file, READ_CACHE, (my_off_t) 0, 0, 0);

  /* The file ends with EOF or empty line */
  while ((length=my_b_gets(index_file, fname, sizeof(fname))) > 1)
  {
    int dir_len;
    ulonglong file_length= 0;                   // Length if open fails
    fname[--length] = '\0';                     // remove the newline

    protocol->prepare_for_resend();
    dir_len= dirname_length(fname);
    length-= dir_len;
    protocol->store(fname + dir_len, length, &my_charset_bin);

    if (!(strncmp(fname+dir_len, cur.log_file_name+cur_dir_len, length)))
      file_length= cur.pos;  /* The active log, use the active position */
    else
    {
      /* this is an old log, open it and find the size */
      if ((file= mysql_file_open(key_file_binlog,
                                 fname, O_RDONLY | O_SHARE | O_BINARY,
                                 MYF(0))) >= 0)
      {
        file_length= (ulonglong) mysql_file_seek(file, 0L, MY_SEEK_END, MYF(0));
        mysql_file_close(file, MYF(0));
      }
    }
    protocol->store(file_length);
    if (protocol->write())
      goto err;
  }
  mysql_bin_log.unlock_index();
  my_eof(thd);
  DBUG_RETURN(FALSE);

err:
  mysql_bin_log.unlock_index();
  DBUG_RETURN(TRUE);
}

/**
   Load data's io cache specific hook to be executed
   before a chunk of data is being read into the cache's buffer
   The fuction instantianates and writes into the binlog
   replication events along LOAD DATA processing.
   
   @param file  pointer to io-cache
   @retval 0 success
   @retval 1 failure
*/
int log_loaded_block(IO_CACHE* file)
{
  DBUG_ENTER("log_loaded_block");
  LOAD_FILE_INFO *lf_info;
  uint block_len;
  /* buffer contains position where we started last read */
  uchar* buffer= (uchar*) my_b_get_buffer_start(file);
  uint max_event_size= current_thd->variables.max_allowed_packet;
  lf_info= (LOAD_FILE_INFO*) file->arg;
  if (lf_info->thd->is_current_stmt_binlog_format_row())
    DBUG_RETURN(0);
  if (lf_info->last_pos_in_file != HA_POS_ERROR &&
      lf_info->last_pos_in_file >= my_b_get_pos_in_file(file))
    DBUG_RETURN(0);
  
  for (block_len= (uint) (my_b_get_bytes_in_buffer(file)); block_len > 0;
       buffer += min(block_len, max_event_size),
       block_len -= min(block_len, max_event_size))
  {
    lf_info->last_pos_in_file= my_b_get_pos_in_file(file);
    if (lf_info->wrote_create_file)
    {
      Append_block_log_event a(lf_info->thd, lf_info->thd->db, buffer,
                               min(block_len, max_event_size),
                               lf_info->log_delayed);
      if (mysql_bin_log.write(&a))
        DBUG_RETURN(1);
    }
    else
    {
      Begin_load_query_log_event b(lf_info->thd, lf_info->thd->db,
                                   buffer,
                                   min(block_len, max_event_size),
                                   lf_info->log_delayed);
      if (mysql_bin_log.write(&b))
        DBUG_RETURN(1);
      lf_info->wrote_create_file= 1;
    }
  }
  DBUG_RETURN(0);
}


/**
   Initialise the slave replication state from the mysql.rpl_slave_state table.

   This is called each time an SQL thread starts, but the data is only actually
   loaded on the first call.

   The slave state is the last GTID applied on the slave within each
   replication domain.

   To avoid row lock contention, there are multiple rows for each domain_id.
   The one containing the current slave state is the one with the maximal
   sub_id value, within each domain_id.

    CREATE TABLE mysql.rpl_slave_state (
      domain_id INT UNSIGNED NOT NULL,
      sub_id BIGINT UNSIGNED NOT NULL,
      server_id INT UNSIGNED NOT NULL,
      seq_no BIGINT UNSIGNED NOT NULL,
      PRIMARY KEY (domain_id, sub_id))
*/

void
rpl_init_gtid_slave_state()
{
  rpl_global_gtid_slave_state.init();
}


void
rpl_deinit_gtid_slave_state()
{
  rpl_global_gtid_slave_state.deinit();
}


int
rpl_load_gtid_slave_state(THD *thd)
{
  TABLE_LIST tlist;
  TABLE *table;
  bool table_opened= false;
  bool table_scanned= false;
  struct local_element { uint64 sub_id; rpl_gtid gtid; };
  struct local_element *entry;
  HASH hash;
  int err= 0;
  uint32 i;
  DBUG_ENTER("rpl_load_gtid_slave_state");

  my_hash_init(&hash, &my_charset_bin, 32,
               offsetof(local_element, gtid) + offsetof(rpl_gtid, domain_id),
               sizeof(uint32), NULL, my_free, HASH_UNIQUE);

  rpl_global_gtid_slave_state.lock();
  bool loaded= rpl_global_gtid_slave_state.loaded;
  rpl_global_gtid_slave_state.unlock();
  if (loaded)
    goto end;

  mysql_reset_thd_for_next_command(thd, 0);

  tlist.init_one_table(STRING_WITH_LEN("mysql"),
                       rpl_gtid_slave_state_table_name.str,
                       rpl_gtid_slave_state_table_name.length,
                       NULL, TL_READ);
  if ((err= open_and_lock_tables(thd, &tlist, FALSE, 0)))
    goto end;
  table_opened= true;
  table= tlist.table;
  table->no_replicate= 1;

  /*
    ToDo: Check the table definition, error if not as expected.
    We need the correct first 4 columns with correct type, and the primary key.
  */

  bitmap_set_bit(table->read_set, table->field[0]->field_index);
  bitmap_set_bit(table->read_set, table->field[1]->field_index);
  bitmap_set_bit(table->read_set, table->field[2]->field_index);
  bitmap_set_bit(table->read_set, table->field[3]->field_index);
  if ((err= table->file->ha_rnd_init_with_error(1)))
    goto end;
  table_scanned= true;
  for (;;)
  {
    uint32 domain_id, server_id;
    uint64 sub_id, seq_no;
    uchar *rec;

    if ((err= table->file->ha_rnd_next(table->record[0])))
    {
      if (err == HA_ERR_RECORD_DELETED)
        continue;
      else if (err == HA_ERR_END_OF_FILE)
        break;
      else
        goto end;
    }
    domain_id= (ulonglong)table->field[0]->val_int();
    sub_id= (ulonglong)table->field[1]->val_int();
    server_id= (ulonglong)table->field[2]->val_int();
    seq_no= (ulonglong)table->field[3]->val_int();
    DBUG_PRINT("info", ("Read slave state row: %u:%u-%lu sub_id=%lu\n",
                        (unsigned)domain_id, (unsigned)server_id,
                        (ulong)seq_no, (ulong)sub_id));
    if ((rec= my_hash_search(&hash, (const uchar *)&domain_id, 0)))
    {
      entry= (struct local_element *)rec;
      if (entry->sub_id >= sub_id)
        continue;
    }
    else
    {
      if (!(entry= (struct local_element *)my_malloc(sizeof(*entry),
                                                     MYF(MY_WME))))
      {
        err= 1;
        goto end;
      }
      if ((err= my_hash_insert(&hash, (uchar *)entry)))
      {
        my_free(entry);
        goto end;
      }
    }

    entry->sub_id= sub_id;
    entry->gtid.domain_id= domain_id;
    entry->gtid.server_id= server_id;
    entry->gtid.seq_no= seq_no;
  }

  rpl_global_gtid_slave_state.lock();
  for (i= 0; i < hash.records; ++i)
  {
    entry= (struct local_element *)my_hash_element(&hash, i);
    if ((err= rpl_global_gtid_slave_state.update(entry->gtid.domain_id,
                                                 entry->gtid.server_id,
                                                 entry->sub_id,
                                                 entry->gtid.seq_no)))
    {
      rpl_global_gtid_slave_state.unlock();
      goto end;
    }
  }
  rpl_global_gtid_slave_state.loaded= true;
  rpl_global_gtid_slave_state.unlock();

  err= 0;                                       /* Clear HA_ERR_END_OF_FILE */

end:
  if (table_scanned)
  {
    table->file->ha_index_or_rnd_end();
    ha_commit_trans(thd, FALSE);
    ha_commit_trans(thd, TRUE);
  }
  if (table_opened)
    close_thread_tables(thd);
  my_hash_free(&hash);
  DBUG_RETURN(err);
}

#endif /* HAVE_REPLICATION */
