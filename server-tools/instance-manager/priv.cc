/* Copyright (C) 2003 MySQL AB & MySQL Finland AB & TCX DataKonsult AB

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

#include "priv.h"

#include <my_global.h>
#include <mysql_com.h>
#include <my_sys.h>

#include "log.h"

/* the pid of the manager process (of the signal thread on the LinuxThreads) */
pid_t manager_pid;

/*
  This flag is set if mysqlmanager has detected that it is running on the
  system using LinuxThreads
*/
bool linuxthreads;

/*
  The following string must be less then 80 characters, as
  mysql_connection.cc relies on it
*/
const LEX_STRING mysqlmanager_version= { C_STRING_WITH_LEN("1.0-beta") };

const unsigned char protocol_version= PROTOCOL_VERSION;

unsigned long net_buffer_length= 16384;

unsigned long max_allowed_packet= 16384;

unsigned long net_read_timeout= NET_WAIT_TIMEOUT;    // same as in mysqld

unsigned long net_write_timeout= 60;            // same as in mysqld

unsigned long net_retry_count= 10;              // same as in mysqld

/* needed by net_serv.cc */
unsigned int test_flags= 0;
unsigned long bytes_sent = 0L, bytes_received = 0L;
unsigned long mysqld_net_retry_count = 10L;
unsigned long open_files_limit;



int create_pid_file(const char *pid_file_name, int pid)
{
  FILE *pid_file;

  if (!(pid_file= my_fopen(pid_file_name, O_WRONLY | O_CREAT | O_BINARY,
                           MYF(0))))
  {
    log_error("Error: can not create pid file '%s': %s (errno: %d)",
              (const char *) pid_file_name,
              (const char *) strerror(errno),
              (int) errno);
    return 1;
  }

  if (fprintf(pid_file, "%d\n", (int) pid) <= 0)
  {
    log_error("Error: can not write to pid file '%s': %s (errno: %d)",
              (const char *) pid_file_name,
              (const char *) strerror(errno),
              (int) errno);
    return 1;
  }

  my_fclose(pid_file, MYF(0));

  return 0;
}
