/* Copyright (C) 2000 MySQL AB & MySQL Finland AB & TCX DataKonsult AB
   
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


/* logging of commands */
/* TODO: Abort logging when we get an error in reading or writing log files */

#ifdef __EMX__
#include <io.h>
#endif

#include "mysql_priv.h"
#include "sql_acl.h"
#include "sql_repl.h"

#include <my_dir.h>
#include <stdarg.h>
#include <m_ctype.h>				// For test_if_number
#include <assert.h>

MYSQL_LOG mysql_log,mysql_update_log,mysql_slow_log,mysql_bin_log;
extern I_List<i_string> binlog_do_db, binlog_ignore_db;
extern ulong max_binlog_size;

static bool test_if_number(const char *str,
			   long *res, bool allow_wildcards);

/****************************************************************************
** Find a uniq filename for 'filename.#'.
** Set # to a number as low as possible
** returns != 0 if not possible to get uniq filename
****************************************************************************/

static int find_uniq_filename(char *name)
{
  long		number;
  uint		i,length;
  char		buff[FN_REFLEN];
  struct st_my_dir *dir_info;
  reg1 struct fileinfo *file_info;
  ulong		max_found=0;
  DBUG_ENTER("find_uniq_filename");

  length=dirname_part(buff,name);
  char *start=name+length,*end=strend(start);
  *end='.';
  length= (uint) (end-start+1);

  if (!(dir_info = my_dir(buff,MYF(MY_DONT_SORT))))
  {						// This shouldn't happen
    strmov(end,".1");				// use name+1
    DBUG_RETURN(0);
  }
  file_info= dir_info->dir_entry;
  for (i=dir_info->number_off_files ; i-- ; file_info++)
  {
    if (bcmp(file_info->name,start,length) == 0 &&
	test_if_number(file_info->name+length, &number,0))
    {
      set_if_bigger(max_found,(ulong) number);
    }
  }
  my_dirend(dir_info);

  *end++='.';
  sprintf(end,"%03ld",max_found+1);
  DBUG_RETURN(0);
}

MYSQL_LOG::MYSQL_LOG(): last_time(0), query_start(0),index_file(-1),
			name(0), log_type(LOG_CLOSED),write_error(0),
			inited(0), file_id(1),no_rotate(0),
			need_start_event(1)
{
  /*
    We don't want to intialize LOCK_Log here as the thread system may
    not have been initailized yet. We do it instead at 'open'.
  */
  index_file_name[0] = 0;
  bzero((char*) &log_file,sizeof(log_file));
}

MYSQL_LOG::~MYSQL_LOG()
{
  if (inited)
  {
    (void) pthread_mutex_destroy(&LOCK_log);
    (void) pthread_mutex_destroy(&LOCK_index);
  }
}

void MYSQL_LOG::set_index_file_name(const char* index_file_name)
{
  if (index_file_name)
    fn_format(this->index_file_name,index_file_name,mysql_data_home,".index",
	      4);
  else
    this->index_file_name[0] = 0;
}


int MYSQL_LOG::generate_new_name(char *new_name, const char *log_name)
{      
  if (log_type == LOG_NORMAL)
    fn_format(new_name,log_name,mysql_data_home,"",4);
  else
  {
    fn_format(new_name,log_name,mysql_data_home,"",4);
    if (!fn_ext(log_name)[0])
    {
      if (find_uniq_filename(new_name))
      {
	sql_print_error(ER(ER_NO_UNIQUE_LOGFILE), log_name);
	return 1;
      }
    }
  }
  return 0;
}

bool MYSQL_LOG::open_index( int options)
{
  return (index_file < 0 && 
	 (index_file = my_open(index_file_name, options | O_BINARY ,
			       MYF(MY_WME))) < 0);
}

void MYSQL_LOG::init(enum_log_type log_type_arg,
		     enum cache_type io_cache_type_arg,
		     bool no_auto_events_arg)
{
  log_type = log_type_arg;
  io_cache_type = io_cache_type_arg;
  no_auto_events = no_auto_events_arg;
  if (!inited)
  {
    inited=1;
    (void) pthread_mutex_init(&LOCK_log,MY_MUTEX_INIT_SLOW);
    (void) pthread_mutex_init(&LOCK_index, MY_MUTEX_INIT_SLOW);
    (void) pthread_cond_init(&update_cond, 0);
  }
}

void MYSQL_LOG::close_index()
{
  if (index_file >= 0)
  {
    my_close(index_file, MYF(0));
    index_file = -1;
  }
}

void MYSQL_LOG::open(const char *log_name, enum_log_type log_type_arg,
		     const char *new_name, enum cache_type io_cache_type_arg,
		     bool no_auto_events_arg)
{
  MY_STAT tmp_stat;
  char buff[512];
  File file= -1;
  bool do_magic;
  int open_flags = O_CREAT | O_APPEND | O_BINARY;
  if (!inited && log_type_arg == LOG_BIN && *fn_ext(log_name))
      no_rotate = 1;
  init(log_type_arg,io_cache_type_arg,no_auto_events_arg);
  
  if (!(name=my_strdup(log_name,MYF(MY_WME))))
    goto err;
  if (new_name)
    strmov(log_file_name,new_name);
  else if (generate_new_name(log_file_name, name))
    goto err;
  
  if (io_cache_type == SEQ_READ_APPEND)
    open_flags |= O_RDWR;
  else
    open_flags |= O_WRONLY;
  
  if (log_type == LOG_BIN && !index_file_name[0])
    fn_format(index_file_name, name, mysql_data_home, ".index", 6);
  
  db[0]=0;
  do_magic = ((log_type == LOG_BIN) && !my_stat(log_file_name,
						&tmp_stat, MYF(0)));
  
  if ((file=my_open(log_file_name,open_flags,
		    MYF(MY_WME | ME_WAITTANG))) < 0 ||
      init_io_cache(&log_file, file, IO_SIZE, io_cache_type,
		    my_tell(file,MYF(MY_WME)), 0, MYF(MY_WME | MY_NABP)))
    goto err;

  if (log_type == LOG_NORMAL)
  {
    char *end;
#ifdef __NT__
    sprintf(buff, "%s, Version: %s, started with:\nTCP Port: %d, Named Pipe: %s\n", my_progname, server_version, mysql_port, mysql_unix_port);
#else
    sprintf(buff, "%s, Version: %s, started with:\nTcp port: %d  Unix socket: %s\n", my_progname,server_version,mysql_port,mysql_unix_port);
#endif
    end=strmov(strend(buff),"Time                 Id Command    Argument\n");
    if (my_b_write(&log_file, (byte*) buff,(uint) (end-buff)) ||
	flush_io_cache(&log_file))
      goto err;
  }
  else if (log_type == LOG_NEW)
  {
    time_t skr=time(NULL);
    struct tm tm_tmp;
    localtime_r(&skr,&tm_tmp);
    sprintf(buff,"# %s, Version: %s at %02d%02d%02d %2d:%02d:%02d\n",
	    my_progname,server_version,
	    tm_tmp.tm_year % 100,
	    tm_tmp.tm_mon+1,
	    tm_tmp.tm_mday,
	    tm_tmp.tm_hour,
	    tm_tmp.tm_min,
	    tm_tmp.tm_sec);
    if (my_b_write(&log_file, (byte*) buff,(uint) strlen(buff)) ||
	flush_io_cache(&log_file))
      goto err;
  }
  else if (log_type == LOG_BIN)
  {
      bool error;
    /*
      Explanation of the boolean black magic:
      if we are supposed to write magic number try write
      clean up if failed
      then if index_file has not been previously opened, try to open it
      clean up if failed
    */
    if ((do_magic && my_b_write(&log_file, (byte*) BINLOG_MAGIC, 4)) ||
	open_index(O_APPEND | O_RDWR | O_CREAT))
      goto err;

    if (need_start_event && !no_auto_events)
    {
      Start_log_event s;
      s.set_log_pos(this);
      s.write(&log_file);
      need_start_event=0;
    }
    flush_io_cache(&log_file);
    pthread_mutex_lock(&LOCK_index);
    error=(my_write(index_file, (byte*) log_file_name, strlen(log_file_name),
		    MYF(MY_NABP | MY_WME)) ||
	   my_write(index_file, (byte*) "\n", 1, MYF(MY_NABP | MY_WME)));
    pthread_mutex_unlock(&LOCK_index);
    if (error)
    {
      close_index();
      goto err;
    }
  }
  return;

err:
  sql_print_error("Could not use %s for logging (error %d)", log_name,errno);
  if (file >= 0)
    my_close(file,MYF(0));
  end_io_cache(&log_file);
  x_free(name); name=0;
  log_type=LOG_CLOSED;
  return;
}

int MYSQL_LOG::get_current_log(LOG_INFO* linfo)
{
  pthread_mutex_lock(&LOCK_log);
  strmake(linfo->log_file_name, log_file_name, sizeof(linfo->log_file_name)-1);
  linfo->pos = my_b_tell(&log_file);
  pthread_mutex_unlock(&LOCK_log);
  return 0;
}

// if log_name is "" we stop at the first entry
int MYSQL_LOG::find_first_log(LOG_INFO* linfo, const char* log_name,
			      bool need_mutex)
{
  if (index_file < 0)
    return LOG_INFO_INVALID;
  int error = 0;
  char* fname = linfo->log_file_name;
  uint log_name_len = (uint) strlen(log_name);
  IO_CACHE io_cache;

  // mutex needed because we need to make sure the file pointer does not move
  // from under our feet
  if (need_mutex)
    pthread_mutex_lock(&LOCK_index);
  if (init_io_cache(&io_cache, index_file, IO_SIZE, READ_CACHE, (my_off_t) 0,
		    0, MYF(MY_WME)))
  {
    error = LOG_INFO_SEEK;
    goto err;
  }
  for(;;)
  {
    uint length;
    if (!(length=my_b_gets(&io_cache, fname, FN_REFLEN-1)))
    {
      error = !io_cache.error ? LOG_INFO_EOF : LOG_INFO_IO;
      goto err;
    }

    // if the log entry matches, empty string matching anything
    if (!log_name_len ||
	(log_name_len == length-1 && fname[log_name_len] == '\n' &&
	 !memcmp(fname, log_name, log_name_len)))
    {
      fname[length-1]=0;			// remove last \n
      linfo->index_file_offset = my_b_tell(&io_cache);
      break;
    }
  }
  error = 0;

err:
  if (need_mutex)
    pthread_mutex_unlock(&LOCK_index);
  end_io_cache(&io_cache);
  return error;
     
}


int MYSQL_LOG::find_next_log(LOG_INFO* linfo, bool need_lock)
{
  // mutex needed because we need to make sure the file pointer does not move
  // from under our feet
  if (index_file < 0) return LOG_INFO_INVALID;
  int error = 0;
  char* fname = linfo->log_file_name;
  IO_CACHE io_cache;
  uint length;
  if (need_lock)
    pthread_mutex_lock(&LOCK_index);
  if (init_io_cache(&io_cache, index_file, IO_SIZE, 
		    READ_CACHE, (my_off_t) linfo->index_file_offset, 0,
		    MYF(MY_WME)))
  {
    error = LOG_INFO_SEEK;
    goto err;
  }
  if (!(length=my_b_gets(&io_cache, fname, FN_REFLEN)))
  {
    error = !io_cache.error ? LOG_INFO_EOF : LOG_INFO_IO;
    goto err;
  }
  fname[length-1]=0;				// kill /n
  linfo->index_file_offset = my_b_tell(&io_cache);
  error = 0;

err:
  if (need_lock)
    pthread_mutex_unlock(&LOCK_index);
  end_io_cache(&io_cache);
  return error;
}

int MYSQL_LOG::reset_logs(THD* thd)
{
  LOG_INFO linfo;
  int error=0;
  const char* save_name;
  enum_log_type save_log_type;
  pthread_mutex_lock(&LOCK_log);
  if (find_first_log(&linfo,""))
  {
    error=1;
    goto err;
  }
  
  for(;;)
  {
    my_delete(linfo.log_file_name, MYF(MY_WME));
    if (find_next_log(&linfo))
      break;
  }
  save_name=name;
  name=0;
  save_log_type=log_type;
  close(1);
  my_delete(index_file_name, MYF(MY_WME));
  if (thd && !thd->slave_thread)
    need_start_event=1;
  open(save_name,save_log_type,0,io_cache_type,no_auto_events);
  my_free((gptr)save_name,MYF(0));
err:  
  pthread_mutex_unlock(&LOCK_log);
  return error;
}

int MYSQL_LOG::purge_first_log(struct st_relay_log_info* rli)
{
  // pre-conditions
  DBUG_ASSERT(is_open());
  DBUG_ASSERT(index_file >= 0);
  DBUG_ASSERT(rli->slave_running == 1);
  DBUG_ASSERT(!strcmp(rli->linfo.log_file_name,rli->relay_log_name));
  // assume that we have previously read the first log and
  // stored it in rli->relay_log_name
  DBUG_ASSERT(rli->linfo.index_file_offset ==
	      strlen(rli->relay_log_name) + 1);
  
  int tmp_fd;
  

  char* fname, *io_buf;
  int error = 0;
  if (!(fname = (char*)my_malloc(IO_SIZE+FN_REFLEN, MYF(MY_WME))))
    return 1;
  pthread_mutex_lock(&LOCK_index);
  my_seek(index_file,rli->linfo.index_file_offset,
	  MY_SEEK_SET, MYF(MY_WME));
  io_buf = fname + FN_REFLEN;
  strxmov(fname,rli->relay_log_name,".tmp",NullS);
  
  if ((tmp_fd = my_open(fname,O_CREAT|O_BINARY|O_RDWR, MYF(MY_WME))) < 0)
  {
    error = 1;
    goto err;
  }
  for (;;)
  {
    int bytes_read;
    bytes_read = my_read(index_file, io_buf, IO_SIZE, MYF(0));
    if (bytes_read < 0) // error
    {
      error=1;
      goto err;
    }
    if (!bytes_read)
      break; // end of file
    // otherwise, we've read something and need to write it out
    if (my_write(tmp_fd, io_buf, bytes_read, MYF(MY_WME|MY_NABP)))
    {
      error=1;
      goto err;
    }
  }
err:
  if (tmp_fd)
    my_close(tmp_fd, MYF(MY_WME));
  if (error)
    my_delete(fname, MYF(0)); // do not report error if the file is not there
  else
  {
    my_close(index_file, MYF(MY_WME));
    if (my_rename(fname,index_file_name,MYF(MY_WME)) ||
	(index_file=my_open(index_file_name,O_BINARY|O_RDWR|O_APPEND,
			    MYF(MY_WME)))<0 ||
	my_delete(rli->relay_log_name, MYF(MY_WME)))
      error=1;
    if ((error=find_first_log(&rli->linfo,"",0/*no mutex*/)))
    {
      char buff[22];
      sql_print_error("next log error=%d,offset=%s,log=%s",error,
		      llstr(rli->linfo.index_file_offset,buff),
		      rli->linfo.log_file_name);
      goto err2;
    }
    rli->relay_log_pos = 4;
    strnmov(rli->relay_log_name,rli->linfo.log_file_name,
	    sizeof(rli->relay_log_name));
  }
  // no need to free io_buf because we allocated both fname and io_buf in
  // one malloc()
err2:
  pthread_mutex_unlock(&LOCK_index);
  my_free(fname, MYF(MY_WME));
  return error;
}

int MYSQL_LOG::purge_logs(THD* thd, const char* to_log)
{
  if (index_file < 0) return LOG_INFO_INVALID;
  if (no_rotate) return LOG_INFO_PURGE_NO_ROTATE;
  int error;
  char fname[FN_REFLEN];
  char *p;
  uint fname_len, i;
  bool logs_to_purge_inited = 0, logs_to_keep_inited = 0, found_log = 0;
  DYNAMIC_ARRAY logs_to_purge, logs_to_keep;
  my_off_t purge_offset ;
  LINT_INIT(purge_offset);
  IO_CACHE io_cache;
  
  pthread_mutex_lock(&LOCK_index);
  
  if (init_io_cache(&io_cache,index_file, IO_SIZE*2, READ_CACHE, (my_off_t) 0,
		    0, MYF(MY_WME)))
  {
    error = LOG_INFO_MEM;
    goto err;
  }
  if (init_dynamic_array(&logs_to_purge, sizeof(char*), 1024, 1024))
  {
    error = LOG_INFO_MEM;
    goto err;
  }
  logs_to_purge_inited = 1;
  
  if (init_dynamic_array(&logs_to_keep, sizeof(char*), 1024, 1024))
  {
    error = LOG_INFO_MEM;
    goto err;
  }
  logs_to_keep_inited = 1;
  
  for (;;)
  {
    my_off_t init_purge_offset= my_b_tell(&io_cache);
    if (!(fname_len=my_b_gets(&io_cache, fname, FN_REFLEN)))
    {
      if(!io_cache.error)
	break;
      error = LOG_INFO_IO;
      goto err;
    }

    fname[--fname_len]=0;			// kill \n
    if (!memcmp(fname, to_log, fname_len + 1 ))
    {
      found_log = 1;
      purge_offset = init_purge_offset;
    }
      
    // if one of the logs before the target is in use
    if (!found_log && log_in_use(fname))
    {
      error = LOG_INFO_IN_USE;
      goto err;
    }
      
    if (!(p = sql_memdup(fname, fname_len+1)) ||
	insert_dynamic(found_log ? &logs_to_keep : &logs_to_purge,
		       (gptr) &p))
    {
      error = LOG_INFO_MEM;
      goto err;
    }
  }
  
  end_io_cache(&io_cache);
  if (!found_log)
  {
    error = LOG_INFO_EOF;
    goto err;
  }
  
  for (i = 0; i < logs_to_purge.elements; i++)
  {
    char* l;
    get_dynamic(&logs_to_purge, (gptr)&l, i);
    if (my_delete(l, MYF(MY_WME)))
      sql_print_error("Error deleting %s during purge", l);
  }
  
  // if we get killed -9 here, the sysadmin would have to do a small
  // vi job on the log index file after restart - otherwise, this should
  // be safe
#ifdef HAVE_FTRUNCATE
  if (ftruncate(index_file,0))
  {
    sql_print_error("Could not truncate the binlog index file \
during log purge for write");
    error = LOG_INFO_FATAL;
    goto err;
  }
  my_seek(index_file, 0, MY_SEEK_CUR,MYF(MY_WME));
#else
  my_close(index_file, MYF(MY_WME));
  my_delete(index_file_name, MYF(MY_WME));
  if ((index_file = my_open(index_file_name,
			    O_CREAT | O_BINARY | O_RDWR | O_APPEND,
			    MYF(MY_WME)))<0)
  {
    sql_print_error("Could not re-open the binlog index file \
during log purge for write");
    error = LOG_INFO_FATAL;
    goto err;
  }
#endif
  
  for (i = 0; i < logs_to_keep.elements; i++)
  {
    char* l;
    get_dynamic(&logs_to_keep, (gptr)&l, i);
    if (my_write(index_file, (byte*) l, strlen(l), MYF(MY_WME|MY_NABP)) ||
	my_write(index_file, (byte*) "\n", 1, MYF(MY_WME|MY_NABP)))
    {
      error = LOG_INFO_FATAL;
      goto err;
    }
  }

  // now update offsets
  adjust_linfo_offsets(purge_offset);
  error = 0;

err:
  pthread_mutex_unlock(&LOCK_index);
  if (logs_to_purge_inited)
    delete_dynamic(&logs_to_purge);
  if (logs_to_keep_inited)
    delete_dynamic(&logs_to_keep);
  end_io_cache(&io_cache);
  return error;
}

// we assume that buf has at least FN_REFLEN bytes alloced
void MYSQL_LOG::make_log_name(char* buf, const char* log_ident)
{
  buf[0] = 0;					// In case of error
  if (inited)
  {
    int dir_len = dirname_length(log_file_name); 
    int ident_len = (uint) strlen(log_ident);
    if (dir_len + ident_len + 1 > FN_REFLEN)
      return; // protection agains malicious buffer overflow
      
    memcpy(buf, log_file_name, dir_len);
    // copy filename + end null
    memcpy(buf + dir_len, log_ident, ident_len + 1);
  }
}

bool MYSQL_LOG::is_active(const char* log_file_name)
{
  return inited && !strcmp(log_file_name, this->log_file_name);
}

void MYSQL_LOG::new_file(bool inside_mutex)
{
  if (is_open())
  {
    char new_name[FN_REFLEN], *old_name=name;
    if (!inside_mutex)
      VOID(pthread_mutex_lock(&LOCK_log));

    if (!no_rotate)
    {
      /*
	only rotate open logs that are marked non-rotatable
	(binlog with constant name are non-rotatable)
      */
      if (generate_new_name(new_name, name))
      {
	if (!inside_mutex)
	  VOID(pthread_mutex_unlock(&LOCK_log));
	return;					// Something went wrong
      }
      if (log_type == LOG_BIN)
      {
	if (!no_auto_events)
	{
	  /*
	    We log the whole file name for log file as the user may decide
	    to change base names at some point.
	  */
	  THD* thd = current_thd;
	  Rotate_log_event r(thd,new_name+dirname_length(new_name));
	  r.set_log_pos(this);

	  /*
	    This log rotation could have been initiated by a master of
	    the slave running with log-bin we set the flag on rotate
	    event to prevent inifinite log rotation loop
	  */
	  if (thd && thd->slave_thread)
	    r.flags |= LOG_EVENT_FORCED_ROTATE_F;
	  r.write(&log_file);
	}
	// update needs to be signaled even if there is no rotate event
	// log rotation should give the waiting thread a signal to
	// discover EOF and move on to the next log
	signal_update(); 
      }
      else
	strmov(new_name, old_name);		// Reopen old file name
    }
    name=0;
    close();
    open(old_name, log_type, new_name, io_cache_type, no_auto_events);
    my_free(old_name,MYF(0));
    last_time=query_start=0;
    write_error=0;
    if (!inside_mutex)
      VOID(pthread_mutex_unlock(&LOCK_log));
  }
}

bool MYSQL_LOG::append(Log_event* ev)
{
  bool error = 0;
  pthread_mutex_lock(&LOCK_log);
  
  DBUG_ASSERT(log_file.type == SEQ_READ_APPEND);
  // Log_event::write() is smart enough to use my_b_write() or
  // my_b_append() depending on the kind of cache we have
  if (ev->write(&log_file))
  {
    error=1;
    goto err;
  }
  if ((uint)my_b_append_tell(&log_file) > max_binlog_size)
  {
    new_file(1);
  }
  signal_update();
err:  
  pthread_mutex_unlock(&LOCK_log);
  return error;
}

bool MYSQL_LOG::appendv(const char* buf, uint len,...)
{
  bool error = 0;
  va_list(args);
  va_start(args,len);
  
  DBUG_ASSERT(log_file.type == SEQ_READ_APPEND);
  
  pthread_mutex_lock(&LOCK_log);
  do
  {
    if (my_b_append(&log_file,buf,len))
    {
      error = 1;
      break;
    }
    if ((uint)my_b_append_tell(&log_file) > max_binlog_size)
    {
      new_file(1);
    }
  } while ((buf=va_arg(args,const char*)) && (len=va_arg(args,uint)));
  
  if (!error)
    signal_update();
  pthread_mutex_unlock(&LOCK_log);
  return error;
}

bool MYSQL_LOG::write(THD *thd,enum enum_server_command command,
		      const char *format,...)
{
  if (is_open() && (what_to_log & (1L << (uint) command)))
  {
    int error=0;
    VOID(pthread_mutex_lock(&LOCK_log));

    /* Test if someone closed after the is_open test */
    if (log_type != LOG_CLOSED)
    {
      time_t skr;
      ulong id;
      va_list args;
      va_start(args,format);
      char buff[32];

      if (thd)
      {						// Normal thread
	if ((thd->options & OPTION_LOG_OFF) &&
	    (thd->master_access & PROCESS_ACL))
	{
	  VOID(pthread_mutex_unlock(&LOCK_log));
	  return 0;				// No logging
	}
	id=thd->thread_id;
	if (thd->user_time || !(skr=thd->query_start()))
	  skr=time(NULL);			// Connected
      }
      else
      {						// Log from connect handler
	skr=time(NULL);
	id=0;
      }
      if (skr != last_time)
      {
	last_time=skr;
	struct tm tm_tmp;
	struct tm *start;
	localtime_r(&skr,&tm_tmp);
	start=&tm_tmp;
	/* Note that my_b_write() assumes it knows the length for this */
	sprintf(buff,"%02d%02d%02d %2d:%02d:%02d\t",
		start->tm_year % 100,
		start->tm_mon+1,
		start->tm_mday,
		start->tm_hour,
		start->tm_min,
		start->tm_sec);
	if (my_b_write(&log_file, (byte*) buff,16))
	  error=errno;
      }
      else if (my_b_write(&log_file, (byte*) "\t\t",2) < 0)
	error=errno;
      sprintf(buff,"%7ld %-11.11s", id,command_name[(uint) command]);
      if (my_b_write(&log_file, (byte*) buff,strlen(buff)))
	error=errno;
      if (format)
      {
	if (my_b_write(&log_file, (byte*) " ",1) ||
	    my_b_vprintf(&log_file,format,args) == (uint) -1)
	  error=errno;
      }
      if (my_b_write(&log_file, (byte*) "\n",1) ||
	  flush_io_cache(&log_file))
	error=errno;
      if (error && ! write_error)
      {
	write_error=1;
	sql_print_error(ER(ER_ERROR_ON_WRITE),name,error);
      }
      va_end(args);
    }
    VOID(pthread_mutex_unlock(&LOCK_log));
    return error != 0;
  }
  return 0;
}


bool MYSQL_LOG::write(Log_event* event_info)
{
  /* In most cases this is only called if 'is_open()' is true */
  bool error=0;
  bool should_rotate = 0;
  
  if (!inited)					// Can't use mutex if not init
    return 0;
  VOID(pthread_mutex_lock(&LOCK_log));
  if (is_open())
  {
    THD *thd=event_info->thd;
    const char* db = event_info->get_db();
#ifdef USING_TRANSACTIONS    
    IO_CACHE *file = ((event_info->get_cache_stmt() && thd) ?
		      &thd->transaction.trans_log :
		      &log_file);
#else
    IO_CACHE *file = &log_file;
#endif    
    if ((thd && !(thd->options & OPTION_BIN_LOG) &&
	 (thd->master_access & PROCESS_ACL)) ||
	(db && !db_ok(db, binlog_do_db, binlog_ignore_db)))
    {
      VOID(pthread_mutex_unlock(&LOCK_log));
      return 0;
    }
    error=1;
    // no check for auto events flag here - this write method should
    // never be called if auto-events are enabled
    if (thd && thd->last_insert_id_used)
    {
      Intvar_log_event e(thd,(uchar)LAST_INSERT_ID_EVENT,thd->last_insert_id);
      e.set_log_pos(this);
      if (thd->server_id)
        e.server_id = thd->server_id;
      if (e.write(file))
	goto err;
    }
    if (thd && thd->insert_id_used)
    {
      Intvar_log_event e(thd,(uchar)INSERT_ID_EVENT,thd->last_insert_id);
      e.set_log_pos(this);
      if (thd->server_id)
        e.server_id = thd->server_id;
      if (e.write(file))
	goto err;
    }
    if (thd && thd->convert_set)
    {
      char buf[1024] = "SET CHARACTER SET ";
      char* p = strend(buf);
      p = strmov(p, thd->convert_set->name);
      int save_query_length = thd->query_length;
      // just in case somebody wants it later
      thd->query_length = (uint)(p - buf);
      Query_log_event e(thd, buf);
      e.set_log_pos(this);
      if (e.write(file))
	goto err;
      thd->query_length = save_query_length; // clean up
    }
    event_info->set_log_pos(this);
    if (event_info->write(file) ||
	file == &log_file && flush_io_cache(file))
      goto err;
    error=0;
    should_rotate = (file == &log_file &&
		     (uint)my_b_tell(file) >= max_binlog_size); 
err:
    if (error)
    {
      if (my_errno == EFBIG)
	my_error(ER_TRANS_CACHE_FULL, MYF(0));
      else
	my_error(ER_ERROR_ON_WRITE, MYF(0), name, errno);
      write_error=1;
    }
    if (file == &log_file)
      signal_update();
  }
  if (should_rotate)
    new_file(1); // inside mutex
  VOID(pthread_mutex_unlock(&LOCK_log));
  return error;
}

uint MYSQL_LOG::next_file_id()
{
  uint res;
  pthread_mutex_lock(&LOCK_log);
  res = file_id++;
  pthread_mutex_unlock(&LOCK_log);
  return res;
}

/*
  Write a cached log entry to the binary log
  We only come here if there is something in the cache.
  'cache' needs to be reinitialized after this functions returns.
*/

bool MYSQL_LOG::write(IO_CACHE *cache)
{
  VOID(pthread_mutex_lock(&LOCK_log));
  bool error=1;
  
  if (is_open())
  {
    uint length;
    //QQ: this looks like a bug - why READ_CACHE?
    if (reinit_io_cache(cache, READ_CACHE, 0, 0, 0))
    {
      sql_print_error(ER(ER_ERROR_ON_WRITE), cache->file_name, errno);
      goto err;
    }
    length=my_b_bytes_in_cache(cache);
    do
    {
      if (my_b_write(&log_file, cache->read_pos, length))
      {
	if (!write_error)
	  sql_print_error(ER(ER_ERROR_ON_WRITE), name, errno);
	goto err;
      }
      cache->read_pos=cache->read_end;		// Mark buffer used up
    } while ((length=my_b_fill(cache)));
    if (flush_io_cache(&log_file))
    {
      if (!write_error)
	sql_print_error(ER(ER_ERROR_ON_WRITE), name, errno);
      goto err;
    }
    if (cache->error)				// Error on read
    {
      sql_print_error(ER(ER_ERROR_ON_READ), cache->file_name, errno);
      goto err;
    }
  }
  error=0;

err:
  if (error)
    write_error=1;
  else
    signal_update();
    
  VOID(pthread_mutex_unlock(&LOCK_log));
  
  return error;
}


/* Write update log in a format suitable for incremental backup */

bool MYSQL_LOG::write(THD *thd,const char *query, uint query_length,
		      time_t query_start)
{
  bool error=0;
  if (is_open())
  {
    time_t current_time;
    VOID(pthread_mutex_lock(&LOCK_log));
    if (is_open())
    {						// Safety agains reopen
      int tmp_errno=0;
      char buff[80],*end;
      end=buff;
      if (!(thd->options & OPTION_UPDATE_LOG) &&
	  (thd->master_access & PROCESS_ACL))
      {
	VOID(pthread_mutex_unlock(&LOCK_log));
	return 0;
      }
      if ((specialflag & SPECIAL_LONG_LOG_FORMAT) || query_start)
      {
	current_time=time(NULL);
	if (current_time != last_time)
	{
	  last_time=current_time;
	  struct tm tm_tmp;
	  struct tm *start;
	  localtime_r(&current_time,&tm_tmp);
	  start=&tm_tmp;
	  /* Note that my_b_write() assumes it knows the length for this */
	  sprintf(buff,"# Time: %02d%02d%02d %2d:%02d:%02d\n",
		  start->tm_year % 100,
		  start->tm_mon+1,
		  start->tm_mday,
		  start->tm_hour,
		  start->tm_min,
		  start->tm_sec);
	  if (my_b_write(&log_file, (byte*) buff,24))
	    tmp_errno=errno;
	}
	if (my_b_printf(&log_file, "# User@Host: %s[%s] @ %s [%s]\n",
			thd->priv_user,
			thd->user,
			thd->host ? thd->host : "",
			thd->ip ? thd->ip : "") == (uint) -1)
	  tmp_errno=errno;
      }
      if (query_start)
      {
	/* For slow query log */
	if (my_b_printf(&log_file,
			"# Query_time: %lu  Lock_time: %lu  Rows_sent: %lu  Rows_examined: %lu\n",
			(ulong) (current_time - query_start),
			(ulong) (thd->time_after_lock - query_start),
			(ulong) thd->sent_row_count,
			(ulong) thd->examined_row_count) == (uint) -1)
	  tmp_errno=errno;
      }
      if (thd->db && strcmp(thd->db,db))
      {						// Database changed
	if (my_b_printf(&log_file,"use %s;\n",thd->db) == (uint) -1)
	  tmp_errno=errno;
	strmov(db,thd->db);
      }
      if (thd->last_insert_id_used)
      {
	end=strmov(end,",last_insert_id=");
	end=longlong10_to_str((longlong) thd->current_insert_id,end,-10);
      }
      // Save value if we do an insert.
      if (thd->insert_id_used)
      {
	if (specialflag & SPECIAL_LONG_LOG_FORMAT)
	{
	  end=strmov(end,",insert_id=");
	  end=longlong10_to_str((longlong) thd->last_insert_id,end,-10);
	}
      }
      if (thd->query_start_used)
      {
	if (query_start != thd->query_start())
	{
	  query_start=thd->query_start();
	  end=strmov(end,",timestamp=");
	  end=int10_to_str((long) query_start,end,10);
	}
      }
      if (end != buff)
      {
	*end++=';';
	*end++='\n';
	*end=0;
	if (my_b_write(&log_file, (byte*) "SET ",4) ||
	    my_b_write(&log_file, (byte*) buff+1,(uint) (end-buff)-1))
	  tmp_errno=errno;
      }
      if (!query)
      {
	query="#adminstrator command";
	query_length=21;
      }
      if (my_b_write(&log_file, (byte*) query,query_length) ||
	  my_b_write(&log_file, (byte*) ";\n",2) ||
	  flush_io_cache(&log_file))
	tmp_errno=errno;
      if (tmp_errno)
      {
	error=1;
	if (! write_error)
	{
	  write_error=1;
	  sql_print_error(ER(ER_ERROR_ON_WRITE),name,error);
	}
      }
    }
    VOID(pthread_mutex_unlock(&LOCK_log));
  }
  return error;
}

void MYSQL_LOG:: wait_for_update(THD* thd)
{
  const char* old_msg = thd->enter_cond(&update_cond, &LOCK_log,
					"Slave: waiting for binlog update");
  pthread_cond_wait(&update_cond, &LOCK_log);
  // this is not a bug - we unlock the mutex for the caller, and expect him
  // to lock it and then not unlock it upon return. This is a rather odd
  // way of doing things, but this is the cleanest way I could think of to
  // solve the race deadlock caused by THD::awake() first acquiring mysys_var
  // mutex and then the current mutex, while wait_for_update being called with
  // the current mutex already aquired and THD::exit_cond() trying to acquire
  // mysys_var mutex. We do need the mutex to be acquired prior to the
  // invocation of wait_for_update in all cases, so mutex acquisition inside
  // wait_for_update() is not an option
  pthread_mutex_unlock(&LOCK_log);
  thd->exit_cond(old_msg);
}  

void MYSQL_LOG::close(bool exiting)
{					// One can't set log_type here!
  if (is_open())
  {
    if (log_type == LOG_BIN && !no_auto_events)
    {
      Stop_log_event s;
      s.set_log_pos(this);
      s.write(&log_file);
      signal_update();
    }
    end_io_cache(&log_file);
    if (my_close(log_file.file,MYF(0)) < 0 && ! write_error)
    {
      write_error=1;
      sql_print_error(ER(ER_ERROR_ON_WRITE),name,errno);
    }
  }
  if (exiting && index_file >= 0)
  {
    if (my_close(index_file,MYF(0)) < 0 && ! write_error)
    {
      write_error=1;
      sql_print_error(ER(ER_ERROR_ON_WRITE),name,errno);
    }
    index_file=-1;
    log_type=LOG_CLOSED;
  }
  safeFree(name);
}


	/* Check if a string is a valid number */
	/* Output: TRUE -> number */

static bool test_if_number(register const char *str,
			   long *res, bool allow_wildcards)
{
  reg2 int flag;
  const char *start;
  DBUG_ENTER("test_if_number");

  flag=0; start=str;
  while (*str++ == ' ') ;
  if (*--str == '-' || *str == '+')
    str++;
  while (isdigit(*str) || (allow_wildcards &&
			   (*str == wild_many || *str == wild_one)))
  {
    flag=1;
    str++;
  }
  if (*str == '.')
  {
    for (str++ ;
	 isdigit(*str) ||
	   (allow_wildcards && (*str == wild_many || *str == wild_one)) ;
	 str++, flag=1) ;
  }
  if (*str != 0 || flag == 0)
    DBUG_RETURN(0);
  if (res)
    *res=atol(start);
  DBUG_RETURN(1);			/* Number ok */
} /* test_if_number */


void sql_print_error(const char *format,...)
{
  va_list args;
  time_t skr;
  struct tm tm_tmp;
  struct tm *start;
  va_start(args,format);
  DBUG_ENTER("sql_print_error");

  VOID(pthread_mutex_lock(&LOCK_error_log));
#ifndef DBUG_OFF
  {
    char buff[1024];
    my_vsnprintf(buff,sizeof(buff)-1,format,args);
    DBUG_PRINT("error",("%s",buff));
  }
#endif
  skr=time(NULL);
  localtime_r(&skr,&tm_tmp);
  start=&tm_tmp;
  fprintf(stderr,"%02d%02d%02d %2d:%02d:%02d  ",
	  start->tm_year % 100,
	  start->tm_mon+1,
	  start->tm_mday,
	  start->tm_hour,
	  start->tm_min,
	  start->tm_sec);
  (void) vfprintf(stderr,format,args);
  (void) fputc('\n',stderr);
  fflush(stderr);
  va_end(args);

  VOID(pthread_mutex_unlock(&LOCK_error_log));
  DBUG_VOID_RETURN;
}



void sql_perror(const char *message)
{
#ifdef HAVE_STRERROR
  sql_print_error("%s: %s",message, strerror(errno));
#else
  perror(message);
#endif
}
