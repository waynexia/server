/* Copyright (C) 2007 MySQL AB

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
#include "ma_recovery.h"
#include <my_getopt.h>

#define PCACHE_SIZE (1024*1024*10)
#define LOG_FLAGS 0
#define LOG_FILE_SIZE (1024L*1024L)

static const char *load_default_groups[]= { "maria_read_log",0 };
static void get_options(int *argc,char * * *argv);
#ifndef DBUG_OFF
#if defined(__WIN__)
const char *default_dbug_option= "d:t:i:O,\\maria_read_log.trace";
#else
const char *default_dbug_option= "d:t:i:o,/tmp/maria_read_log.trace";
#endif
#endif /* DBUG_OFF */
static my_bool opt_only_display, opt_display_and_apply;

int main(int argc, char **argv)
{
  LSN lsn;
  char **default_argv;
  MY_INIT(argv[0]);

  load_defaults("my", load_default_groups, &argc, &argv);
  default_argv= argv;
  get_options(&argc, &argv);

  maria_data_root= ".";
  maria_in_recovery= TRUE;

  if (maria_init())
  {
    fprintf(stderr, "Can't init Maria engine (%d)\n", errno);
    goto err;
  }
  /* we don't want to create a control file, it MUST exist */
  if (ma_control_file_create_or_open())
  {
    fprintf(stderr, "Can't open control file (%d)\n", errno);
    goto err;
  }
  if (last_logno == FILENO_IMPOSSIBLE)
  {
    fprintf(stderr, "Can't find any log\n");
    goto err;
  }
  /* same page cache for log and data; assumes same page size... */
  DBUG_ASSERT(maria_block_size == TRANSLOG_PAGE_SIZE);
  if (init_pagecache(maria_pagecache, PCACHE_SIZE, 0, 0,
                     TRANSLOG_PAGE_SIZE) == 0)
  {
    fprintf(stderr, "Got error in init_pagecache() (errno: %d)\n", errno);
    goto err;
  }
  /*
    If log handler does not find the "last_logno" log it will return error,
    which is good.
    But if it finds a log and this log was crashed, it will create a new log,
    which is useless. TODO: start log handler in read-only mode.
  */
  if (translog_init(".", LOG_FILE_SIZE, 50112, 0, maria_pagecache,
                    TRANSLOG_DEFAULT_FLAGS))
  {
    fprintf(stderr, "Can't init loghandler (%d)\n", errno);
    goto err;
  }

  if (opt_only_display)
    printf("You are using --only-display, NOTHING will be written to disk\n");

  /* LSN could be also --start-from-lsn=# */
  lsn= translog_first_theoretical_lsn();
  /*
    @todo process LSN_IMPOSSIBLE and LSN_ERROR values of
    translog_first_theoretical_lsn()
  */

  fprintf(stdout, "TRACE of the last maria_read_log\n");
  if (maria_apply_log(lsn, opt_display_and_apply, stdout,
                      opt_display_and_apply))
    goto err;
  fprintf(stdout, "%s: SUCCESS\n", my_progname);

  goto end;
err:
  /* don't touch anything more, in case we hit a bug */
  fprintf(stderr, "%s: FAILED\n", my_progname);
  exit(1);
end:
  maria_end();
  free_defaults(default_argv);
  my_end(0);
  exit(0);
  return 0;				/* No compiler warning */
}


static struct my_option my_long_options[] =
{
  {"help", '?', "Display this help and exit.",
   0, 0, 0, GET_NO_ARG, NO_ARG, 0, 0, 0, 0, 0, 0},
  {"only-display", 'o', "display brief info about records's header",
   (uchar **) &opt_only_display, (uchar **) &opt_only_display, 0, GET_BOOL,
   NO_ARG,0, 0, 0, 0, 0, 0},
  {"display-and-apply", 'a',
   "like --only-display but displays more info and modifies tables",
   (uchar **) &opt_display_and_apply, (uchar **) &opt_display_and_apply, 0,
   GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0},
#ifndef DBUG_OFF
  {"debug", '#', "Output debug log. Often this is 'd:t:o,filename'.",
   0, 0, 0, GET_STR, OPT_ARG, 0, 0, 0, 0, 0, 0},
#endif
  { 0, 0, 0, 0, 0, 0, GET_NO_ARG, NO_ARG, 0, 0, 0, 0, 0, 0}
};

#include <help_start.h>

static void print_version(void)
{
  VOID(printf("%s Ver 1.0 for %s on %s\n",
              my_progname, SYSTEM_TYPE, MACHINE_TYPE));
  NETWARE_SET_SCREEN_MODE(1);
}


static void usage(void)
{
  print_version();
  puts("Copyright (C) 2007 MySQL AB");
  puts("This software comes with ABSOLUTELY NO WARRANTY. This is free software,");
  puts("and you are welcome to modify and redistribute it under the GPL license\n");

  puts("Display and apply log records from a MARIA transaction log");
  puts("found in the current directory (for now)");
  VOID(printf("\nUsage: %s OPTIONS\n", my_progname));
  puts("You need to use one of -o or -a");
  my_print_help(my_long_options);
  print_defaults("my", load_default_groups);
  my_print_variables(my_long_options);
}

#include <help_end.h>

static my_bool
get_one_option(int optid __attribute__((unused)),
               const struct my_option *opt __attribute__((unused)),
               char *argument __attribute__((unused)))
{
  switch (optid) {
  case '?':
    usage();
    exit(0);
#ifndef DBUG_OFF
  case '#':
    DBUG_SET_INITIAL(argument ? argument : default_dbug_option);
    break;
#endif
  }
  return 0;
}

static void get_options(int *argc,char ***argv)
{
  int ho_error;

  my_progname= argv[0][0];

  if ((ho_error=handle_options(argc, argv, my_long_options, get_one_option)))
    exit(ho_error);

  if ((opt_only_display + opt_display_and_apply) != 1)
  {
    usage();
    exit(1);
  }
}
