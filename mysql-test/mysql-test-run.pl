#!/usr/bin/perl
# -*- cperl -*-

# This is a transformation of the "mysql-test-run" Bourne shell script
# to Perl. This is just an intermediate step, the goal is to rewrite
# the Perl script to C. The complexity of the mysql-test-run script
# makes it a bit hard to write and debug it as a C program directly,
# so this is considered a prototype.
#
# Because of this the Perl coding style may in some cases look a bit
# funny. The rules used are
#
#   - The coding style is as close as possible to the C/C++ MySQL
#     coding standard.
#
#   - Where NULL is to be returned, the undefined value is used.
#
#   - Regexp comparisons are simple and can be translated to strcmp
#     and other string functions. To ease this transformation matching
#     is done in the lib "lib/mtr_match.pl", i.e. regular expressions
#     should be avoided in the main program.
#
#   - The "unless" construct is not to be used. It is the same as "if !".
#
#   - opendir/readdir/closedir is used instead of glob()/<*>.
#
#   - All lists of arguments to send to commands are Perl lists/arrays,
#     not strings we append args to. Within reason, most string
#     concatenation for arguments should be avoided.
#
#   - sprintf() is to be used, within reason, for all string creation.
#     This mtr_add_arg() function is also based on sprintf(), i.e. you
#     use a format string and put the variable argument in the argument
#     list.
#
#   - Functions defined in the main program are not to be prefixed,
#     functions in "library files" are to be prefixed with "mtr_" (for
#     Mysql-Test-Run). There are some exceptions, code that fits best in
#     the main program, but are put into separate files to avoid
#     clutter, may be without prefix.
#
#   - All stat/opendir/-f/ is to be kept in collect_test_cases(). It
#     will create a struct that the rest of the program can use to get
#     the information. This separates the "find information" from the
#     "do the work" and makes the program more easy to maintain.
#
#   - At the moment, there are tons of "global" variables that control
#     this script, even accessed from the files in "lib/*.pl". This
#     will change over time, for now global variables are used instead
#     of using %opt, %path and %exe hashes, because I want more
#     compile time checking, that hashes would not give me. Once this
#     script is debugged, hashes will be used and passed as parameters
#     to functions, to more closely mimic how it would be coded in C
#     using structs.
#
#   - The rule when it comes to the logic of this program is
#
#       command_line_setup() - is to handle the logic between flags
#       collect_test_cases() - is to do its best to select what tests
#                              to run, dig out options, if needs restart etc.
#       run_testcase()       - is to run a single testcase, and follow the
#                              logic set in both above. No, or rare file
#                              system operations. If a test seems complex,
#                              it should probably not be here.
#
# A nice way to trace the execution of this script while debugging
# is to use the Devel::Trace package found at
# "http://www.plover.com/~mjd/perl/Trace/" and run this script like
# "perl -d:Trace mysql-test-run.pl"

$Devel::Trace::TRACE= 0;       # Don't trace boring init stuff

#require 5.6.1;
use File::Path;
use File::Basename;
use Cwd;
use Getopt::Long;
use Sys::Hostname;
#use Carp;
use IO::Socket;
use IO::Socket::INET;
use Data::Dumper;
use strict;
#use diagnostics;

require "lib/mtr_process.pl";
require "lib/mtr_io.pl";
require "lib/mtr_gcov.pl";
require "lib/mtr_gprof.pl";
require "lib/mtr_report.pl";
require "lib/mtr_match.pl";
require "lib/mtr_misc.pl";

$Devel::Trace::TRACE= 1;

my @skip_if_embedded_server=
  (
   "alter_table",
   "bdb-deadlock",
   "connect",
   "flush_block_commit",
   "grant2",
   "grant_cache",
   "grant",
   "init_connect",
   "innodb-deadlock",
   "innodb-lock",
   "mix_innodb_myisam_binlog",
   "mysqlbinlog2",
   "mysqlbinlog",
   "mysqldump",
   "mysql_protocols",
   "ps_1general",
   "rename",
   "show_check",
   "system_mysql_db_fix",
   "user_var",
   "variables",
 );

# Used by gcov
our @mysqld_src_dirs=
  (
   "strings",
   "mysys",
   "include",
   "extra",
   "regex",
   "isam",
   "merge",
   "myisam",
   "myisammrg",
   "heap",
   "sql",
  );

##############################################################################
#
#  Default settings
#
##############################################################################

# We are to use handle_options() in "mysys/my_getopt.c" for the C version
#
# In the C version we want to use structs and, in some cases, arrays of
# structs. We let each struct be a separate hash.

# Misc global variables

our $glob_win32=                  0;
our $glob_mysql_test_dir=         undef;
our $glob_mysql_bench_dir=        undef;
our $glob_hostname=               undef;
our $glob_scriptname=             undef;
our $glob_use_running_server=     0;
our $glob_use_running_ndbcluster= 0;
our $glob_user=                   'test';
our $glob_use_embedded_server=    0;

our $glob_basedir;
our $glob_do_test;

# The total result

our $path_charsetsdir;
our $path_client_bindir;
our $path_language;
our $path_tests_bindir;
our $path_timefile;
our $path_manager_log;           # Used by mysqldadmin
our $path_slave_load_tmpdir;     # What is this?!
our $path_my_basedir;
our $opt_tmpdir;                 # A path but set directly on cmd line

our $opt_usage;
our $opt_suite;

our $opt_netware;

our $opt_script_debug= 0;  # Script debugging, enable with --script-debug

# Options FIXME not all....

our $exe_master_mysqld;
our $exe_mysql;
our $exe_mysqladmin;
our $exe_mysqlbinlog;
our $exe_mysqld;
our $exe_mysqldump;              # Called from test case
our $exe_mysqltest;
our $exe_slave_mysqld;

our $opt_bench= 0;
our $opt_small_bench= 0;
our $opt_big_test= 0;            # Send --big-test to mysqltest

our $opt_extra_mysqld_opt;       # FIXME not handled

our $opt_compress;
our $opt_current_test;
our $opt_ddd;
our $opt_debug;
our $opt_do_test;
our $opt_embedded_server;
our $opt_extern;
our $opt_fast;
our $opt_force;

our $opt_gcov;
our $opt_gcov_err;
our $opt_gcov_msg;

our $opt_gdb;
our $opt_client_gdb;
our $opt_manual_gdb;

our $opt_gprof;
our $opt_gprof_dir;
our $opt_gprof_master;
our $opt_gprof_slave;

our $opt_local;
our $opt_local_master;

our $master;                    # Will be struct in C
our $slave;

our $opt_ndbcluster_port;
our $opt_ndbconnectstring;

our $opt_no_manager;            # Does nothing now, we never use manager

our $opt_old_master;

our $opt_record;

our $opt_result_ext;

our $opt_skip;
our $opt_skip_rpl;
our $opt_skip_test;

our $opt_sleep;

our $opt_ps_protocol;

# FIXME all of the sleep time handling needs cleanup
our $opt_sleep_time_after_restart=        1;
our $opt_sleep_time_for_delete=          10;
our $opt_sleep_time_for_first_master=   400; # enough time create innodb tables
our $opt_sleep_time_for_second_master=  400;
our $opt_sleep_time_for_first_slave=    400;
our $opt_sleep_time_for_second_slave=    30;

our $opt_socket;

our $opt_source_dist;

our $opt_start_and_exit;
our $opt_start_from;

our $opt_strace_client;

our $opt_timer;


our $opt_user_test;

our $opt_valgrind;
our $opt_valgrind_all;
our $opt_valgrind_options;

our $opt_verbose;

our $opt_wait_for_master;
our $opt_wait_for_slave;
our $opt_wait_timeout=  10;

our $opt_warnings;

our $opt_with_ndbcluster;
our $opt_with_openssl;


######################################################################
#
#  Function declarations
#
######################################################################

sub main ();
sub initial_setup ();
sub command_line_setup ();
sub executable_setup ();
sub kill_and_cleanup ();
sub collect_test_cases ($);
sub sleep_until_file_created ($$);
sub ndbcluster_start ();
sub ndbcluster_stop ();
sub run_benchmarks ($);
sub run_tests ();
sub mysql_install_db ();
sub install_db ($$);
sub run_testcase ($);
sub do_before_start_master ($$);
sub do_before_start_slave ($$);
sub mysqld_start ($$$$);
sub mysqld_arguments ($$$$$);
sub stop_masters_slaves ();
sub stop_masters ();
sub stop_slaves ();
sub run_mysqltest ($$);
sub usage ($);

######################################################################
#
#  Main program
#
######################################################################

main();

sub main () {

  initial_setup();
  command_line_setup();
  executable_setup();
  signal_setup();

  if ( $opt_gcov )
  {
    gcov_prepare();
  }

  if ( $opt_gprof )
  {
    gprof_prepare();
  }

  if ( ! $glob_use_running_server )
  {
    kill_and_cleanup();
    mysql_install_db();

    if ( $opt_with_ndbcluster and ! $glob_use_running_ndbcluster )
    {
      ndbcluster_start();     # We start the cluster storage engine
    }

#    mysql_loadstd();  FIXME copying from "std_data" .frm and
#                      .MGR but there are none?!
  }

  if ( $opt_start_and_exit )
  {
    mtr_report("Servers started, exiting");
  }
  else
  {
    if ( $opt_bench )
    {
      run_benchmarks(shift);      # Shift what? Extra arguments?!
    }
    else
    {
      run_tests();
    }
  }

  exit(0);
}

##############################################################################
#
#  Initial setup independent on command line arguments
#
##############################################################################

sub initial_setup () {

  select(STDOUT);
  $| = 1;                       # Make unbuffered

  $glob_scriptname=  basename($0);

  $glob_win32= ($^O eq "MSWin32");

  # We require that we are in the "mysql-test" directory
  # to run mysql-test-run

  if (! -f $glob_scriptname)
  {
    mtr_error("Can't find the location for the mysql-test-run script\n" .
              "Go to to the mysql-test directory and execute the script " .
              "as follows:\n./$glob_scriptname");
  }

  if ( -d "../sql" )
  {
    $opt_source_dist=  1;
  }

  $glob_hostname=  mtr_full_hostname();

  # 'basedir' is always parent of "mysql-test" directory
  $glob_mysql_test_dir=  cwd();
  $glob_basedir=         dirname($glob_mysql_test_dir);
  $glob_mysql_bench_dir= "$glob_basedir/mysql-bench"; # FIXME make configurable

  $path_timefile=  "$glob_mysql_test_dir/var/log/mysqltest-time";

  # needs to be same length to test logging (FIXME what???)
  $path_slave_load_tmpdir=  "../../var/tmp";

  $path_my_basedir=
    $opt_source_dist ? $glob_mysql_test_dir : $glob_basedir;
}



##############################################################################
#
#  Default settings
#
##############################################################################

sub command_line_setup () {

  # These are defaults for things that are set on the command line

  $opt_suite=        "main";    # Special default suite
  $opt_tmpdir=       "$glob_mysql_test_dir/var/tmp";
  # FIXME maybe unneded?
  $path_manager_log= "$glob_mysql_test_dir/var/log/manager.log";
  $opt_current_test= "$glob_mysql_test_dir/var/log/current_test";

  my $opt_master_myport=       9306;
  my $opt_slave_myport=        9308;
  $opt_ndbcluster_port=        9350;
  $opt_sleep_time_for_delete=  10;

  my $opt_user;

  # Read the command line
  # Note: Keep list, and the order, in sync with usage at end of this file

  GetOptions(
             # Control what engine/variation to run
             'embedded-server'          => \$opt_embedded_server,
             'ps-protocol'              => \$opt_ps_protocol,
             'bench'                    => \$opt_bench,
             'small-bench'              => \$opt_small_bench,
             'no-manager'               => \$opt_no_manager,

             # Control what test suites or cases to run
             'force'                    => \$opt_force,
             'with-ndbcluster'          => \$opt_with_ndbcluster,
             'do-test=s'                => \$opt_do_test,
             'suite=s'                  => \$opt_suite,
             'skip-rpl'                 => \$opt_skip_rpl,
             'skip-test=s'              => \$opt_skip_test,

             # Specify ports
             'master_port=i'            => \$opt_master_myport,
             'slave_port=i'             => \$opt_slave_myport,
             'ndbcluster_port=i'        => \$opt_ndbcluster_port,

             # Test case authoring
             'record'                   => \$opt_record,

             # ???
             'mysqld=s'                 => \$opt_extra_mysqld_opt,

             # Run test on running server
             'extern'                   => \$opt_extern,
             'ndbconnectstring=s'       => \$opt_ndbconnectstring,

             # Debugging
             'gdb'                      => \$opt_gdb,
             'manual-gdb'               => \$opt_manual_gdb,
             'client-gdb'               => \$opt_client_gdb,
             'ddd'                      => \$opt_ddd,
             'strace-client'            => \$opt_strace_client,
             'master-binary=s'          => \$exe_master_mysqld,
             'slave-binary=s'           => \$exe_slave_mysqld,

             # Coverage, profiling etc
             'gcov'                     => \$opt_gcov,
             'gprof'                    => \$opt_gprof,
             'valgrind'                 => \$opt_valgrind,
             'valgrind-all'             => \$opt_valgrind_all,
             'valgrind-options=s'       => \$opt_valgrind_options,

             # Misc
             'big-test'                 => \$opt_big_test,
             'compress'                 => \$opt_compress,
             'debug'                    => \$opt_debug,
             'fast'                     => \$opt_fast,
             'local'                    => \$opt_local,
             'local-master'             => \$opt_local_master,
             'netware'                  => \$opt_netware,
             'old-master'               => \$opt_old_master,
             'script-debug'             => \$opt_script_debug,
             'sleep=i'                  => \$opt_sleep,
             'socket=s'                 => \$opt_socket,
             'start-and-exit'           => \$opt_start_and_exit,
             'start-from=s'             => \$opt_start_from,
             'timer'                    => \$opt_timer,
             'tmpdir=s'                 => \$opt_tmpdir,
             'user-test=s'              => \$opt_user_test,
             'user=s'                   => \$opt_user,
             'verbose'                  => \$opt_verbose,
             'wait-timeout=i'           => \$opt_wait_timeout,
             'warnings|log-warnings'    => \$opt_warnings,
             'with-openssl'             => \$opt_with_openssl,

             'help|h'                   => \$opt_usage,
            ) or usage("Can't read options");

  if ( $opt_usage )
  {
    usage("");
  }

  # Put this into a hash, will be a C struct

  $master->[0]->{'path_myddir'}=  "$glob_mysql_test_dir/var/master-data";
  $master->[0]->{'path_myerr'}=   "$glob_mysql_test_dir/var/log/master.err";
  $master->[0]->{'path_mylog'}=   "$glob_mysql_test_dir/var/log/master.log";
  $master->[0]->{'path_mypid'}=   "$glob_mysql_test_dir/var/run/master.pid";
  $master->[0]->{'path_mysock'}=  "$opt_tmpdir/master.sock";
  $master->[0]->{'path_myport'}=   $opt_master_myport;

  $master->[1]->{'path_myddir'}=  "$glob_mysql_test_dir/var/master1-data";
  $master->[1]->{'path_myerr'}=   "$glob_mysql_test_dir/var/log/master1.err";
  $master->[1]->{'path_mylog'}=   "$glob_mysql_test_dir/var/log/master1.log";
  $master->[1]->{'path_mypid'}=   "$glob_mysql_test_dir/var/run/master1.pid";
  $master->[1]->{'path_mysock'}=  "$opt_tmpdir/master1.sock";
  $master->[1]->{'path_myport'}=   $opt_master_myport + 1;

  $slave->[0]->{'path_myddir'}=   "$glob_mysql_test_dir/var/slave-data";
  $slave->[0]->{'path_myerr'}=    "$glob_mysql_test_dir/var/log/slave.err";
  $slave->[0]->{'path_mylog'}=    "$glob_mysql_test_dir/var/log/slave.log";
  $slave->[0]->{'path_mypid'}=    "$glob_mysql_test_dir/var/run/slave.pid";
  $slave->[0]->{'path_mysock'}=   "$opt_tmpdir/slave.sock";
  $slave->[0]->{'path_myport'}=    $opt_slave_myport;

  $slave->[1]->{'path_myddir'}=   "$glob_mysql_test_dir/var/slave1-data";
  $slave->[1]->{'path_myerr'}=    "$glob_mysql_test_dir/var/log/slave1.err";
  $slave->[1]->{'path_mylog'}=    "$glob_mysql_test_dir/var/log/slave1.log";
  $slave->[1]->{'path_mypid'}=    "$glob_mysql_test_dir/var/run/slave1.pid";
  $slave->[1]->{'path_mysock'}=   "$opt_tmpdir/slave1.sock";
  $slave->[1]->{'path_myport'}=    $opt_slave_myport + 1;

  $slave->[2]->{'path_myddir'}=   "$glob_mysql_test_dir/var/slave2-data";
  $slave->[2]->{'path_myerr'}=    "$glob_mysql_test_dir/var/log/slave2.err";
  $slave->[2]->{'path_mylog'}=    "$glob_mysql_test_dir/var/log/slave2.log";
  $slave->[2]->{'path_mypid'}=    "$glob_mysql_test_dir/var/run/slave2.pid";
  $slave->[2]->{'path_mysock'}=   "$opt_tmpdir/slave2.sock";
  $slave->[2]->{'path_myport'}=    $opt_slave_myport + 2;

  # Do sanity checks of command line arguments

  if ( $opt_extern and $opt_local )
  {
    mtr_error("Can't use --extern and --local at the same time");
  }

  if ( ! $opt_socket )
  {     # FIXME set default before reading options?
#    $opt_socket=  '@MYSQL_UNIX_ADDR@';
    $opt_socket=  "/tmp/mysql.sock"; # FIXME
  }

  if ( $opt_extern )
  {
    $glob_use_running_server=  1;
    $opt_skip_rpl= 1;                   # We don't run rpl test cases
    $master->[0]->{'path_mysock'}=  $opt_socket;
  }

  # --------------------------------------------------------------------------
  # Set LD_LIBRARY_PATH if we are using shared libraries
  # --------------------------------------------------------------------------
  $ENV{'LD_LIBRARY_PATH'}=
    "$glob_basedir/lib:$glob_basedir/libmysql/.libs" .
      ($ENV{'LD_LIBRARY_PATH'} ? ":$ENV{'LD_LIBRARY_PATH'}" : "");
  $ENV{'DYLD_LIBRARY_PATH'}=
    "$glob_basedir/lib:$glob_basedir/libmysql/.libs" .
      ($ENV{'DYLD_LIBRARY_PATH'} ? ":$ENV{'DYLD_LIBRARY_PATH'}" : "");

  # --------------------------------------------------------------------------
  # Look at the command line options and set script flags
  # --------------------------------------------------------------------------

  if ( $opt_record and ! @ARGV)
  {
    mtr_error("Will not run in record mode without a specific test case");
  }

  if ( $opt_embedded_server )
  {
    $glob_use_embedded_server= 1;
    $opt_skip_rpl= 1;              # We never run replication with embedded

    if ( $opt_extern )
    {
      mtr_error("Can't use --extern with --embedded-server");
    }
    $opt_result_ext=  ".es";
  }

  # FIXME don't understand what this is
#  if ( $opt_local_master )
#  {
#    $opt_master_myport=  3306;
#  }

  if ( $opt_small_bench )
  {
    $opt_bench=  1;
  }

  if ( $opt_sleep )
  {
    $opt_sleep_time_after_restart= $opt_sleep;
  }

  if ( $opt_gcov and ! $opt_source_dist )
  {
    mtr_error("Coverage test needs the source - please use source dist");
  }

  if ( $glob_use_embedded_server and ! $opt_source_dist )
  {
    mtr_error("Embedded server needs source tree - please use source dist");
  }

  if ( $opt_gdb )
  {
    $opt_wait_timeout=  300;
    if ( $opt_extern )
    {
      mtr_error("Can't use --extern with --gdb");
    }
  }

  if ( $opt_manual_gdb )
  {
    $opt_gdb=  1;
    if ( $opt_extern )
    {
      mtr_error("Can't use --extern with --manual-gdb");
    }
  }

  if ( $opt_ddd )
  {
    if ( $opt_extern )
    {
      mtr_error("Can't use --extern with --ddd");
    }
  }

  if ( $opt_ndbconnectstring )
  {
    $glob_use_running_ndbcluster= 1;
    $opt_with_ndbcluster= 1;
  }

  # FIXME

  #if ( $opt_valgrind or $opt_valgrind_all )
  #{
    # VALGRIND=`which valgrind` # this will print an error if not found FIXME
    # Give good warning to the user and stop
  #  if ( ! $VALGRIND )
  #  {
  #    print "You need to have the 'valgrind' program in your PATH to run mysql-test-run with option --valgrind. Valgrind's home page is http://valgrind.kde.org.\n"
  #    exit 1
  #  }
    # >=2.1.2 requires the --tool option, some versions write to stdout, some to stderr
  #  valgrind --help 2>&1 | grep "\-\-tool" > /dev/null && VALGRIND="$VALGRIND --tool=memcheck"
  #  VALGRIND="$VALGRIND --alignment=8 --leak-check=yes --num-callers=16"
  #  $opt_extra_mysqld_opt.= " --skip-safemalloc --skip-bdb";
  #  SLEEP_TIME_AFTER_RESTART=10
  #  $opt_sleep_time_for_delete=  60
  #  $glob_use_running_server= ""
  #  if ( "$1"=  "--valgrind-all" )
  #  {
  #    VALGRIND="$VALGRIND -v --show-reachable=yes"
  #  }
  #}

  if ( $opt_user )
  {
    $glob_user=  $opt_user;
  }
  elsif ( $glob_use_running_server )
  {
    $glob_user=  "test";
  }
  else
  {
    $glob_user=  "root"; # We want to do FLUSH xxx commands
  }

}


##############################################################################
#
#  Set paths to various executable programs
#
##############################################################################

sub executable_setup () {

  if ( $opt_source_dist )
  {
    if ( $glob_use_embedded_server )
    {
      if ( -f "$glob_basedir/libmysqld/examples/mysqltest" )
      {
        $exe_mysqltest=  "$glob_basedir/libmysqld/examples/mysqltest";
      }
      else
      {
        mtr_error("Cannot find embedded server 'mysqltest'");
      }
      $path_tests_bindir= "$glob_basedir/libmysqld/examples";
    }
    else
    {
      if ( -f "$glob_basedir/client/.libs/lt-mysqltest" )
      {
        $exe_mysqltest=  "$glob_basedir/client/.libs/lt-mysqltest";
      }
      elsif ( -f "$glob_basedir/client/.libs/mysqltest" )
      {
        $exe_mysqltest=  "$glob_basedir/client/.libs/mysqltest";
      }
      else
      {
        $exe_mysqltest=  "$glob_basedir/client/mysqltest";
      }
      $path_tests_bindir= "$glob_basedir/tests";
    }
    if ( -f "$glob_basedir/client/.libs/mysqldump" )
    {
      $exe_mysqldump=  "$glob_basedir/client/.libs/mysqldump";
    }
    else
    {
      $exe_mysqldump=  "$glob_basedir/client/mysqldump";
    }
    if ( -f "$glob_basedir/client/.libs/mysqlbinlog" )
    {
      $exe_mysqlbinlog=  "$glob_basedir/client/.libs/mysqlbinlog";
    }
    else
    {
      $exe_mysqlbinlog=   "$glob_basedir/client/mysqlbinlog";
    }

    $exe_mysqld= "$glob_basedir/sql/mysqld";
    $path_client_bindir= "$glob_basedir/client";
    $exe_mysqladmin=    "$path_client_bindir/mysqladmin";
    $exe_mysql=         "$path_client_bindir/mysql";
    $path_language=      "$glob_basedir/sql/share/english/";
    $path_charsetsdir=   "$glob_basedir/sql/share/charsets";
  }
  else
  {
    $path_client_bindir= "$glob_basedir/bin";
    $path_tests_bindir=  "$glob_basedir/tests";
    $exe_mysqltest=     "$path_client_bindir/mysqltest";
    $exe_mysqldump=     "$path_client_bindir/mysqldump";
    $exe_mysqlbinlog=  "$path_client_bindir/mysqlbinlog";
    $exe_mysqladmin=    "$path_client_bindir/mysqladmin";
    $exe_mysql=         "$path_client_bindir/mysql";
    if ( -d "$glob_basedir/share/mysql/english" )
    {
      $path_language    ="$glob_basedir/share/mysql/english/";
      $path_charsetsdir ="$glob_basedir/share/mysql/charsets";
    }
    else
    {
      $path_language    ="$glob_basedir/share/english/";
      $path_charsetsdir ="$glob_basedir/share/charsets";
    }

    if ( -x "$glob_basedir/libexec/mysqld" )
    {
      $exe_mysqld= "$glob_basedir/libexec/mysqld";
    }
    else
    {
      $exe_mysqld= "$glob_basedir/bin/mysqld";
    }

  }

  # FIXME special $exe_master_mysqld and $exe_slave_mysqld
  # are not used that much....

  if ( ! $exe_master_mysqld )
  {
    $exe_master_mysqld=  $exe_mysqld;
  }

  if ( ! $exe_slave_mysqld )
  {
    $exe_slave_mysqld=  $exe_mysqld;
  }
}


##############################################################################
#
#  If we get a ^C, we try to clean up before termination
#
##############################################################################
# FIXME check restrictions what to do in a signal handler

sub signal_setup () {
  $SIG{INT}= \&handle_int_signal;
}

sub handle_int_signal () {
  $SIG{INT}= 'DEFAULT';         # If we get a ^C again, we die...
  mtr_warning("got INT signal, cleaning up.....");
  stop_masters_slaves();
  mtr_error("We die from ^C signal from user");
}


##############################################################################
#
#  Collect information about test cases we are to run
#
##############################################################################

sub collect_test_cases ($) {
  my $suite= shift;             # Test suite name

  my $testdir;
  my $resdir;

  if ( $suite eq "main" )
  {
    $testdir= "$glob_mysql_test_dir/t";
    $resdir=  "$glob_mysql_test_dir/r";
  }
  else
  {
    $testdir= "$glob_mysql_test_dir/suite/$suite/t";
    $resdir=  "$glob_mysql_test_dir/suite/$suite/r";
  }

  my @tests;               # Array of hash, will be array of C struct

  opendir(TESTDIR, $testdir) or mtr_error("Can't open dir \"$testdir\": $!");

  foreach my $elem ( sort readdir(TESTDIR) ) {
    my $tname= mtr_match_extension($elem,"test");
    next if ! defined $tname;
    next if $opt_do_test and ! defined mtr_match_prefix($elem,$opt_do_test);
    my $path= "$testdir/$elem";

    # ----------------------------------------------------------------------
    # Skip some tests silently
    # ----------------------------------------------------------------------

    if ( $opt_start_from and $tname lt $opt_start_from )
    {
      next;
    }

    # ----------------------------------------------------------------------
    # Skip some tests but include in list, just mark them to skip
    # ----------------------------------------------------------------------

    my $tinfo= {};
    $tinfo->{'name'}= $tname;
    $tinfo->{'result_file'}= "$resdir/$tname.result";
    push(@tests, $tinfo);

    if ( $opt_skip_test and defined mtr_match_prefix($tname,$opt_skip_test) )
    {
      $tinfo->{'skip'}= 1;
      next;
    }

    # FIXME temporary solution, we have a hard coded list of test cases to
    # skip if we are using the embedded server

    if ( $glob_use_embedded_server and
         mtr_match_any_exact($tname,\@skip_if_embedded_server) )
    {
      $tinfo->{'skip'}= 1;
      next;
    }

    # ----------------------------------------------------------------------
    # Collect information about test case
    # ----------------------------------------------------------------------

    $tinfo->{'path'}= $path;

    if ( defined mtr_match_prefix($tname,"rpl") )
    {
      if ( $opt_skip_rpl )
      {
        $tinfo->{'skip'}= 1;
        next;
      }

      # FIXME currently we always restart slaves
      $tinfo->{'slave_restart'}= 1;

      if ( $tname eq 'rpl_failsafe' or $tname eq 'rpl_chain_temp_table' )
      {
        $tinfo->{'slave_num'}= 3;
      }
      else
      {
        $tinfo->{'slave_num'}= 1;
      }
    }

    # FIXME what about embedded_server + ndbcluster, skip ?!

    my $master_opt_file= "$testdir/$tname-master.opt";
    my $slave_opt_file=  "$testdir/$tname-slave.opt";
    my $slave_mi_file=   "$testdir/$tname.slave-mi";
    my $master_sh=       "$testdir/$tname-master.sh";
    my $slave_sh=        "$testdir/$tname-slave.sh";

    if ( -f $master_opt_file )
    {
      $tinfo->{'master_restart'}= 1;    # We think so for now
      # This is a dirty hack from old mysql-test-run, we use the opt file
      # to flag other things as well, it is not a opt list at all
      my $extra_master_opt= mtr_get_opts_from_file($master_opt_file);

      foreach my $opt (@$extra_master_opt)
      {
        my $value;

        $value= mtr_match_prefix($opt, "--timezone=");

        if ( defined $value )
        {
          $ENV{'TZ'}= $value;           # FIXME pass this on somehow....
          $extra_master_opt= [];
          $tinfo->{'master_restart'}= 0;
          last;
        }

        $value= mtr_match_prefix($opt, "--result-file=");

        if ( defined $value )
        {
          $tinfo->{'result_file'}= "r/$value.result";
          if ( $opt_result_ext and $opt_record or
               -f "$tinfo->{'result_file'}$opt_result_ext")
          {
            $tinfo->{'result_file'}.= $opt_result_ext;
          }
          $extra_master_opt= [];
          $tinfo->{'master_restart'}= 0;
          last;
        }
      }

      $tinfo->{'master_opt'}= $extra_master_opt;
    }

    if ( -f $slave_opt_file )
    {
      $tinfo->{'slave_opt'}= mtr_get_opts_from_file($slave_opt_file);
      $tinfo->{'slave_restart'}= 1;
    }

    if ( -f $slave_mi_file )
    {
      $tinfo->{'slave_mi'}= mtr_get_opts_from_file($slave_mi_file);
      $tinfo->{'slave_restart'}= 1;
    }

    if ( -f $master_sh )
    {
      if ( $glob_win32 )
      {
        $tinfo->{'skip'}= 1;
      }
      else
      {
        $tinfo->{'master_sh'}= $master_sh;
        $tinfo->{'master_restart'}= 1;
      }
    }

    if ( -f $slave_sh )
    {
      if ( $glob_win32 )
      {
        $tinfo->{'skip'}= 1;
      }
      else
      {
        $tinfo->{'slave_sh'}= $slave_sh;
        $tinfo->{'slave_restart'}= 1;
      }
    }

    # We can't restart a running server that may be in use

    if ( $glob_use_running_server and
         ( $tinfo->{'master_restart'} or $tinfo->{'slave_restart'} ) )
    {
      $tinfo->{'skip'}= 1;
    }

  }

  closedir TESTDIR;

  return \@tests;
}


##############################################################################
#
#  Handle left overs from previous runs
#
##############################################################################

sub kill_and_cleanup () {

  if ( $opt_fast or $glob_use_embedded_server )
  {
    # FIXME is embedded server really using PID files?!
    unlink($master->[0]->{'path_mypid'});
    unlink($master->[1]->{'path_mypid'});
    unlink($slave->[0]->{'path_mypid'});
    unlink($slave->[1]->{'path_mypid'});
    unlink($slave->[2]->{'path_mypid'});
  }
  else
  {
    # Ensure that no old mysqld test servers are running
    # This is different from terminating processes we have
    # started from ths run of the script, this is terminating
    # leftovers from previous runs.

    mtr_report("Killing Possible Leftover Processes");
    mtr_kill_leftovers();
  }

  if ( $opt_with_ndbcluster and ! $glob_use_running_ndbcluster )
  {
    ndbcluster_stop();
  }

  mtr_report("Removing Stale Files");

  rmtree("$glob_mysql_test_dir/var/log");
  rmtree("$glob_mysql_test_dir/var/ndbcluster");
  rmtree("$glob_mysql_test_dir/var/run");
  rmtree("$glob_mysql_test_dir/var/tmp");

  mkpath("$glob_mysql_test_dir/var/log");
  mkpath("$glob_mysql_test_dir/var/ndbcluster");
  mkpath("$glob_mysql_test_dir/var/run");
  mkpath("$glob_mysql_test_dir/var/tmp");
  mkpath($opt_tmpdir);

  rmtree("$master->[0]->{'path_myddir'}");
  mkpath("$master->[0]->{'path_myddir'}/mysql"); # Need to create subdir?!
  mkpath("$master->[0]->{'path_myddir'}/test");

  rmtree("$master->[1]->{'path_myddir'}");
  mkpath("$master->[1]->{'path_myddir'}/mysql"); # Need to create subdir?!
  mkpath("$master->[1]->{'path_myddir'}/test");

  rmtree("$slave->[0]->{'path_myddir'}");
  mkpath("$slave->[0]->{'path_myddir'}/mysql"); # Need to create subdir?!
  mkpath("$slave->[0]->{'path_myddir'}/test");

  rmtree("$slave->[1]->{'path_myddir'}");
  mkpath("$slave->[1]->{'path_myddir'}/mysql"); # Need to create subdir?!
  mkpath("$slave->[1]->{'path_myddir'}/test");

  rmtree("$slave->[2]->{'path_myddir'}");
  mkpath("$slave->[2]->{'path_myddir'}/mysql"); # Need to create subdir?!
  mkpath("$slave->[2]->{'path_myddir'}/test");

  $opt_wait_for_master=  $opt_sleep_time_for_first_master;
  $opt_wait_for_slave=   $opt_sleep_time_for_first_slave;
}


# FIXME

sub sleep_until_file_created ($$) {
  my $pidfile= shift;
  my $timeout= shift;

  my $loop=  $timeout * 2;
  while ( $loop-- )
  {
    if ( -r $pidfile )
    {
      return;
    }
    sleep(1);
  }

  if ( ! -r $pidfile )
  {
    mtr_error("No $pidfile was created");
  }
}


##############################################################################
#
#  Start the ndb cluster
#
##############################################################################

# FIXME why is there a different start below?!

sub ndbcluster_start () {

  mtr_report("Starting ndbcluster");
  my $ndbcluster_opts=  $opt_bench ? "" : "--small";
  # FIXME check result code?!
  mtr_run("$glob_mysql_test_dir/ndb/ndbcluster",
          ["--port-base=$opt_ndbcluster_port",
           $ndbcluster_opts,
           "--diskless",
           "--initial",
           "--data-dir=$glob_mysql_test_dir/var"],
          "", "", "", "");
}

sub ndbcluster_stop () {
  mtr_run("$glob_mysql_test_dir/ndb/ndbcluster",
          ["--data-dir=$glob_mysql_test_dir/var",
           "--port-base=$opt_ndbcluster_port",
           "--stop"],
          "", "", "", "");
}


##############################################################################
#
#  Run the benchmark suite
#
##############################################################################

sub run_benchmarks ($) {
  my $benchmark=  shift;

  my $args;

  if ( ! $glob_use_embedded_server and ! $opt_local_master )
  {
    $master->[0]->{'pid'}= mysqld_start('master',0,[],[]);
  }

  mtr_init_args(\$args);

  mtr_add_arg($args, "--socket=%s", $master->[0]->{'path_mysock'});
  mtr_add_arg($args, "--user=root");

  if ( $opt_small_bench )
  {
    mtr_add_arg($args, "--small-test");
    mtr_add_arg($args, "--small-tables");
  }

  if ( $opt_with_ndbcluster )
  {
    mtr_add_arg($args, "--create-options=TYPE=ndb");
  }

  my $benchdir=  "$glob_basedir/sql-bench";
  chdir($benchdir);             # FIXME check error

  # FIXME write shorter....

  if ( ! $benchmark )
  {
    mtr_add_arg($args, "--log");
    mtr_run("$glob_mysql_bench_dir/run-all-tests", $args, "", "", "", "");
    # FIXME check result code?!
  }
  elsif ( -x $benchmark )
  {
    mtr_run("$glob_mysql_bench_dir/$benchmark", $args, "", "", "", "");
    # FIXME check result code?!
  }
  else
  {
    mtr_error("Benchmark $benchmark not found");
  }

  chdir($glob_mysql_test_dir);          # Go back

  if ( ! $glob_use_embedded_server )
  {
    stop_masters();
  }
}


##############################################################################
#
#  Run the test suite
#
##############################################################################

# FIXME how to specify several suites to run? Comma separated list?

sub run_tests () {
  run_suite($opt_suite);
}

sub run_suite () {
  my $suite= shift;

  mtr_print_thick_line();

  mtr_report("Finding Tests in $suite suite");

  my $tests= collect_test_cases($suite);

  mtr_report("Starting Tests in $suite suite");

  mtr_print_header();

  foreach my $tinfo ( @$tests )
  {
    run_testcase($tinfo);
  }

  mtr_print_line();

  if ( ! $opt_gdb and ! $glob_use_running_server and
       ! $opt_ddd and ! $glob_use_embedded_server )
  {
    stop_masters_slaves();
  }

  if ( $opt_with_ndbcluster and ! $glob_use_running_ndbcluster )
  {
    ndbcluster_stop();
  }

  if ( $opt_gcov )
  {
    gcov_collect(); # collect coverage information
  }
  if ( $opt_gprof )
  {
    gprof_collect(); # collect coverage information
  }

  mtr_report_stats($tests);
}


##############################################################################
#
#  Initiate the test databases
#
##############################################################################

sub mysql_install_db () {

  mtr_report("Installing Test Databases");

  install_db('master', $master->[0]->{'path_myddir'});
  install_db('slave',  $slave->[0]->{'path_myddir'});

  return 0;
}


sub install_db ($$) {
  my $type=      shift;
  my $data_dir=  shift;

  my $init_db_sql=  "lib/init_db.sql"; # FIXME this is too simple maybe
  my $args;

  mtr_report("Installing \u$type Databases");

  mtr_init_args(\$args);

  mtr_add_arg($args, "--no-defaults");
  mtr_add_arg($args, "--bootstrap");
  mtr_add_arg($args, "--skip-grant-tables");
  mtr_add_arg($args, "--basedir=%s", $path_my_basedir);
  mtr_add_arg($args, "--datadir=%s", $data_dir);
  mtr_add_arg($args, "--skip-innodb");
  mtr_add_arg($args, "--skip-ndbcluster");
  mtr_add_arg($args, "--skip-bdb");

  if ( ! $opt_netware )
  {
    mtr_add_arg($args, "--language=%s", $path_language);
    mtr_add_arg($args, "--character-sets-dir=%s", $path_charsetsdir);
  }

  if ( mtr_run($exe_mysqld, $args, $init_db_sql,
               $path_manager_log, $path_manager_log, "") != 0 )
  {
    mtr_error("Error executing mysqld --bootstrap\n" .
              "Could not install $type test DBs");
  }
}


##############################################################################
#
#  Run a single test case
#
##############################################################################

# When we get here, we have already filtered out test cases that doesn't
# apply to the current setup, for example if we use a running server, test
# cases that restart the server are dropped. So this function should mostly
# be about doing things, not a lot of logic.

# We don't start and kill the servers for each testcase. But some
# testcases needs a restart, because they specify options to start
# mysqld with. After that testcase, we need to restart again, to set
# back the normal options.

sub run_testcase ($) {
  my $tinfo=  shift;

  my $tname= $tinfo->{'name'};

  mtr_tonewfile($opt_current_test,"$tname\n"); # Always tell where we are

  # ----------------------------------------------------------------------
  # If marked to skip, just print out and return.
  # Note that a test case not marked as 'skip' can still be
  # skipped later, because of the test case itself in cooperation
  # with the mysqltest program tells us so.
  # ----------------------------------------------------------------------

  if ( $tinfo->{'skip'} )
  {
    mtr_report_test_name($tinfo);
    mtr_report_test_skipped($tinfo);
    return;
  }

  # ----------------------------------------------------------------------
  # If not using a running servers we may need to stop and restart.
  # We restart in the case we have initiation scripts, server options
  # etc to run. But we also restart again after the test first restart
  # and test is run, to get back to normal server settings.
  #
  # To make the code a bit more clean, we actually only stop servers
  # here, and mark this to be done. Then a generic "start" part will
  # start up the needed servers again.
  # ----------------------------------------------------------------------

  if ( ! $glob_use_running_server and ! $glob_use_embedded_server )
  {
    if ( $tinfo->{'master_restart'} or $master->[0]->{'uses_special_flags'} )
    {
      stop_masters();
      $master->[0]->{'uses_special_flags'}= 0; # Forget about why we stopped
    }

    # ----------------------------------------------------------------------
    # Always terminate all slaves, if any. Else we may have useless
    # reconnection attempts and error messages in case the slave and
    # master servers restart.
    # ----------------------------------------------------------------------

    stop_slaves();
  }    

  # ----------------------------------------------------------------------
  # Prepare to start masters. Even if we use embedded, we want to run
  # the preparation.
  # ----------------------------------------------------------------------

  mtr_tofile($master->[0]->{'path_myerr'},"CURRENT_TEST: $tname\n");
  do_before_start_master($tname,$tinfo->{'master_sh'});

  # ----------------------------------------------------------------------
  # Start masters
  # ----------------------------------------------------------------------

  mtr_report_test_name($tinfo);

  if ( ! $glob_use_running_server and ! $glob_use_embedded_server )
  {
    # FIXME give the args to the embedded server?!
    # FIXME what does $opt_local_master mean?!
    # FIXME split up start and check that started so that can do
    #       starts in parallel, masters and slaves at the same time.

    if ( ! $opt_local_master )
    {
      if ( ! $master->[0]->{'pid'} )
      {
        $master->[0]->{'pid'}=
          mysqld_start('master',0,$tinfo->{'master_opt'},[]);
      }
      if ( $opt_with_ndbcluster and ! $master->[1]->{'pid'} )
      {
        $master->[1]->{'pid'}=
          mysqld_start('master',1,$tinfo->{'master_opt'},[]);
      }

      if ( $tinfo->{'master_opt'} )
      {
        $master->[0]->{'uses_special_flags'}= 1;
      }
    }

    # ----------------------------------------------------------------------
    # Start slaves - if needed
    # ----------------------------------------------------------------------

    if ( $tinfo->{'slave_num'} )
    {
      mtr_tofile($slave->[0]->{'path_myerr'},"CURRENT_TEST: $tname\n");

      do_before_start_slave($tname,$tinfo->{'slave_sh'});

      for ( my $idx= 0; $idx <  $tinfo->{'slave_num'}; $idx++ )
      {
        if ( ! $slave->[$idx]->{'pid'} )
        {
          $slave->[$idx]->{'pid'}=
            mysqld_start('slave',$idx,
                         $tinfo->{'slave_opt'}, $tinfo->{'slave_mi'});
        }
      }
    }
  }

  # ----------------------------------------------------------------------
  # Run the test case
  # ----------------------------------------------------------------------

  {
    unlink("r/$tname.reject");
    unlink($path_timefile);

    my $res= run_mysqltest($tinfo, $tinfo->{'master_opt'});

    if ( $res == 0 )
    {
      mtr_report_test_passed($tinfo);
    }
    elsif ( $res == 2 )
    {
      # Testcase itself tell us to skip this one
      mtr_report_test_skipped($tinfo);
    }
    else
    {
      # Test case failed
      if ( $res > 2 )
      {
        mtr_tofile($path_timefile,
                   "mysqltest returned unexpected code $res, " .
                   "it has probably crashed");
      }
      mtr_report_test_failed($tinfo);
      mtr_show_failed_diff($tname);
      print "\n";
      if ( ! $opt_force )
      {
        print "Aborting: $tname failed. To continue, re-run with '--force'.";
        print "\n";
        if ( ! $opt_gdb and ! $glob_use_running_server and
             ! $opt_ddd and ! $glob_use_embedded_server )
        {
          stop_masters_slaves();
        }
        exit(1);
      }

      # FIXME always terminate on failure?!
      if ( ! $opt_gdb and ! $glob_use_running_server and
           ! $opt_ddd and ! $glob_use_embedded_server )
      {
        stop_masters_slaves();
      }
      print "Resuming Tests\n\n";
    }
  }
}


##############################################################################
#
#  Start and stop servers
#
##############################################################################

# The embedded server needs the cleanup so we do some of the start work
# but stop before actually running mysqld or anything.

sub do_before_start_master ($$) {
  my $tname=  shift;
  my $master_init_script=  shift;

  # FIXME what about second master.....

  # Remove stale binary logs except for 2 tests which need them FIXME here????
  if ( $tname ne "rpl_crash_binlog_ib_1b" and
       $tname ne "rpl_crash_binlog_ib_2b" and
       $tname ne "rpl_crash_binlog_ib_3b")
  {
    # FIXME we really want separate dir for binlogs
    `rm -fr $glob_mysql_test_dir/var/log/master-bin.*`;
#    unlink("$glob_mysql_test_dir/var/log/master-bin.*");
  }

  # Remove old master.info and relay-log.info files
  unlink("$glob_mysql_test_dir/var/master-data/master.info");
  unlink("$glob_mysql_test_dir/var/master-data/relay-log.info");
  unlink("$glob_mysql_test_dir/var/master1-data/master.info");
  unlink("$glob_mysql_test_dir/var/master1-data/relay-log.info");

  #run master initialization shell script if one exists

  if ( $master_init_script and
       mtr_run($master_init_script, [], "", "", "", "") != 0 )
  {
    mtr_error("Can't run $master_init_script");
  }
  # for gcov  FIXME needed? If so we need more absolute paths
# chdir($glob_basedir);
}

sub do_before_start_slave ($$) {
  my $tname=  shift;
  my $slave_init_script=  shift;

  # When testing fail-safe replication, we will have more than one slave
  # in this case, we start secondary slaves with an argument

  # Remove stale binary logs and old master.info files
  # except for too tests which need them
  if ( $tname ne "rpl_crash_binlog_ib_1b" and
       $tname ne "rpl_crash_binlog_ib_2b" and
       $tname ne "rpl_crash_binlog_ib_3b" )
  {
    # FIXME we really want separate dir for binlogs
    `rm -fr $glob_mysql_test_dir/var/log/slave*-bin.*`;
#    unlink("$glob_mysql_test_dir/var/log/slave*-bin.*"); # FIXME idx???
    # FIXME really master?!
    unlink("$glob_mysql_test_dir/var/slave-data/master.info");
    unlink("$glob_mysql_test_dir/var/slave-data/relay-log.info");
  }

  #run slave initialization shell script if one exists
  if ( $slave_init_script and
       mtr_run($slave_init_script, [], "", "", "", "") != 0 )
  {
    mtr_error("Can't run $slave_init_script");
  }

  unlink("$glob_mysql_test_dir/var/slave-data/log.*");
}

sub mysqld_arguments ($$$$$) {
  my $args=       shift;
  my $type=       shift;        # master/slave/bootstrap
  my $idx=        shift;
  my $extra_opt=  shift;
  my $slave_master_info=  shift;

  my $sidx= "";                 # Index as string, 0 is empty string
  if ( $idx > 0 )
  {
    $sidx= sprintf("%d", $idx); # sprintf not needed in Perl for this
  }

  my $prefix= "";               # If mysqltest server arg

  if ( $glob_use_embedded_server )
  {
    $prefix= "--server-arg=";
  } else {
    # We can't pass embedded server --no-defaults
    mtr_add_arg($args, "%s--no-defaults", $prefix);
  }

  mtr_add_arg($args, "%s--basedir=%s", $prefix, $path_my_basedir);
  mtr_add_arg($args, "%s--character-sets-dir=%s", $prefix, $path_charsetsdir);
  mtr_add_arg($args, "%s--core", $prefix);
  mtr_add_arg($args, "%s--default-character-set=latin1", $prefix);
  mtr_add_arg($args, "%s--language=%s", $prefix, $path_language);
  mtr_add_arg($args, "%s--tmpdir=$opt_tmpdir", $prefix);

  if ( $opt_valgrind )
  {
    mtr_add_arg($args, "%s--skip-safemalloc", $prefix);
    mtr_add_arg($args, "%s--skip-bdb", $prefix);
  }

  my $pidfile;

  if ( $type eq 'master' )
  {
    mtr_add_arg($args, "%s--log-bin=%s/var/log/master-bin", $prefix,
                $glob_mysql_test_dir);
    mtr_add_arg($args, "%s--pid-file=%s", $prefix,
                $master->[$idx]->{'path_mypid'});
    mtr_add_arg($args, "%s--port=%d", $prefix,
                $master->[$idx]->{'path_myport'});
    mtr_add_arg($args, "%s--server-id=1", $prefix);
    mtr_add_arg($args, "%s--socket=%s", $prefix,
                $master->[$idx]->{'path_mysock'});
    mtr_add_arg($args, "%s--innodb_data_file_path=ibdata1:50M", $prefix);
    mtr_add_arg($args, "%s--local-infile", $prefix);
    mtr_add_arg($args, "%s--datadir=%s", $prefix,
                $master->[$idx]->{'path_myddir'});
  }

  if ( $type eq 'slave' )
  {
    my $slave_server_id=  2 + $idx;
    my $slave_rpl_rank= $idx > 0 ? 2 : $slave_server_id;

    mtr_add_arg($args, "%s--datadir=%s", $prefix,
                $slave->[$idx]->{'path_myddir'});
    mtr_add_arg($args, "%s--exit-info=256", $prefix);
    mtr_add_arg($args, "%s--init-rpl-role=slave", $prefix);
    mtr_add_arg($args, "%s--log-bin=%s/var/log/slave%s-bin", $prefix,
                $glob_mysql_test_dir, $sidx); # FIXME use own dir for binlogs
    mtr_add_arg($args, "%s--log-slave-updates", $prefix);
    mtr_add_arg($args, "%s--log=%s", $prefix,
                $slave->[$idx]->{'path_myerr'});
    mtr_add_arg($args, "%s--master-retry-count=10", $prefix);
    mtr_add_arg($args, "%s--pid-file=%s", $prefix,
                $slave->[$idx]->{'path_mypid'});
    mtr_add_arg($args, "%s--port=%d", $prefix,
                $slave->[$idx]->{'path_myport'});
    mtr_add_arg($args, "%s--relay-log=%s/var/log/slave%s-relay-bin", $prefix,
                $glob_mysql_test_dir, $sidx);
    mtr_add_arg($args, "%s--report-host=127.0.0.1", $prefix);
    mtr_add_arg($args, "%s--report-port=%d", $prefix,
                $slave->[$idx]->{'path_myport'});
    mtr_add_arg($args, "%s--report-user=root", $prefix);
    mtr_add_arg($args, "%s--skip-innodb", $prefix);
    mtr_add_arg($args, "%s--skip-ndbcluster", $prefix);
    mtr_add_arg($args, "%s--skip-slave-start", $prefix);
    mtr_add_arg($args, "%s--slave-load-tmpdir=%s", $prefix,
                $path_slave_load_tmpdir);
    mtr_add_arg($args, "%s--socket=%s", $prefix,
                $slave->[$idx]->{'path_mysock'});
    mtr_add_arg($args, "%s--set-variable=slave_net_timeout=10", $prefix);

    if ( @$slave_master_info )
    {
      foreach my $arg ( @$slave_master_info )
      {
        mtr_add_arg($args, "%s%s", $prefix, $arg);
      }
    }
    else
    {
      mtr_add_arg($args, "%s--master-user=root", $prefix);
      mtr_add_arg($args, "%s--master-connect-retry=1", $prefix);
      mtr_add_arg($args, "%s--master-host=127.0.0.1", $prefix);
      mtr_add_arg($args, "%s--master-password=", $prefix);
      mtr_add_arg($args, "%s--master-port=%d", $prefix,
                  $master->[0]->{'path_myport'}); # First master
      mtr_add_arg($args, "%s--server-id=%d", $prefix, $slave_server_id);
      mtr_add_arg($args, "%s--rpl-recovery-rank=%d", $prefix, $slave_rpl_rank);
    }
  } # end slave

  if ( $opt_debug )
  {
    if ( $type eq 'master' )
    {
      mtr_add_arg($args, "--debug=d:t:i:A,%s/var/log/master%s.trace",
                  $prefix, $glob_mysql_test_dir, $sidx);
    }
    if ( $type eq 'slave' )
    {
      mtr_add_arg($args, "--debug=d:t:i:A,%s/var/log/slave%s.trace",
                  $prefix, $glob_mysql_test_dir, $sidx);
    }
  }

  if ( $opt_with_ndbcluster )
  {
    mtr_add_arg($args, "%s--ndbcluster", $prefix);

    if ( $glob_use_running_ndbcluster )
    {
      mtr_add_arg($args,"--ndb-connectstring=%s", $prefix,
                  $opt_ndbconnectstring);
    }
    else
    {
      mtr_add_arg($args,"--ndb-connectstring=host=localhost:%d",
                  $prefix, $opt_ndbcluster_port);
    }
  }

  # FIXME always set nowdays??? SMALL_SERVER
  mtr_add_arg($args, "%s--key_buffer_size=1M", $prefix);
  mtr_add_arg($args, "%s--sort_buffer=256K", $prefix);
  mtr_add_arg($args, "%s--max_heap_table_size=1M", $prefix);

  if ( $opt_with_openssl )
  {
    mtr_add_arg($args, "%s--ssl-ca=%s/SSL/cacert.pem", $prefix, $glob_basedir);
    mtr_add_arg($args, "%s--ssl-cert=%s/SSL/server-cert.pem", $prefix,
                $glob_basedir);
    mtr_add_arg($args, "%s--ssl-key=%s/SSL/server-key.pem", $prefix,
                $glob_basedir);
  }

  if ( $opt_warnings )
  {
    mtr_add_arg($args, "%s--log-warnings", $prefix);
  }

  if ( $opt_gdb or $opt_client_gdb or $opt_manual_gdb or $opt_ddd)
  {
    mtr_add_arg($args, "%s--gdb", $prefix);
  }

  # If we should run all tests cases, we will use a local server for that

  if ( -w "/" )
  {
    # We are running as root;  We need to add the --root argument
    mtr_add_arg($args, "%s--user=root", $prefix);
  }

  if ( $type eq 'master' )
  {

    if ( ! $opt_old_master )
    {
      mtr_add_arg($args, "%s--rpl-recovery-rank=1", $prefix);
      mtr_add_arg($args, "%s--init-rpl-role=master", $prefix);
    }

    # FIXME strange,.....
    if ( $opt_local_master )
    {
      mtr_add_arg($args, "%s--host=127.0.0.1", $prefix);
      mtr_add_arg($args, "%s--port=%s", $prefix, $ENV{'MYSQL_MYPORT'});
    }
  }

  foreach my $arg ( @$extra_opt )
  {
    mtr_add_arg($args, "%s%s", $prefix, $arg);
  }

  if ( $opt_bench )
  {
    mtr_add_arg($args, "%s--rpl-recovery-rank=1", $prefix);
    mtr_add_arg($args, "%s--init-rpl-role=master", $prefix);
  }
  else
  {
    mtr_add_arg($args, "%s--exit-info=256", $prefix);
    mtr_add_arg($args, "%s--open-files-limit=1024", $prefix);

    if ( $type eq 'master' )
    {
      mtr_add_arg($args, "%s--log=%s", $prefix, $master->[0]->{'path_mylog'});
    }
    if ( $type eq 'slave' )
    {
      mtr_add_arg($args, "%s--log=%s", $prefix, $slave->[0]->{'path_mylog'});
    }
  }

  return $args;
}

# FIXME
#  if ( $type eq 'master' and $glob_use_embedded_server )
#  {
#    # Add a -A to each argument to pass it to embedded server
#    my @mysqltest_opt=  map {("-A",$_)} @args;
#    $opt_extra_mysqltest_opt=  \@mysqltest_opt;
#    return;
#  }

##############################################################################
#
#  Start mysqld and return the PID
#
##############################################################################

sub mysqld_start ($$$$) {
  my $type=       shift;        # master/slave/bootstrap
  my $idx=        shift;
  my $extra_opt=  shift;
  my $slave_master_info=  shift;

  my $args;                             # Arg vector
  my $exe;
  my $pid;

  # FIXME code duplication, make up your mind....
  if ( $opt_source_dist )
  {
    $exe= "$glob_basedir/sql/mysqld";
  }
  else
  {
    $exe ="$glob_basedir/libexec/mysqld";
    if ( ! -x $exe )
    {
      $exe ="$glob_basedir/bin/mysqld";
    }
  }

  mtr_init_args(\$args);

  if ( $opt_valgrind )
  {

    mtr_add_arg($args, "--tool=memcheck");
    mtr_add_arg($args, "--alignment=8");
    mtr_add_arg($args, "--leak-check=yes");
    mtr_add_arg($args, "--num-callers=16");

    if ( $opt_valgrind_all )
    {
      mtr_add_arg($args, "-v");
      mtr_add_arg($args, "--show-reachable=yes");
    }

    if ( $opt_valgrind_options )
    {
      # FIXME split earlier and put into @glob_valgrind_*
      mtr_add_arg($args, split(' ', $opt_valgrind_options));
    }

    mtr_add_arg($args, $exe);

    $exe=  $opt_valgrind;
  }

  mysqld_arguments($args,$type,$idx,$extra_opt,$slave_master_info);

  if ( $type eq 'master' )
  {
    if ( $pid= mtr_spawn($exe, $args, "",
                         $master->[$idx]->{'path_myerr'},
                         $master->[$idx]->{'path_myerr'}, "") )
    {
      sleep_until_file_created($master->[$idx]->{'path_mypid'},
                               $opt_wait_for_master);
      $opt_wait_for_master= $opt_sleep_time_for_second_master;
      return $pid;
    }
  }

  if ( $type eq 'slave' )
  {
    if ( $pid= mtr_spawn($exe, $args, "",
                         $slave->[$idx]->{'path_myerr'},
                         $slave->[$idx]->{'path_myerr'}, "") )
    {
      sleep_until_file_created($slave->[$idx]->{'path_mypid'},
                               $opt_wait_for_slave);
      $opt_wait_for_slave= $opt_sleep_time_for_second_slave;
      return $pid;
    }
  }

  mtr_error("Can't start mysqld FIXME");
}

sub stop_masters_slaves () {

  print  "Ending Tests\n";
  print  "Shutting-down MySQL daemon\n\n";
  stop_masters();
  print "Master(s) shutdown finished\n";
  stop_slaves();
  print "Slave(s) shutdown finished\n";
}

sub stop_masters () {

  my @args;

  for ( my $idx; $idx < 2; $idx++ )
  {
    # FIXME if we hit ^C before fully started, this test will prevent
    # the mysqld process from being killed
    if ( $master->[$idx]->{'pid'} )
    {
      push(@args,
           $master->[$idx]->{'path_mypid'},
           $master->[$idx]->{'path_mysock'},
         );
      $master->[$idx]->{'pid'}= 0;
    }
  }

  mtr_stop_servers(\@args);
}

sub stop_slaves () {
  my $force= shift;

  my @args;

  for ( my $idx; $idx < 3; $idx++ )
  {
    if ( $slave->[$idx]->{'pid'} )
    {
      push(@args,
           $slave->[$idx]->{'path_mypid'},
           $slave->[$idx]->{'path_mysock'},
         );
      $slave->[$idx]->{'pid'}= 0;
    }
  }

  mtr_stop_servers(\@args);
}


sub run_mysqltest ($$) {
  my $tinfo=       shift;
  my $master_opts= shift;

  # FIXME set where????
  my $cmdline_mysqldump= "$exe_mysqldump --no-defaults -uroot " .
                         "--socket=$master->[0]->{'path_mysock'} --password=";
  if ( $opt_debug )
  {
    $cmdline_mysqldump .=
      " --debug=d:t:A,$glob_mysql_test_dir/var/log/mysqldump.trace";
  }

  my $cmdline_mysqlbinlog=
    "$exe_mysqlbinlog --no-defaults --local-load=$opt_tmpdir";

  if ( $opt_debug )
  {
    $cmdline_mysqlbinlog .=
      " --debug=d:t:A,$glob_mysql_test_dir/var/log/mysqlbinlog.trace";
  }

  my $cmdline_mysql=
    "$exe_mysql --host=localhost --port=$master->[0]->{'path_myport'} " .
    "--socket=$master->[0]->{'path_mysock'} --user=root --password=";

  $ENV{'MYSQL'}=                    $exe_mysql;
  $ENV{'MYSQL_DUMP'}=               $cmdline_mysqldump;
  $ENV{'MYSQL_BINLOG'}=             $exe_mysqlbinlog;
  $ENV{'CLIENT_BINDIR'}=            $path_client_bindir;
  $ENV{'TESTS_BINDIR'}=             $path_tests_bindir;

  my $exe= $exe_mysqltest;
  my $args;

  mtr_init_args(\$args);

  mtr_add_arg($args, "--no-defaults");
  mtr_add_arg($args, "--socket=%s", $master->[0]->{'path_mysock'});
  mtr_add_arg($args, "--database=test");
  mtr_add_arg($args, "--user=%s", $glob_user);
  mtr_add_arg($args, "--password=");
  mtr_add_arg($args, "--silent");
  mtr_add_arg($args, "-v");
  mtr_add_arg($args, "--skip-safemalloc");
  mtr_add_arg($args, "--tmpdir=%s", $opt_tmpdir);
  mtr_add_arg($args, "--port=%d", $master->[0]->{'path_myport'});

  if ( $opt_ps_protocol )
  {
    mtr_add_arg($args, "--ps-protocol");
  }

  if ( $opt_strace_client )
  {
    $exe=  "strace";            # FIXME there are ktrace, ....
    mtr_add_arg($args, "-o");
    mtr_add_arg($args, "%s/var/log/mysqltest.strace", $glob_mysql_test_dir);
    mtr_add_arg($args, "$exe_mysqltest");
  }

  if ( $opt_timer )
  {
    mtr_add_arg($args, "--timer-file=var/log/timer");
  }

  if ( $opt_big_test )
  {
    mtr_add_arg($args, "--big-test");
  }

  if ( $opt_record )
  {
    mtr_add_arg($args, "--record");
  }

  if ( $opt_compress )
  {
    mtr_add_arg($args, "--compress");
  }

  if ( $opt_sleep )
  {
    mtr_add_arg($args, "--sleep=%d", $opt_sleep);
  }

  if ( $opt_debug )
  {
    mtr_add_arg($args, "--debug=d:t:A,%s/var/log/mysqltest.trace",
                $glob_mysql_test_dir);
  }

  if ( $opt_with_openssl )
  {
    mtr_add_arg($args, "--ssl-ca=%s/SSL/cacert.pem", $glob_basedir);
    mtr_add_arg($args, "--ssl-cert=%s/SSL/client-cert.pem", $glob_basedir);
    mtr_add_arg($args, "--ssl-key=%s/SSL/client-key.pem", $glob_basedir);
  }

  mtr_add_arg($args, "-R");
  mtr_add_arg($args, $tinfo->{'result_file'});

  # ----------------------------------------------------------------------
  # If embedded server, we create server args to give mysqltest to pass on
  # ----------------------------------------------------------------------

  if ( $glob_use_embedded_server )
  {
    mysqld_arguments($args,'master',0,$tinfo->{'master_opt'},[]);
  }

  return mtr_run($exe_mysqltest,$args,$tinfo->{'path'},"",$path_timefile,"");
}

##############################################################################
#
#  Usage
#
##############################################################################

sub usage ($)
{
  print STDERR <<HERE;

mysql-test-run [ OPTIONS ] [ TESTCASE ]

FIXME when is TESTCASE arg used or not?!

Options to control what engine/variation to run

  embedded-server       Use the embedded server, i.e. no mysqld daemons
  ps-protocol           Use the binary protocol between client and server
  bench                 Run the benchmark suite FIXME
  small-bench           FIXME
  no-manager            Use the istanse manager (currently disabled)

Options to control what test suites or cases to run

  force                 Continue to run the suite after failure
  with-ndbcluster       Use cluster, and enable test cases that requres it
  do-test=PREFIX        Run test cases which name are prefixed with PREFIX
  start-from=PREFIX     Run test cases starting from test prefixed with PREFIX
  suite=NAME            Run the test suite named NAME. The default is "main"
  skip-rpl              Skip the replication test cases.
  skip-test=PREFIX      Skip test cases which name are prefixed with PREFIX

Options that specify ports

  master_port=PORT      Specify the port number used by the first master
  slave_port=PORT       Specify the port number used by the first slave
  ndbcluster_port=i     Specify the port number used by cluster FIXME

Options for test case authoring

  record TESTNAME       (Re)genereate the result file for TESTNAME

Options that pass on options

  mysqld=ARGS           Specify additional arguments to "mysqld"

Options to run test on running server

  extern                Use running server for tests FIXME DANGEROUS
  ndbconnectstring=STR  Use running cluster, and connect using STR      
  user=USER             The databse user name

Options for debugging the product

  gdb                   FIXME
  manual-gdb            FIXME
  client-gdb            FIXME
  ddd                   FIXME
  strace-client         FIXME
  master-binary=PATH    Specify the master "mysqld" to use
  slave-binary=PATH     Specify the slave "mysqld" to use

Options for coverage, profiling etc

  gcov                  FIXME
  gprof                 FIXME
  valgrind              FIXME
  valgrind-all          FIXME
  valgrind-options=ARGS Extra options to give valgrind

Misc options

  verbose               Verbose output from this script
  script-debug          Debug this script itself
  compress              Use the compressed protocol between client and server
  timer                 Show test case execution time
  start-and-exit        Only initiate and start the "mysqld" servers
  fast                  Don't try to cleanup from earlier runs
  help                  Get this help text

Options not yet described, or that I want to look into more

  big-test              
  debug                 
  local                 
  local-master          
  netware               
  old-master            
  sleep=SECONDS         
  socket=PATH           
  tmpdir=DIR            
  user-test=s           
  wait-timeout=SECONDS  
  warnings              
  log-warnings          
  with-openssl          

HERE
  exit(1);
}
