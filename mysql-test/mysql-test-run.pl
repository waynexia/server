#!/usr/bin/perl
# -*- cperl -*-

#
##############################################################################
#
#  mysql-test-run.pl
#
#  Tool used for executing a suite of .test files
#
#  See the "MySQL Test framework manual" for more information
#  http://dev.mysql.com/doc/mysqltest/en/index.html
#
#
##############################################################################

use strict;
use warnings;

BEGIN {
  # Check that mysql-test-run.pl is started from mysql-test/
  unless ( -f "mysql-test-run.pl" )
  {
    print "**** ERROR **** ",
      "You must start mysql-test-run from the mysql-test/ directory\n";
    exit(1);
  }
  # Check that lib exist
  unless ( -d "lib/" )
  {
    print "**** ERROR **** ",
      "Could not find the lib/ directory \n";
    exit(1);
  }
}

BEGIN {
  # Check backward compatibility support
  # By setting the environment variable MTR_VERSION
  # it's possible to use a previous version of
  # mysql-test-run.pl
  my $version= $ENV{MTR_VERSION} || 2;
  if ( $version == 1 )
  {
    print "=======================================================\n";
    print "  WARNING: Using mysql-test-run.pl version 1!  \n";
    print "=======================================================\n";
    # Should use exec() here on *nix but this appears not to work on Windows
    exit(system($^X, "lib/v1/mysql-test-run.pl", @ARGV) >> 8);
  }
  elsif ( $version == 2 )
  {
    # This is the current version, just continue
    ;
  }
  else
  {
    print "ERROR: Version $version of mysql-test-run does not exist!\n";
    exit(1);
  }
}

use lib "lib";

use Cwd;
use Getopt::Long;
use My::File::Path; # Patched version of File::Path
use File::Basename;
use File::Copy;
use File::Find;
use File::Temp qw / tempdir /;
use File::Spec::Functions qw / splitdir /;
use My::Platform;
use My::SafeProcess;
use My::ConfigFactory;
use My::Options;
use My::Find;
use My::SysInfo;
use My::CoreDump;
use mtr_cases;
use mtr_report;
use mtr_match;
use mtr_unique;
use IO::Socket::INET;
use IO::Select;

require "lib/mtr_process.pl";
require "lib/mtr_io.pl";
require "lib/mtr_gcov.pl";
require "lib/mtr_misc.pl";

$SIG{INT}= sub { mtr_error("Got ^C signal"); };

our $mysql_version_id;
our $glob_mysql_test_dir;
our $basedir;

our $path_charsetsdir;
our $path_client_bindir;
our $path_client_libdir;
our $path_language;

our $path_current_testlog;
our $path_testlog;

our $default_vardir;
our $opt_vardir;                # Path to use for var/ dir
my $path_vardir_trace;          # unix formatted opt_vardir for trace files
my $opt_tmpdir;                 # Path to use for tmp/ dir
my $opt_tmpdir_pid;

END {
  if ( defined $opt_tmpdir_pid and $opt_tmpdir_pid == $$ )
  {
    # Remove the tempdir this process has created
    mtr_verbose("Removing tmpdir '$opt_tmpdir");
    rmtree($opt_tmpdir);
  }
}

my $path_config_file;           # The generated config file, var/my.cnf

# Visual Studio produces executables in different sub-directories based on the
# configuration used to build them.  To make life easier, an environment
# variable or command-line option may be specified to control which set of
# executables will be used by the test suite.
our $opt_vs_config = $ENV{'MTR_VS_CONFIG'};

my $DEFAULT_SUITES= "main,binlog,federated,rpl,rpl_ndb,ndb,innodb";
my $opt_suites;

our $opt_verbose= 0;  # Verbose output, enable with --verbose
our $exe_mysql;
our $exe_mysqladmin;
our $exe_mysqltest;
our $exe_libtool;

our $opt_big_test= 0;

our @opt_combinations;

our @opt_extra_mysqld_opt;

my $opt_compress;
my $opt_ssl;
my $opt_skip_ssl;
our $opt_ssl_supported;
my $opt_ps_protocol;
my $opt_sp_protocol;
my $opt_cursor_protocol;
my $opt_view_protocol;

our $opt_debug;
our @opt_cases;                  # The test cases names in argv
our $opt_embedded_server;

# Options used when connecting to an already running server
my %opts_extern;
sub using_extern { return (keys %opts_extern > 0);};

our $opt_fast= 0;
our $opt_force;
our $opt_mem= $ENV{'MTR_MEM'};

our $opt_gcov;
our $opt_gcov_exe= "gcov";
our $opt_gcov_err= "mysql-test-gcov.msg";
our $opt_gcov_msg= "mysql-test-gcov.err";

our $glob_debugger= 0;
our $opt_gdb;
our $opt_client_gdb;
our $opt_ddd;
our $opt_client_ddd;
our $opt_manual_gdb;
our $opt_manual_ddd;
our $opt_manual_debug;
our $opt_debugger;
our $opt_client_debugger;

my $config; # The currently running config
my $current_config_name; # The currently running config file template

our $opt_experimental;
our $experimental_test_cases;

my $baseport;
my $opt_build_thread= $ENV{'MTR_BUILD_THREAD'} || "auto";
my $build_thread= 0;

my $opt_record;
my $opt_report_features;

my $opt_skip_core;

our $opt_check_testcases= 1;
my $opt_mark_progress;

my $opt_sleep;

my $opt_testcase_timeout=    15; # minutes
my $opt_suite_timeout   =   300; # minutes
my $opt_shutdown_timeout=    10; # seconds
my $opt_start_timeout   =   180; # seconds

sub testcase_timeout { return $opt_testcase_timeout * 60; };
sub suite_timeout { return $opt_suite_timeout * 60; };
sub check_timeout { return $opt_testcase_timeout * 6; };

my $opt_start;
my $opt_start_dirty;
my $start_only;
my $opt_wait_all;
my $opt_repeat= 1;
my $opt_retry= 3;
my $opt_retry_failure= 2;

my $opt_strace_client;

our $opt_user = "root";

my $opt_valgrind= 0;
my $opt_valgrind_mysqld= 0;
my $opt_valgrind_mysqltest= 0;
my @default_valgrind_args= ("--show-reachable=yes");
my @valgrind_args;
my $opt_valgrind_path;
my $opt_callgrind;

our $opt_warnings= 1;

our $opt_skip_ndbcluster= 0;

my $exe_ndbd;
my $exe_ndb_mgmd;
my $exe_ndb_waiter;

our $debug_compiled_binaries;

our %mysqld_variables;

my $source_dist= 0;

my $opt_max_save_core= $ENV{MTR_MAX_SAVE_CORE} || 5;
my $opt_max_save_datadir= $ENV{MTR_MAX_SAVE_DATADIR} || 20;
my $opt_max_test_fail= $ENV{MTR_MAX_TEST_FAIL} || 10;

my $opt_parallel= $ENV{MTR_PARALLEL} || 1;

select(STDOUT);
$| = 1; # Automatically flush STDOUT

main();


sub main {
  # Default, verbosity on
  report_option('verbose', 0);

  # This is needed for test log evaluation in "gen-build-status-page"
  # in all cases where the calling tool does not log the commands
  # directly before it executes them, like "make test-force-pl" in RPM builds.
  mtr_report("Logging: $0 ", join(" ", @ARGV));

  command_line_setup();

  if ( $opt_gcov ) {
    gcov_prepare($basedir);
  }

  if (!$opt_suites) {
    $opt_suites= $DEFAULT_SUITES;

    # Check for any extra suites to enable based on the path name
    my %extra_suites=
      (
       "mysql-5.1-new-ndb"              => "ndb_team",
       "mysql-5.1-new-ndb-merge"        => "ndb_team",
       "mysql-5.1-telco-6.2"            => "ndb_team",
       "mysql-5.1-telco-6.2-merge"      => "ndb_team",
       "mysql-5.1-telco-6.3"            => "ndb_team",
       "mysql-6.0-ndb"                  => "ndb_team",
      );

    foreach my $dir ( reverse splitdir($basedir) ) {
      my $extra_suite= $extra_suites{$dir};
      if (defined $extra_suite) {
	mtr_report("Found extra suite: $extra_suite");
	$opt_suites= "$extra_suite,$opt_suites";
	last;
      }
    }
  }

  mtr_report("Collecting tests...");
  my $tests= collect_test_cases($opt_suites, \@opt_cases);

  if ( $opt_report_features ) {
    # Put "report features" as the first test to run
    my $tinfo = My::Test->new
      (
       name           => 'report_features',
       # No result_file => Prints result
       path           => 'include/report-features.test',
       template_path  => "include/default_my.cnf",
       master_opt     => [],
       slave_opt      => [],
      );
    unshift(@$tests, $tinfo);
  }

  print "vardir: $opt_vardir\n";
  initialize_servers();

  #######################################################################
  my $num_tests= @$tests;
  if ( $opt_parallel eq "auto" ) {
    # Try to find a suitable value for number of workers
    my $sys_info= My::SysInfo->new();

    $opt_parallel= $sys_info->num_cpus();
    for my $limit (2000, 1500, 1000, 500){
      $opt_parallel-- if ($sys_info->min_bogomips() < $limit);
    }
    my $max_par= $ENV{MTR_MAX_PARALLEL} || 8;
    $opt_parallel= $max_par if ($opt_parallel > $max_par);
    $opt_parallel= $num_tests if ($opt_parallel > $num_tests);
    $opt_parallel= 1 if (IS_WINDOWS and $sys_info->isvm());
    $opt_parallel= 1 if ($opt_parallel < 1);
    mtr_report("Using parallel: $opt_parallel");
  }

  # Create server socket on any free port
  my $server = new IO::Socket::INET
    (
     LocalAddr => 'localhost',
     Proto => 'tcp',
     Listen => $opt_parallel,
    );
  mtr_error("Could not create testcase server port: $!") unless $server;
  my $server_port = $server->sockport();
  mtr_report("Using server port $server_port");

  # Create child processes
  my %children;
  for my $child_num (1..$opt_parallel){
    my $child_pid= My::SafeProcess::Base::_safe_fork();
    if ($child_pid == 0){
      $server= undef; # Close the server port in child
      $tests= {}; # Don't need the tests list in child

      # Use subdir of var and tmp unless only one worker
      if ($opt_parallel > 1) {
	set_vardir("$opt_vardir/$child_num");
	$opt_tmpdir= "$opt_tmpdir/$child_num";
      }

      run_worker($server_port, $child_num);
      exit(1);
    }

    $children{$child_pid}= 1;
  }
  #######################################################################

  mtr_report();
  mtr_print_thick_line();
  mtr_print_header();

  my $completed= run_test_server($server, $tests, $opt_parallel);

  # Send Ctrl-C to any children still running
  kill("INT", keys(%children));

  # Wait for childs to exit
  foreach my $pid (keys %children)
  {
    my $ret_pid= waitpid($pid, 0);
    if ($ret_pid != $pid){
      mtr_report("Unknown process $ret_pid exited");
    }
    else {
      delete $children{$ret_pid};
    }
  }

  if ( not defined @$completed ) {
    mtr_error("Test suite aborted");
  }

  if ( @$completed != $num_tests){

    if ($opt_force){
      # All test should have been run, print any that are still in $tests
      #foreach my $test ( @$tests ){
      #  $test->print_test();
      #}
    }

    # Not all tests completed, failure
    mtr_report();
    mtr_report("Only ", int(@$completed), " of $num_tests completed.");
    mtr_error("Not all tests completed");
  }

  mtr_print_line();

  if ( $opt_gcov ) {
    gcov_collect($basedir, $opt_gcov_exe,
		 $opt_gcov_msg, $opt_gcov_err);
  }

  mtr_report_stats($completed);

  exit(0);
}


sub run_test_server ($$$) {
  my ($server, $tests, $childs) = @_;

  my $num_saved_cores= 0;  # Number of core files saved in vardir/log/ so far.
  my $num_saved_datadir= 0;  # Number of datadirs saved in vardir/log/ so far.
  my $num_failed_test= 0; # Number of tests failed so far

  # Scheduler variables
  my $max_ndb= $childs / 2;
  $max_ndb = 4 if $max_ndb > 4;
  $max_ndb = 1 if $max_ndb < 1;
  my $num_ndb_tests= 0;

  my $completed= [];
  my %running;
  my $result;
  my $exe_mysqld= find_mysqld($basedir) || ""; # Used as hint to CoreDump

  my $suite_timeout_proc= My::SafeProcess->timer(suite_timeout());

  my $s= IO::Select->new();
  $s->add($server);
  while (1) {
    my @ready = $s->can_read(1); # Wake up once every second
    foreach my $sock (@ready) {
      if ($sock == $server) {
	# New client connected
	my $child= $sock->accept();
	mtr_verbose("Client connected");
	$s->add($child);
	print $child "HELLO\n";
      }
      else {
	my $line= <$sock>;
	if (!defined $line) {
	  # Client disconnected
	  mtr_verbose("Child closed socket");
	  $s->remove($sock);
	  if (--$childs == 0){
	    $suite_timeout_proc->kill();
	    return $completed;
	  }
	  next;
	}
	chomp($line);

	if ($line eq 'TESTRESULT'){
	  $result= My::Test::read_test($sock);
	  # $result->print_test();

	  # Report test status
	  mtr_report_test($result);

	  if ( $result->is_failed() ) {

	    # Save the workers "savedir" in var/log
	    my $worker_savedir= $result->{savedir};
	    my $worker_savename= basename($worker_savedir);
	    my $savedir= "$opt_vardir/log/$worker_savename";

	    if ($opt_max_save_datadir > 0 &&
		$num_saved_datadir >= $opt_max_save_datadir)
	    {
	      mtr_report(" - skipping '$worker_savedir/'");
	      rmtree($worker_savedir);
	    }
	    else {
	      mtr_report(" - saving '$worker_savedir/' to '$savedir/'");
	      rename($worker_savedir, $savedir);
	      # Move any core files from e.g. mysqltest
	      foreach my $coref (glob("core*"), glob("*.dmp"))
	      {
		mtr_report(" - found '$coref', moving it to '$savedir'");
                move($coref, $savedir);
              }
	      if ($opt_max_save_core > 0) {
		# Limit number of core files saved
		find({ no_chdir => 1,
		       wanted => sub {
			 my $core_file= $File::Find::name;
			 my $core_name= basename($core_file);

			 if ($core_name =~ /^core/ or  # Starting with core
			     (IS_WINDOWS and $core_name =~ /\.dmp$/)){
                                                       # Ending with .dmp
			   mtr_report(" - found '$core_name'",
				      "($num_saved_cores/$opt_max_save_core)");

			   My::CoreDump->show($core_file, $exe_mysqld);

			   if ($num_saved_cores >= $opt_max_save_core) {
			     mtr_report(" - deleting it, already saved",
					"$opt_max_save_core");
			     unlink("$core_file");
			   }
			   ++$num_saved_cores;
			 }
		       }
		     },
		     $savedir);
	      }
	    }
	    $num_saved_datadir++;
	    $num_failed_test++ unless ($result->{retries} ||
                                       $result->{exp_fail});

	    if ( !$opt_force ) {
	      # Test has failed, force is off
	      $suite_timeout_proc->kill();
	      push(@$completed, $result);
	      return $completed;
	    }
	    elsif ($opt_max_test_fail > 0 and
		   $num_failed_test >= $opt_max_test_fail) {
	      $suite_timeout_proc->kill();
	      push(@$completed, $result);
	      mtr_report_stats($completed, 1);
	      mtr_report("Too many tests($num_failed_test) failed!",
			 "Terminating...");
	      return undef;
	    }
	  }

	  # Retry test run after test failure
	  my $retries= $result->{retries} || 2;
	  my $test_has_failed= $result->{failures} || 0;
	  if ($test_has_failed and $retries <= $opt_retry){
	    # Test should be run one more time unless it has failed
	    # too many times already
	    my $failures= $result->{failures};
	    if ($opt_retry > 1 and $failures >= $opt_retry_failure){
	      mtr_report("\nTest has failed $failures times,",
			 "no more retries!\n");
	    }
	    else {
	      mtr_report("\nRetrying test, attempt($retries/$opt_retry)...\n");
	      delete($result->{result});
	      $result->{retries}= $retries+1;
	      $result->write_test($sock, 'TESTCASE');
	      next;
	    }
	  }

	  # Repeat test $opt_repeat number of times
	  my $repeat= $result->{repeat} || 1;
	  # Don't repeat if test was skipped
	  if ($repeat < $opt_repeat && $result->{'result'} ne 'MTR_RES_SKIPPED')
	  {
	    $result->{retries}= 0;
	    $result->{rep_failures}++ if $result->{failures};
	    $result->{failures}= 0;
	    delete($result->{result});
	    $result->{repeat}= $repeat+1;
	    $result->write_test($sock, 'TESTCASE');
	    next;
	  }

	  # Remove from list of running
	  mtr_error("'", $result->{name},"' is not known to be running")
	    unless delete $running{$result->key()};

	  # Update scheduler variables
	  $num_ndb_tests-- if ($result->{ndb_test});

	  # Save result in completed list
	  push(@$completed, $result);

	}
	elsif ($line eq 'START'){
	  ; # Send first test
	}
	else {
	  mtr_error("Unknown response: '$line' from client");
	}

	# Find next test to schedule
	# - Try to use same configuration as worker used last time
	# - Limit number of parallel ndb tests

	my $next;
	my $second_best;
	for(my $i= 0; $i <= @$tests; $i++)
	{
	  my $t= $tests->[$i];

	  last unless defined $t;

	  if (run_testcase_check_skip_test($t)){
	    # Move the test to completed list
	    #mtr_report("skip - Moving test $i to completed");
	    push(@$completed, splice(@$tests, $i, 1));

	    # Since the test at pos $i was taken away, next
	    # test will also be at $i -> redo
	    redo;
	  }

	  # Limit number of parallell NDB tests
	  if ($t->{ndb_test} and $num_ndb_tests >= $max_ndb){
	    #mtr_report("Skipping, num ndb is already at max, $num_ndb_tests");
	    next;
	  }

	  # Prefer same configuration
	  if (defined $result and
	      $result->{template_path} eq $t->{template_path})
	  {
	    #mtr_report("Test uses same config => good match");
	    # Test uses same config => good match
	    $next= splice(@$tests, $i, 1);
	    last;
	  }

	  # Second best choice is the first that does not fulfill
	  # any of the above conditions
	  if (!defined $second_best){
	    #mtr_report("Setting second_best to $i");
	    $second_best= $i;
	  }
	}

	# Use second best choice if no other test has been found
	if (!$next and defined $second_best){
	  #mtr_report("Take second best choice $second_best");
	  mtr_error("Internal error, second best too large($second_best)")
	    if $second_best >  $#$tests;
	  $next= splice(@$tests, $second_best, 1);
	}

	if ($next) {
	  #$next->print_test();
	  $next->write_test($sock, 'TESTCASE');
	  $running{$next->key()}= $next;
	  $num_ndb_tests++ if ($next->{ndb_test});
	}
	else {
	  # No more test, tell child to exit
	  #mtr_report("Saying BYE to child");
	  print $sock "BYE\n";
	}
      }
    }

    # ----------------------------------------------------
    # Check if test suite timer expired
    # ----------------------------------------------------
    if ( ! $suite_timeout_proc->wait_one(0) )
    {
      mtr_report_stats($completed, 1);
      mtr_report("Test suite timeout! Terminating...");
      return undef;
    }
  }
}


sub run_worker ($) {
  my ($server_port, $thread_num)= @_;

  $SIG{INT}= sub { exit(1); };

  # Connect to server
  my $server = new IO::Socket::INET
    (
     PeerAddr => 'localhost',
     PeerPort => $server_port,
     Proto    => 'tcp'
    );
  mtr_error("Could not connect to server at port $server_port: $!")
    unless $server;

  # --------------------------------------------------------------------------
  # Set worker name
  # --------------------------------------------------------------------------
  report_option('name',"worker[$thread_num]");

  # --------------------------------------------------------------------------
  # Set different ports per thread
  # --------------------------------------------------------------------------
  set_build_thread_ports($thread_num);

  # --------------------------------------------------------------------------
  # Turn off verbosity in workers, unless explicitly specified
  # --------------------------------------------------------------------------
  report_option('verbose', undef) if ($opt_verbose == 0);

  environment_setup();

  # Read hello from server which it will send when shared
  # resources have been setup
  my $hello= <$server>;

  setup_vardir();
  check_running_as_root();

  if ( using_extern() ) {
    create_config_file_for_extern(%opts_extern);
  }

  # Ask server for first test
  print $server "START\n";

  while(my $line= <$server>){
    chomp($line);
    if ($line eq 'TESTCASE'){
      my $test= My::Test::read_test($server);
      #$test->print_test();

      # Clear comment and logfile, to avoid
      # reusing them from previous test
      delete($test->{'comment'});
      delete($test->{'logfile'});

      $test->{worker} = $thread_num if $opt_parallel > 1;

      run_testcase($test);
      #$test->{result}= 'MTR_RES_PASSED';
      # Send it back, now with results set
      #$test->print_test();
      $test->write_test($server, 'TESTRESULT');
    }
    elsif ($line eq 'BYE'){
      mtr_report("Server said BYE");
      stop_all_servers($opt_shutdown_timeout);
      exit(0);
    }
    else {
      mtr_error("Could not understand server, '$line'");
    }
  }

  stop_all_servers();

  exit(1);
}


sub ignore_option {
  my ($opt, $value)= @_;
  mtr_report("Ignoring option '$opt'");
}



# Setup any paths that are $opt_vardir related
sub set_vardir {
  my ($vardir)= @_;

  $opt_vardir= $vardir;

  $path_vardir_trace= $opt_vardir;
  # Chop off any "c:", DBUG likes a unix path ex: c:/src/... => /src/...
  $path_vardir_trace=~ s/^\w://;

  # Location of my.cnf that all clients use
  $path_config_file= "$opt_vardir/my.cnf";

  $path_testlog=         "$opt_vardir/log/mysqltest.log";
  $path_current_testlog= "$opt_vardir/log/current_test";

}


sub command_line_setup {
  my $opt_comment;
  my $opt_usage;

  # Read the command line options
  # Note: Keep list, and the order, in sync with usage at end of this file
  Getopt::Long::Configure("pass_through");
  GetOptions(
             # Control what engine/variation to run
             'embedded-server'          => \$opt_embedded_server,
             'ps-protocol'              => \$opt_ps_protocol,
             'sp-protocol'              => \$opt_sp_protocol,
             'view-protocol'            => \$opt_view_protocol,
             'cursor-protocol'          => \$opt_cursor_protocol,
             'ssl|with-openssl'         => \$opt_ssl,
             'skip-ssl'                 => \$opt_skip_ssl,
             'compress'                 => \$opt_compress,
             'vs-config'                => \$opt_vs_config,

	     # Max number of parallel threads to use
	     'parallel=s'               => \$opt_parallel,

             # Config file to use as template for all tests
	     'defaults-file=s'          => \&collect_option,
	     # Extra config file to append to all generated configs
	     'defaults-extra-file=s'    => \&collect_option,

             # Control what test suites or cases to run
             'force'                    => \$opt_force,
             'with-ndbcluster-only'     => \&collect_option,
             'skip-ndbcluster|skip-ndb' => \$opt_skip_ndbcluster,
             'suite|suites=s'           => \$opt_suites,
             'skip-rpl'                 => \&collect_option,
             'skip-test=s'              => \&collect_option,
             'do-test=s'                => \&collect_option,
             'start-from=s'             => \&collect_option,
             'big-test'                 => \$opt_big_test,
	     'combination=s'            => \@opt_combinations,
             'skip-combinations'        => \&collect_option,
             'experimental=s'           => \$opt_experimental,
	     'skip-im'                  => \&ignore_option,

             # Specify ports
	     'build-thread|mtr-build-thread=i' => \$opt_build_thread,

             # Test case authoring
             'record'                   => \$opt_record,
             'check-testcases!'         => \$opt_check_testcases,
             'mark-progress'            => \$opt_mark_progress,

             # Extra options used when starting mysqld
             'mysqld=s'                 => \@opt_extra_mysqld_opt,

             # Run test on running server
             'extern=s'                  => \%opts_extern, # Append to hash

             # Debugging
             'debug'                    => \$opt_debug,
             'gdb'                      => \$opt_gdb,
             'client-gdb'               => \$opt_client_gdb,
             'manual-gdb'               => \$opt_manual_gdb,
             'manual-debug'             => \$opt_manual_debug,
             'ddd'                      => \$opt_ddd,
             'client-ddd'               => \$opt_client_ddd,
             'manual-ddd'               => \$opt_manual_ddd,
	     'debugger=s'               => \$opt_debugger,
	     'client-debugger=s'        => \$opt_client_debugger,
             'strace-client:s'          => \$opt_strace_client,
             'max-save-core=i'          => \$opt_max_save_core,
             'max-save-datadir=i'       => \$opt_max_save_datadir,
             'max-test-fail=i'          => \$opt_max_test_fail,

             # Coverage, profiling etc
             'gcov'                     => \$opt_gcov,
             'valgrind|valgrind-all'    => \$opt_valgrind,
             'valgrind-mysqltest'       => \$opt_valgrind_mysqltest,
             'valgrind-mysqld'          => \$opt_valgrind_mysqld,
             'valgrind-options=s'       => sub {
	       my ($opt, $value)= @_;
	       # Deprecated option unless it's what we know pushbuild uses
	       if ($value eq "--gen-suppressions=all --show-reachable=yes") {
		 push(@valgrind_args, $_) for (split(' ', $value));
		 return;
	       }
	       die("--valgrind-options=s is deprecated. Use ",
		   "--valgrind-option=s, to be specified several",
		   " times if necessary");
	     },
             'valgrind-option=s'        => \@valgrind_args,
             'valgrind-path=s'          => \$opt_valgrind_path,
	     'callgrind'                => \$opt_callgrind,

	     # Directories
             'tmpdir=s'                 => \$opt_tmpdir,
             'vardir=s'                 => \$opt_vardir,
             'mem'                      => \$opt_mem,
             'client-bindir=s'          => \$path_client_bindir,
             'client-libdir=s'          => \$path_client_libdir,

             # Misc
             'report-features'          => \$opt_report_features,
             'comment=s'                => \$opt_comment,
             'fast'                     => \$opt_fast,
             'reorder!'                 => \&collect_option,
             'enable-disabled'          => \&collect_option,
             'verbose+'                 => \$opt_verbose,
             'verbose-restart'          => \&report_option,
             'sleep=i'                  => \$opt_sleep,
             'start-dirty'              => \$opt_start_dirty,
             'start'                    => \$opt_start,
             'wait-all'                 => \$opt_wait_all,
	     'print-testcases'          => \&collect_option,
	     'repeat=i'                 => \$opt_repeat,
	     'retry=i'                  => \$opt_retry,
	     'retry-failure=i'          => \$opt_retry_failure,
             'timer!'                   => \&report_option,
             'user=s'                   => \$opt_user,
             'testcase-timeout=i'       => \$opt_testcase_timeout,
             'suite-timeout=i'          => \$opt_suite_timeout,
             'shutdown-timeout=i'       => \$opt_shutdown_timeout,
             'warnings!'                => \$opt_warnings,
	     'timestamp'                => \&report_option,
	     'timediff'                 => \&report_option,

             'help|h'                   => \$opt_usage,
            ) or usage("Can't read options");

  usage("") if $opt_usage;

  # --------------------------------------------------------------------------
  # Setup verbosity
  # --------------------------------------------------------------------------
  if ($opt_verbose != 0){
    report_option('verbose', $opt_verbose);
  }

  if ( -d "../sql" )
  {
    $source_dist=  1;
  }

  # Find the absolute path to the test directory
  $glob_mysql_test_dir= cwd();
  if (IS_CYGWIN)
  {
    # Use mixed path format i.e c:/path/to/
    $glob_mysql_test_dir= mixed_path($glob_mysql_test_dir);
  }

  # In most cases, the base directory we find everything relative to,
  # is the parent directory of the "mysql-test" directory. For source
  # distributions, TAR binary distributions and some other packages.
  $basedir= dirname($glob_mysql_test_dir);

  # In the RPM case, binaries and libraries are installed in the
  # default system locations, instead of having our own private base
  # directory. And we install "/usr/share/mysql-test". Moving up one
  # more directory relative to "mysql-test" gives us a usable base
  # directory for RPM installs.
  if ( ! $source_dist and ! -d "$basedir/bin" )
  {
    $basedir= dirname($basedir);
  }

  # Look for the client binaries directory
  if ($path_client_bindir)
  {
    # --client-bindir=path set on command line, check that the path exists
    $path_client_bindir= mtr_path_exists($path_client_bindir);
  }
  else
  {
    $path_client_bindir= mtr_path_exists("$basedir/client_release",
					 "$basedir/client_debug",
					 vs_config_dirs('client', ''),
					 "$basedir/client",
					 "$basedir/bin");
  }

  # Look for language files and charsetsdir, use same share
  $path_language=   mtr_path_exists("$basedir/share/mysql/english",
                                    "$basedir/sql/share/english",
                                    "$basedir/share/english");


  my $path_share= dirname($path_language);
  $path_charsetsdir=   mtr_path_exists("$path_share/charsets");

  if (using_extern())
  {
    # Connect to the running mysqld and find out what it supports
    collect_mysqld_features_from_running_server();
  }
  else
  {
    # Run the mysqld to find out what features are available
    collect_mysqld_features();
  }

  if ( $opt_comment )
  {
    mtr_report();
    mtr_print_thick_line('#');
    mtr_report("# $opt_comment");
    mtr_print_thick_line('#');
  }

  if ( $opt_experimental )
  {
    # $^O on Windows considered not generic enough
    my $plat= (IS_WINDOWS) ? 'windows' : $^O;

    # read the list of experimental test cases from the file specified on
    # the command line
    open(FILE, "<", $opt_experimental) or mtr_error("Can't read experimental file: $opt_experimental");
    mtr_report("Using experimental file: $opt_experimental");
    $experimental_test_cases = [];
    while(<FILE>) {
      chomp;
      # remove comments (# foo) at the beginning of the line, or after a 
      # blank at the end of the line
      s/( +|^)#.*$//;
      # If @ platform specifier given, use this entry only if it contains
      # @<platform> or @!<xxx> where xxx != platform
      if (/\@.*/)
      {
	next if (/\@!$plat/);
	next unless (/\@$plat/ or /\@!/);
	# Then remove @ and everything after it
	s/\@.*$//;
      }
      # remove whitespace
      s/^ +//;              
      s/ +$//;
      # if nothing left, don't need to remember this line
      if ( $_ eq "" ) {
        next;
      }
      # remember what is left as the name of another test case that should be
      # treated as experimental
      print " - $_\n";
      push @$experimental_test_cases, $_;
    }
    close FILE;
  }

  foreach my $arg ( @ARGV )
  {
    if ( $arg =~ /^--skip-/ )
    {
      push(@opt_extra_mysqld_opt, $arg);
    }
    elsif ( $arg =~ /^--$/ )
    {
      # It is an effect of setting 'pass_through' in option processing
      # that the lone '--' separating options from arguments survives,
      # simply ignore it.
    }
    elsif ( $arg =~ /^-/ )
    {
      usage("Invalid option \"$arg\"");
    }
    else
    {
      push(@opt_cases, $arg);
    }
  }

  # --------------------------------------------------------------------------
  # Find out type of logging that are being used
  # --------------------------------------------------------------------------
  foreach my $arg ( @opt_extra_mysqld_opt )
  {
    if ( $arg =~ /binlog[-_]format=(\S+)/ )
    {
      # Save this for collect phase
      collect_option('binlog-format', $1);
      mtr_report("Using binlog format '$1'");
    }
  }


  # --------------------------------------------------------------------------
  # Find out default storage engine being used(if any)
  # --------------------------------------------------------------------------
  foreach my $arg ( @opt_extra_mysqld_opt )
  {
    if ( $arg =~ /default-storage-engine=(\S+)/ )
    {
      # Save this for collect phase
      collect_option('default-storage-engine', $1);
      mtr_report("Using default engine '$1'")
    }
  }

  # --------------------------------------------------------------------------
  # Check if we should speed up tests by trying to run on tmpfs
  # --------------------------------------------------------------------------
  if ( defined $opt_mem)
  {
    mtr_error("Can't use --mem and --vardir at the same time ")
      if $opt_vardir;
    mtr_error("Can't use --mem and --tmpdir at the same time ")
      if $opt_tmpdir;

    # Search through list of locations that are known
    # to be "fast disks" to find a suitable location
    # Use --mem=<dir> as first location to look.
    my @tmpfs_locations= ($opt_mem, "/dev/shm", "/tmp");

    foreach my $fs (@tmpfs_locations)
    {
      if ( -d $fs )
      {
	my $template= "var_${opt_build_thread}_XXXX";
	$opt_mem= tempdir( $template, DIR => $fs, CLEANUP => 0);
	last;
      }
    }
  }

  # --------------------------------------------------------------------------
  # Set the "var/" directory, the base for everything else
  # --------------------------------------------------------------------------
  $default_vardir= "$glob_mysql_test_dir/var";
  if ( ! $opt_vardir )
  {
    $opt_vardir= $default_vardir;
  }

  # We make the path absolute, as the server will do a chdir() before usage
  unless ( $opt_vardir =~ m,^/, or
           (IS_WINDOWS and $opt_vardir =~ m,^[a-z]:/,i) )
  {
    # Make absolute path, relative test dir
    $opt_vardir= "$glob_mysql_test_dir/$opt_vardir";
  }

  set_vardir($opt_vardir);

  # --------------------------------------------------------------------------
  # Set the "tmp" directory
  # --------------------------------------------------------------------------
  if ( ! $opt_tmpdir )
  {
    $opt_tmpdir=       "$opt_vardir/tmp" unless $opt_tmpdir;

    if (check_socket_path_length("$opt_tmpdir/mysql_testsocket.sock"))
    {
      mtr_report("Too long tmpdir path '$opt_tmpdir'",
		 " creating a shorter one...");

      # Create temporary directory in standard location for temporary files
      $opt_tmpdir= tempdir( TMPDIR => 1, CLEANUP => 0 );
      mtr_report(" - using tmpdir: '$opt_tmpdir'\n");

      # Remember pid that created dir so it's removed by correct process
      $opt_tmpdir_pid= $$;
    }
  }
  $opt_tmpdir =~ s,/+$,,;       # Remove ending slash if any

  # --------------------------------------------------------------------------
  # fast option
  # --------------------------------------------------------------------------
  if ($opt_fast){
    $opt_shutdown_timeout= 0; # Kill processes instead of nice shutdown
  }

  # --------------------------------------------------------------------------
  # Check parallel value
  # --------------------------------------------------------------------------
  if ($opt_parallel ne "auto" && $opt_parallel < 1)
  {
    mtr_error("0 or negative parallel value makes no sense, use 'auto' or positive number");
  }

  # --------------------------------------------------------------------------
  # Record flag
  # --------------------------------------------------------------------------
  if ( $opt_record and ! @opt_cases )
  {
    mtr_error("Will not run in record mode without a specific test case");
  }

  if ( $opt_record ) {
    # Use only one worker with --record
    $opt_parallel= 1;
  }

  # --------------------------------------------------------------------------
  # Embedded server flag
  # --------------------------------------------------------------------------
  if ( $opt_embedded_server )
  {
    if ( IS_WINDOWS )
    {
      # Add the location for libmysqld.dll to the path.
      my $separator= ";";
      my $lib_mysqld=
        mtr_path_exists(vs_config_dirs('libmysqld',''));
      if ( IS_CYGWIN )
      {
	$lib_mysqld= posix_path($lib_mysqld);
	$separator= ":";
      }
      $ENV{'PATH'}= "$ENV{'PATH'}".$separator.$lib_mysqld;
    }
    $opt_skip_ndbcluster= 1;       # Turn off use of NDB cluster
    $opt_skip_ssl= 1;              # Turn off use of SSL

    # Turn off use of bin log
    push(@opt_extra_mysqld_opt, "--skip-log-bin");

    if ( using_extern() )
    {
      mtr_error("Can't use --extern with --embedded-server");
    }


    if ($opt_gdb)
    {
      mtr_warning("Silently converting --gdb to --client-gdb in embedded mode");
      $opt_client_gdb= $opt_gdb;
      $opt_gdb= undef;
    }

    if ($opt_ddd)
    {
      mtr_warning("Silently converting --ddd to --client-ddd in embedded mode");
      $opt_client_ddd= $opt_ddd;
      $opt_ddd= undef;
    }

    if ($opt_debugger)
    {
      mtr_warning("Silently converting --debugger to --client-debugger in embedded mode");
      $opt_client_debugger= $opt_debugger;
      $opt_debugger= undef;
    }

    if ( $opt_gdb || $opt_ddd || $opt_manual_gdb || $opt_manual_ddd ||
	 $opt_manual_debug || $opt_debugger )
    {
      mtr_error("You need to use the client debug options for the",
		"embedded server. Ex: --client-gdb");
    }
  }

  # --------------------------------------------------------------------------
  # Big test flags
  # --------------------------------------------------------------------------
   if ( $opt_big_test )
   {
     $ENV{'BIG_TEST'}= 1;
   }

  # --------------------------------------------------------------------------
  # Gcov flag
  # --------------------------------------------------------------------------
  if ( $opt_gcov and ! $source_dist )
  {
    mtr_error("Coverage test needs the source - please use source dist");
  }

  # --------------------------------------------------------------------------
  # Check debug related options
  # --------------------------------------------------------------------------
  if ( $opt_gdb || $opt_client_gdb || $opt_ddd || $opt_client_ddd ||
       $opt_manual_gdb || $opt_manual_ddd || $opt_manual_debug ||
       $opt_debugger || $opt_client_debugger )
  {
    # Indicate that we are using debugger
    $glob_debugger= 1;
    if ( using_extern() )
    {
      mtr_error("Can't use --extern when using debugger");
    }
    # Set one week timeout (check-testcase timeout will be 1/10th)
    $opt_testcase_timeout= 7 * 24 * 60;
    $opt_suite_timeout= 7 * 24 * 60;
    # One day to shutdown
    $opt_shutdown_timeout= 24 * 60;
    # One day for PID file creation (this is given in seconds not minutes)
    $opt_start_timeout= 24 * 60 * 60;
  }

  # --------------------------------------------------------------------------
  # Modified behavior with --start options
  # --------------------------------------------------------------------------
  if ($opt_start or $opt_start_dirty) {
    collect_option ('quick-collect', 1);
    $start_only= 1;
  }

  # --------------------------------------------------------------------------
  # Check use of wait-all
  # --------------------------------------------------------------------------

  if ($opt_wait_all && ! $start_only)
  {
    mtr_error("--wait-all can only be used with --start or --start-dirty");
  }

  # --------------------------------------------------------------------------
  # Check timeout arguments
  # --------------------------------------------------------------------------

  mtr_error("Invalid value '$opt_testcase_timeout' supplied ".
	    "for option --testcase-timeout")
    if ($opt_testcase_timeout <= 0);
  mtr_error("Invalid value '$opt_suite_timeout' supplied ".
	    "for option --testsuite-timeout")
    if ($opt_suite_timeout <= 0);

  # --------------------------------------------------------------------------
  # Check valgrind arguments
  # --------------------------------------------------------------------------
  if ( $opt_valgrind or $opt_valgrind_path or @valgrind_args)
  {
    mtr_report("Turning on valgrind for all executables");
    $opt_valgrind= 1;
    $opt_valgrind_mysqld= 1;
    $opt_valgrind_mysqltest= 1;

    # Increase the timeouts when running with valgrind
    $opt_testcase_timeout*= 10;
    $opt_suite_timeout*= 6;
    $opt_start_timeout*= 10;

  }
  elsif ( $opt_valgrind_mysqld )
  {
    mtr_report("Turning on valgrind for mysqld(s) only");
    $opt_valgrind= 1;
  }
  elsif ( $opt_valgrind_mysqltest )
  {
    mtr_report("Turning on valgrind for mysqltest and mysql_client_test only");
    $opt_valgrind= 1;
  }

  if ( $opt_callgrind )
  {
    mtr_report("Turning on valgrind with callgrind for mysqld(s)");
    $opt_valgrind= 1;
    $opt_valgrind_mysqld= 1;

    # Set special valgrind options unless options passed on command line
    push(@valgrind_args, "--trace-children=yes")
      unless @valgrind_args;
  }

  if ( $opt_valgrind )
  {
    # Set valgrind_options to default unless already defined
    push(@valgrind_args, @default_valgrind_args)
      unless @valgrind_args;

    # Make valgrind run in quiet mode so it only print errors
    push(@valgrind_args, "--quiet" );

    mtr_report("Running valgrind with options \"",
	       join(" ", @valgrind_args), "\"");
  }

  mtr_report("Checking supported features...");

  check_ndbcluster_support(\%mysqld_variables);
  check_ssl_support(\%mysqld_variables);
  check_debug_support(\%mysqld_variables);

  executable_setup();

}


#
# To make it easier for different devs to work on the same host,
# an environment variable can be used to control all ports. A small
# number is to be used, 0 - 16 or similar.
#
# Note the MASTER_MYPORT has to be set the same in all 4.x and 5.x
# versions of this script, else a 4.0 test run might conflict with a
# 5.1 test run, even if different MTR_BUILD_THREAD is used. This means
# all port numbers might not be used in this version of the script.
#
# Also note the limitation of ports we are allowed to hand out. This
# differs between operating systems and configuration, see
# http://www.ncftp.com/ncftpd/doc/misc/ephemeral_ports.html
# But a fairly safe range seems to be 5001 - 32767
#
sub set_build_thread_ports($) {
  my $thread= shift || 0;

  if ( lc($opt_build_thread) eq 'auto' ) {
    my $found_free = 0;
    $build_thread = 300;	# Start attempts from here
    while (! $found_free)
    {
      $build_thread= mtr_get_unique_id($build_thread, 349);
      if ( !defined $build_thread ) {
        mtr_error("Could not get a unique build thread id");
      }
      $found_free= check_ports_free($build_thread);
      # If not free, release and try from next number
      if (! $found_free) {
        mtr_release_unique_id();
        $build_thread++;
      }
    }
  }
  else
  {
    $build_thread = $opt_build_thread + $thread - 1;
    if (! check_ports_free($build_thread)) {
      # Some port was not free(which one has already been printed)
      mtr_error("Some port(s) was not free")
    }
  }
  $ENV{MTR_BUILD_THREAD}= $build_thread;

  # Calculate baseport
  $baseport= $build_thread * 10 + 10000;
  if ( $baseport < 5001 or $baseport + 9 >= 32767 )
  {
    mtr_error("MTR_BUILD_THREAD number results in a port",
              "outside 5001 - 32767",
              "($baseport - $baseport + 9)");
  }

  mtr_report("Using MTR_BUILD_THREAD $build_thread,",
	     "with reserved ports $baseport..".($baseport+9));

}


sub collect_mysqld_features {
  my $found_variable_list_start= 0;
  my $use_tmpdir;
  if ( defined $opt_tmpdir and -d $opt_tmpdir){
    # Create the tempdir in $opt_tmpdir
    $use_tmpdir= $opt_tmpdir;
  }
  my $tmpdir= tempdir(CLEANUP => 0, # Directory removed by this function
		      DIR => $use_tmpdir);

  #
  # Execute "mysqld --no-defaults --help --verbose" to get a
  # list of all features and settings
  #
  # --no-defaults and --skip-grant-tables are to avoid loading
  # system-wide configs and plugins
  #
  # --datadir must exist, mysqld will chdir into it
  #
  my $args;
  mtr_init_args(\$args);
  mtr_add_arg($args, "--no-defaults");
  mtr_add_arg($args, "--datadir=%s", mixed_path($tmpdir));
  mtr_add_arg($args, "--language=%s", $path_language);
  mtr_add_arg($args, "--skip-grant-tables");
  mtr_add_arg($args, "--verbose");
  mtr_add_arg($args, "--help");

  my $exe_mysqld= find_mysqld($basedir);
  my $cmd= join(" ", $exe_mysqld, @$args);
  my $list= `$cmd`;

  foreach my $line (split('\n', $list))
  {
    # First look for version
    if ( !$mysql_version_id )
    {
      # Look for version
      my $exe_name= basename($exe_mysqld);
      mtr_verbose("exe_name: $exe_name");
      if ( $line =~ /^\S*$exe_name\s\sVer\s([0-9]*)\.([0-9]*)\.([0-9]*)/ )
      {
	#print "Major: $1 Minor: $2 Build: $3\n";
	$mysql_version_id= $1*10000 + $2*100 + $3;
	#print "mysql_version_id: $mysql_version_id\n";
	mtr_report("MySQL Version $1.$2.$3");
      }
    }
    else
    {
      if (!$found_variable_list_start)
      {
	# Look for start of variables list
	if ( $line =~ /[\-]+\s[\-]+/ )
	{
	  $found_variable_list_start= 1;
	}
      }
      else
      {
	# Put variables into hash
	if ( $line =~ /^([\S]+)[ \t]+(.*?)\r?$/ )
	{
	  # print "$1=\"$2\"\n";
	  $mysqld_variables{$1}= $2;
	}
	else
	{
	  # The variable list is ended with a blank line
	  if ( $line =~ /^[\s]*$/ )
	  {
	    last;
	  }
	  else
	  {
	    # Send out a warning, we should fix the variables that has no
	    # space between variable name and it's value
	    # or should it be fixed width column parsing? It does not
	    # look like that in function my_print_variables in my_getopt.c
	    mtr_warning("Could not parse variable list line : $line");
	  }
	}
      }
    }
  }
  rmtree($tmpdir);
  mtr_error("Could not find version of MySQL") unless $mysql_version_id;
  mtr_error("Could not find variabes list") unless $found_variable_list_start;

}



sub collect_mysqld_features_from_running_server ()
{
  my $mysql= mtr_exe_exists("$path_client_bindir/mysql");

  my $args;
  mtr_init_args(\$args);

  mtr_add_arg($args, "--no-defaults");
  mtr_add_arg($args, "--user=%s", $opt_user);

  while (my ($option, $value)= each( %opts_extern )) {
    mtr_add_arg($args, "--$option=$value");
  }

  mtr_add_arg($args, "--silent"); # Tab separated output
  mtr_add_arg($args, "-e '%s'", "use mysql; SHOW VARIABLES");
  my $cmd= "$mysql " . join(' ', @$args);
  mtr_verbose("cmd: $cmd");

  my $list = `$cmd` or
    mtr_error("Could not connect to extern server using command: '$cmd'");
  foreach my $line (split('\n', $list ))
  {
    # Put variables into hash
    if ( $line =~ /^([\S]+)[ \t]+(.*?)\r?$/ )
    {
      # print "$1=\"$2\"\n";
      $mysqld_variables{$1}= $2;
    }
  }

  # "Convert" innodb flag
  $mysqld_variables{'innodb'}= "ON"
    if ($mysqld_variables{'have_innodb'} eq "YES");

  # Parse version
  my $version_str= $mysqld_variables{'version'};
  if ( $version_str =~ /^([0-9]*)\.([0-9]*)\.([0-9]*)/ )
  {
    #print "Major: $1 Minor: $2 Build: $3\n";
    $mysql_version_id= $1*10000 + $2*100 + $3;
    #print "mysql_version_id: $mysql_version_id\n";
    mtr_report("MySQL Version $1.$2.$3");
  }
  mtr_error("Could not find version of MySQL") unless $mysql_version_id;
}

sub find_mysqld {
  my ($mysqld_basedir)= @_;

  my @mysqld_names= ("mysqld", "mysqld-max-nt", "mysqld-max",
		     "mysqld-nt");

  if ( $opt_debug ){
    # Put mysqld-debug first in the list of binaries to look for
    mtr_verbose("Adding mysqld-debug first in list of binaries to look for");
    unshift(@mysqld_names, "mysqld-debug");
  }

  return my_find_bin($mysqld_basedir,
		     ["sql", "libexec", "sbin", "bin"],
		     [@mysqld_names]);
}


sub executable_setup () {

  #
  # Check if libtool is available in this distribution/clone
  # we need it when valgrinding or debugging non installed binary
  # Otherwise valgrind will valgrind the libtool wrapper or bash
  # and gdb will not find the real executable to debug
  #
  if ( -x "../libtool")
  {
    $exe_libtool= "../libtool";
    if ($opt_valgrind or $glob_debugger)
    {
      mtr_report("Using \"$exe_libtool\" when running valgrind or debugger");
    }
  }

  # Look for the client binaries
  $exe_mysqladmin=     mtr_exe_exists("$path_client_bindir/mysqladmin");
  $exe_mysql=          mtr_exe_exists("$path_client_bindir/mysql");

  if ( ! $opt_skip_ndbcluster )
  {
    $exe_ndbd=
      my_find_bin($basedir,
		  ["storage/ndb/src/kernel", "libexec", "sbin", "bin"],
		  "ndbd");

    $exe_ndb_mgmd=
      my_find_bin($basedir,
		  ["storage/ndb/src/mgmsrv", "libexec", "sbin", "bin"],
		  "ndb_mgmd");

    $exe_ndb_waiter=
      my_find_bin($basedir,
		  ["storage/ndb/tools/", "bin"],
		  "ndb_waiter");

  }

  # Look for mysqltest executable
  if ( $opt_embedded_server )
  {
    $exe_mysqltest=
      mtr_exe_exists(vs_config_dirs('libmysqld/examples','mysqltest_embedded'),
                     "$basedir/libmysqld/examples/mysqltest_embedded",
                     "$path_client_bindir/mysqltest_embedded");
  }
  else
  {
    $exe_mysqltest= mtr_exe_exists("$path_client_bindir/mysqltest");
  }

}


sub client_debug_arg($$) {
  my ($args, $client_name)= @_;

  if ( $opt_debug ) {
    mtr_add_arg($args,
		"--debug=d:t:A,%s/log/%s.trace",
		$path_vardir_trace, $client_name)
  }
}


sub mysql_fix_arguments () {

  return "" if ( IS_WINDOWS );

  my $exe=
    mtr_script_exists("$basedir/scripts/mysql_fix_privilege_tables",
		      "$path_client_bindir/mysql_fix_privilege_tables");
  my $args;
  mtr_init_args(\$args);
  mtr_add_arg($args, "--defaults-file=%s", $path_config_file);

  mtr_add_arg($args, "--basedir=%s", $basedir);
  mtr_add_arg($args, "--bindir=%s", $path_client_bindir);
  mtr_add_arg($args, "--verbose");
  return mtr_args2str($exe, @$args);
}


sub client_arguments ($;$) {
  my $client_name= shift;
  my $group_suffix= shift;
  my $client_exe= mtr_exe_exists("$path_client_bindir/$client_name");

  my $args;
  mtr_init_args(\$args);
  mtr_add_arg($args, "--defaults-file=%s", $path_config_file);
  if (defined($group_suffix)) {
    mtr_add_arg($args, "--defaults-group-suffix=%s", $group_suffix);
    client_debug_arg($args, "$client_name-$group_suffix");
  }
  else
  {
    client_debug_arg($args, $client_name);
  }
  return mtr_args2str($client_exe, @$args);
}


sub mysqlslap_arguments () {
  my $exe= mtr_exe_maybe_exists("$path_client_bindir/mysqlslap");
  if ( $exe eq "" ) {
    # mysqlap was not found

    if (defined $mysql_version_id and $mysql_version_id >= 50100 ) {
      mtr_error("Could not find the mysqlslap binary");
    }
    return ""; # Don't care about mysqlslap
  }

  my $args;
  mtr_init_args(\$args);
  mtr_add_arg($args, "--defaults-file=%s", $path_config_file);
  client_debug_arg($args, "mysqlslap");
  return mtr_args2str($exe, @$args);
}


sub mysqldump_arguments ($) {
  my($group_suffix) = @_;
  my $exe= mtr_exe_exists("$path_client_bindir/mysqldump");

  my $args;
  mtr_init_args(\$args);
  mtr_add_arg($args, "--defaults-file=%s", $path_config_file);
  mtr_add_arg($args, "--defaults-group-suffix=%s", $group_suffix);
  client_debug_arg($args, "mysqldump-$group_suffix");
  return mtr_args2str($exe, @$args);
}


sub mysql_client_test_arguments(){
  my $exe;
  # mysql_client_test executable may _not_ exist
  if ( $opt_embedded_server ) {
    $exe= mtr_exe_maybe_exists(
            vs_config_dirs('libmysqld/examples','mysql_client_test_embedded'),
	      "$basedir/libmysqld/examples/mysql_client_test_embedded",
		"$basedir/bin/mysql_client_test_embedded");
  } else {
    $exe= mtr_exe_maybe_exists(vs_config_dirs('tests', 'mysql_client_test'),
			       "$basedir/tests/mysql_client_test",
			       "$basedir/bin/mysql_client_test");
  }

  my $args;
  mtr_init_args(\$args);
  if ( $opt_valgrind_mysqltest ) {
    valgrind_arguments($args, \$exe);
  }
  mtr_add_arg($args, "--defaults-file=%s", $path_config_file);
  mtr_add_arg($args, "--testcase");
  mtr_add_arg($args, "--vardir=$opt_vardir");
  client_debug_arg($args,"mysql_client_test");

  return mtr_args2str($exe, @$args);
}


#
# Set environment to be used by childs of this process for
# things that are constant during the whole lifetime of mysql-test-run
#
sub environment_setup {

  umask(022);

  my @ld_library_paths;

  if ($path_client_libdir)
  {
    # Use the --client-libdir passed on commandline
    push(@ld_library_paths, "$path_client_libdir");
  }
  else
  {
    # Setup LD_LIBRARY_PATH so the libraries from this distro/clone
    # are used in favor of the system installed ones
    if ( $source_dist )
    {
      push(@ld_library_paths, "$basedir/libmysql/.libs/",
	   "$basedir/libmysql_r/.libs/",
	   "$basedir/zlib.libs/");
    }
    else
    {
      push(@ld_library_paths, "$basedir/lib");
    }
  }

  # --------------------------------------------------------------------------
  # Add the path where libndbclient can be found
  # --------------------------------------------------------------------------
  if ( !$opt_skip_ndbcluster )
  {
    push(@ld_library_paths,  "$basedir/storage/ndb/src/.libs");
  }

  # --------------------------------------------------------------------------
  # Add the path where mysqld will find udf_example.so
  # --------------------------------------------------------------------------
  my $lib_udf_example=
    mtr_file_exists(vs_config_dirs('sql', 'udf_example.dll'),
		    "$basedir/sql/.libs/udf_example.so",);

  if ( $lib_udf_example )
  {
    push(@ld_library_paths, dirname($lib_udf_example));
  }

  $ENV{'UDF_EXAMPLE_LIB'}=
    ($lib_udf_example ? basename($lib_udf_example) : "");
  $ENV{'UDF_EXAMPLE_LIB_OPT'}= "--plugin-dir=".
    ($lib_udf_example ? dirname($lib_udf_example) : "");

  # --------------------------------------------------------------------------
  # Add the path where mysqld will find ha_example.so
  # --------------------------------------------------------------------------
  if ($mysql_version_id >= 50100) {
    my $plugin_filename;
    if (IS_WINDOWS)
    {
       $plugin_filename = "ha_example.dll";
    }
    else 
    {
       $plugin_filename = "ha_example.so";
    }
    my $lib_example_plugin=
      mtr_file_exists(vs_config_dirs('storage/example',$plugin_filename),
		      "$basedir/storage/example/.libs/".$plugin_filename,
                      "$basedir/lib/mysql/plugin/".$plugin_filename);
    $ENV{'EXAMPLE_PLUGIN'}=
      ($lib_example_plugin ? basename($lib_example_plugin) : "");
    $ENV{'EXAMPLE_PLUGIN_OPT'}= "--plugin-dir=".
      ($lib_example_plugin ? dirname($lib_example_plugin) : "");

    $ENV{'HA_EXAMPLE_SO'}="'".$plugin_filename."'";
    $ENV{'EXAMPLE_PLUGIN_LOAD'}="--plugin_load=;EXAMPLE=".$plugin_filename.";";
  }
  else
  {
    # Some ".opt" files use some of these variables, so they must be defined
    $ENV{'EXAMPLE_PLUGIN'}= "";
    $ENV{'EXAMPLE_PLUGIN_OPT'}= "";
    $ENV{'HA_EXAMPLE_SO'}= "";
    $ENV{'EXAMPLE_PLUGIN_LOAD'}= "";
  }

  # ----------------------------------------------------
  # Add the path where mysqld will find mypluglib.so
  # ----------------------------------------------------
  my $lib_simple_parser=
    mtr_file_exists(vs_config_dirs('plugin/fulltext', 'mypluglib.dll'),
		    "$basedir/plugin/fulltext/.libs/mypluglib.so",);

  $ENV{'SIMPLE_PARSER'}=
    ($lib_simple_parser ? basename($lib_simple_parser) : "");
  $ENV{'SIMPLE_PARSER_OPT'}= "--plugin-dir=".
    ($lib_simple_parser ? dirname($lib_simple_parser) : "");

  # --------------------------------------------------------------------------
  # Valgrind need to be run with debug libraries otherwise it's almost
  # impossible to add correct supressions, that means if "/usr/lib/debug"
  # is available, it should be added to
  # LD_LIBRARY_PATH
  #
  # But pthread is broken in libc6-dbg on Debian <= 3.1 (see Debian
  # bug 399035, http://bugs.debian.org/cgi-bin/bugreport.cgi?bug=399035),
  # so don't change LD_LIBRARY_PATH on that platform.
  # --------------------------------------------------------------------------
  my $debug_libraries_path= "/usr/lib/debug";
  my $deb_version;
  if (  $opt_valgrind and -d $debug_libraries_path and
        (! -e '/etc/debian_version' or
	 ($deb_version=
	    mtr_grab_file('/etc/debian_version')) !~ /^[0-9]+\.[0-9]$/ or
         $deb_version > 3.1 ) )
  {
    push(@ld_library_paths, $debug_libraries_path);
  }

  $ENV{'LD_LIBRARY_PATH'}= join(":", @ld_library_paths,
				$ENV{'LD_LIBRARY_PATH'} ?
				split(':', $ENV{'LD_LIBRARY_PATH'}) : ());
  mtr_debug("LD_LIBRARY_PATH: $ENV{'LD_LIBRARY_PATH'}");

  $ENV{'DYLD_LIBRARY_PATH'}= join(":", @ld_library_paths,
				  $ENV{'DYLD_LIBRARY_PATH'} ?
				  split(':', $ENV{'DYLD_LIBRARY_PATH'}) : ());
  mtr_debug("DYLD_LIBRARY_PATH: $ENV{'DYLD_LIBRARY_PATH'}");

  # The environment variable used for shared libs on AIX
  $ENV{'SHLIB_PATH'}= join(":", @ld_library_paths,
                           $ENV{'SHLIB_PATH'} ?
                           split(':', $ENV{'SHLIB_PATH'}) : ());
  mtr_debug("SHLIB_PATH: $ENV{'SHLIB_PATH'}");

  # The environment variable used for shared libs on hp-ux
  $ENV{'LIBPATH'}= join(":", @ld_library_paths,
                        $ENV{'LIBPATH'} ?
                        split(':', $ENV{'LIBPATH'}) : ());
  mtr_debug("LIBPATH: $ENV{'LIBPATH'}");

  $ENV{'CHARSETSDIR'}=              $path_charsetsdir;
  $ENV{'UMASK'}=              "0660"; # The octal *string*
  $ENV{'UMASK_DIR'}=          "0770"; # The octal *string*

  #
  # MySQL tests can produce output in various character sets
  # (especially, ctype_xxx.test). To avoid confusing Perl
  # with output which is incompatible with the current locale
  # settings, we reset the current values of LC_ALL and LC_CTYPE to "C".
  # For details, please see
  # Bug#27636 tests fails if LC_* variables set to *_*.UTF-8
  #
  $ENV{'LC_ALL'}=             "C";
  $ENV{'LC_CTYPE'}=           "C";

  $ENV{'LC_COLLATE'}=         "C";
  $ENV{'USE_RUNNING_SERVER'}= using_extern();
  $ENV{'MYSQL_TEST_DIR'}=     $glob_mysql_test_dir;
  $ENV{'DEFAULT_MASTER_PORT'}= $mysqld_variables{'master-port'} || 3306;
  $ENV{'MYSQL_TMP_DIR'}=      $opt_tmpdir;
  $ENV{'MYSQLTEST_VARDIR'}=   $opt_vardir;

  # ----------------------------------------------------
  # Setup env for NDB
  # ----------------------------------------------------
  if ( ! $opt_skip_ndbcluster )
  {
    $ENV{'NDB_MGM'}=
      my_find_bin($basedir,
		  ["storage/ndb/src/mgmclient", "bin"],
		  "ndb_mgm");

    $ENV{'NDB_TOOLS_DIR'}=
      my_find_dir($basedir,
		  ["storage/ndb/tools", "bin"]);

    $ENV{'NDB_EXAMPLES_DIR'}=
      my_find_dir($basedir,
		  ["storage/ndb/ndbapi-examples", "bin"]);

    $ENV{'NDB_EXAMPLES_BINARY'}=
      my_find_bin($basedir,
		  ["storage/ndb/ndbapi-examples/ndbapi_simple", "bin"],
		  "ndbapi_simple", NOT_REQUIRED);

    my $path_ndb_testrun_log= "$opt_vardir/log/ndb_testrun.log";
    $ENV{'NDB_TOOLS_OUTPUT'}=         $path_ndb_testrun_log;
    $ENV{'NDB_EXAMPLES_OUTPUT'}=      $path_ndb_testrun_log;
  }

  # ----------------------------------------------------
  # mysql clients
  # ----------------------------------------------------
  $ENV{'MYSQL_CHECK'}=              client_arguments("mysqlcheck");
  $ENV{'MYSQL_DUMP'}=               mysqldump_arguments(".1");
  $ENV{'MYSQL_DUMP_SLAVE'}=         mysqldump_arguments(".2");
  $ENV{'MYSQL_SLAP'}=               mysqlslap_arguments();
  $ENV{'MYSQL_IMPORT'}=             client_arguments("mysqlimport");
  $ENV{'MYSQL_SHOW'}=               client_arguments("mysqlshow");
  $ENV{'MYSQL_BINLOG'}=             client_arguments("mysqlbinlog");
  $ENV{'MYSQL'}=                    client_arguments("mysql");
  $ENV{'MYSQL_SLAVE'}=              client_arguments("mysql", ".2");
  $ENV{'MYSQL_UPGRADE'}=            client_arguments("mysql_upgrade");
  $ENV{'MYSQLADMIN'}=               native_path($exe_mysqladmin);
  $ENV{'MYSQL_CLIENT_TEST'}=        mysql_client_test_arguments();
  $ENV{'MYSQL_FIX_SYSTEM_TABLES'}=  mysql_fix_arguments();
  $ENV{'EXE_MYSQL'}=                $exe_mysql;

  # ----------------------------------------------------
  # bug25714 executable may _not_ exist in
  # some versions, test using it should be skipped
  # ----------------------------------------------------
  my $exe_bug25714=
      mtr_exe_maybe_exists(vs_config_dirs('tests', 'bug25714'),
                           "$basedir/tests/bug25714");
  $ENV{'MYSQL_BUG25714'}=  native_path($exe_bug25714);

  # ----------------------------------------------------
  # mysql_fix_privilege_tables.sql
  # ----------------------------------------------------
  my $file_mysql_fix_privilege_tables=
    mtr_file_exists("$basedir/scripts/mysql_fix_privilege_tables.sql",
		    "$basedir/share/mysql_fix_privilege_tables.sql",
		    "$basedir/share/mysql/mysql_fix_privilege_tables.sql");
  $ENV{'MYSQL_FIX_PRIVILEGE_TABLES'}=  $file_mysql_fix_privilege_tables;

  # ----------------------------------------------------
  # my_print_defaults
  # ----------------------------------------------------
  my $exe_my_print_defaults=
    mtr_exe_exists(vs_config_dirs('extra', 'my_print_defaults'),
		   "$path_client_bindir/my_print_defaults",
		   "$basedir/extra/my_print_defaults");
  $ENV{'MYSQL_MY_PRINT_DEFAULTS'}= native_path($exe_my_print_defaults);

  # ----------------------------------------------------
  # Setup env so childs can execute myisampack and myisamchk
  # ----------------------------------------------------
  $ENV{'MYISAMCHK'}= native_path(mtr_exe_exists(
                       vs_config_dirs('storage/myisam', 'myisamchk'),
                       vs_config_dirs('myisam', 'myisamchk'),
                       "$path_client_bindir/myisamchk",
                       "$basedir/storage/myisam/myisamchk",
                       "$basedir/myisam/myisamchk"));
  $ENV{'MYISAMPACK'}= native_path(mtr_exe_exists(
                        vs_config_dirs('storage/myisam', 'myisampack'),
                        vs_config_dirs('myisam', 'myisampack'),
                        "$path_client_bindir/myisampack",
                        "$basedir/storage/myisam/myisampack",
                        "$basedir/myisam/myisampack"));

  # ----------------------------------------------------
  # perror
  # ----------------------------------------------------
  my $exe_perror= mtr_exe_exists(vs_config_dirs('extra', 'perror'),
				 "$basedir/extra/perror",
				 "$path_client_bindir/perror");
  $ENV{'MY_PERROR'}= native_path($exe_perror);

  # Create an environment variable to make it possible
  # to detect that valgrind is being used from test cases
  $ENV{'VALGRIND_TEST'}= $opt_valgrind;

}



#
# Remove var and any directories in var/ created by previous
# tests
#
sub remove_stale_vardir () {

  mtr_report("Removing old var directory...");

  # Safety!
  mtr_error("No, don't remove the vardir when running with --extern")
    if using_extern();

  mtr_verbose("opt_vardir: $opt_vardir");
  if ( $opt_vardir eq $default_vardir )
  {
    #
    # Running with "var" in mysql-test dir
    #
    if ( -l $opt_vardir)
    {
      # var is a symlink

      if ( $opt_mem )
      {
	# Remove the directory which the link points at
	mtr_verbose("Removing " . readlink($opt_vardir));
	rmtree(readlink($opt_vardir));

	# Remove the "var" symlink
	mtr_verbose("unlink($opt_vardir)");
	unlink($opt_vardir);
      }
      else
      {
	# Some users creates a soft link in mysql-test/var to another area
	# - allow it, but remove all files in it

	mtr_report(" - WARNING: Using the 'mysql-test/var' symlink");

	# Make sure the directory where it points exist
	mtr_error("The destination for symlink $opt_vardir does not exist")
	  if ! -d readlink($opt_vardir);

	foreach my $bin ( glob("$opt_vardir/*") )
	{
	  mtr_verbose("Removing bin $bin");
	  rmtree($bin);
	}
      }
    }
    else
    {
      # Remove the entire "var" dir
      mtr_verbose("Removing $opt_vardir/");
      rmtree("$opt_vardir/");
    }

    if ( $opt_mem )
    {
      # A symlink from var/ to $opt_mem will be set up
      # remove the $opt_mem dir to assure the symlink
      # won't point at an old directory
      mtr_verbose("Removing $opt_mem");
      rmtree($opt_mem);
    }

  }
  else
  {
    #
    # Running with "var" in some other place
    #

    # Remove the var/ dir in mysql-test dir if any
    # this could be an old symlink that shouldn't be there
    mtr_verbose("Removing $default_vardir");
    rmtree($default_vardir);

    # Remove the "var" dir
    mtr_verbose("Removing $opt_vardir/");
    rmtree("$opt_vardir/");
  }
  # Remove the "tmp" dir
  mtr_verbose("Removing $opt_tmpdir/");
  rmtree("$opt_tmpdir/");
}



#
# Create var and the directories needed in var
#
sub setup_vardir() {
  mtr_report("Creating var directory '$opt_vardir'...");

  if ( $opt_vardir eq $default_vardir )
  {
    #
    # Running with "var" in mysql-test dir
    #
    if ( -l $opt_vardir )
    {
      #  it's a symlink

      # Make sure the directory where it points exist
      mtr_error("The destination for symlink $opt_vardir does not exist")
	if ! -d readlink($opt_vardir);
    }
    elsif ( $opt_mem )
    {
      # Runinng with "var" as a link to some "memory" location, normally tmpfs
      mtr_verbose("Creating $opt_mem");
      mkpath($opt_mem);

      mtr_report(" - symlinking 'var' to '$opt_mem'");
      symlink($opt_mem, $opt_vardir);
    }
  }

  if ( ! -d $opt_vardir )
  {
    mtr_verbose("Creating $opt_vardir");
    mkpath($opt_vardir);
  }

  # Ensure a proper error message if vardir couldn't be created
  unless ( -d $opt_vardir and -w $opt_vardir )
  {
    mtr_error("Writable 'var' directory is needed, use the " .
	      "'--vardir=<path>' option");
  }

  mkpath("$opt_vardir/log");
  mkpath("$opt_vardir/run");

  # Create var/tmp and tmp - they might be different
  mkpath("$opt_vardir/tmp");
  mkpath($opt_tmpdir) if ($opt_tmpdir ne "$opt_vardir/tmp");

  # On some operating systems, there is a limit to the length of a
  # UNIX domain socket's path far below PATH_MAX.
  # Don't allow that to happen
  if (check_socket_path_length("$opt_tmpdir/testsocket.sock")){
    mtr_error("Socket path '$opt_tmpdir' too long, it would be ",
	      "truncated and thus not possible to use for connection to ",
	      "MySQL Server. Set a shorter with --tmpdir=<path> option");
  }

  # copy all files from std_data into var/std_data
  # and make them world readable
  copytree("$glob_mysql_test_dir/std_data", "$opt_vardir/std_data", "0022");

  # Remove old log files
  foreach my $name (glob("r/*.progress r/*.log r/*.warnings"))
  {
    unlink($name);
  }
}


#
# Check if running as root
# i.e a file can be read regardless what mode we set it to
#
sub  check_running_as_root () {
  my $test_file= "$opt_vardir/test_running_as_root.txt";
  mtr_tofile($test_file, "MySQL");
  chmod(oct("0000"), $test_file);

  my $result="";
  if (open(FILE,"<",$test_file))
  {
    $result= join('', <FILE>);
    close FILE;
  }

  # Some filesystems( for example CIFS) allows reading a file
  # although mode was set to 0000, but in that case a stat on
  # the file will not return 0000
  my $file_mode= (stat($test_file))[2] & 07777;

  mtr_verbose("result: $result, file_mode: $file_mode");
  if ($result eq "MySQL" && $file_mode == 0)
  {
    mtr_warning("running this script as _root_ will cause some " .
                "tests to be skipped");
    $ENV{'MYSQL_TEST_ROOT'}= "YES";
  }

  chmod(oct("0755"), $test_file);
  unlink($test_file);
}


sub check_ssl_support ($) {
  my $mysqld_variables= shift;

  if ($opt_skip_ssl)
  {
    mtr_report(" - skipping SSL");
    $opt_ssl_supported= 0;
    $opt_ssl= 0;
    return;
  }

  if ( ! $mysqld_variables->{'ssl'} )
  {
    if ( $opt_ssl)
    {
      mtr_error("Couldn't find support for SSL");
      return;
    }
    mtr_report(" - skipping SSL, mysqld not compiled with SSL");
    $opt_ssl_supported= 0;
    $opt_ssl= 0;
    return;
  }
  mtr_report(" - SSL connections supported");
  $opt_ssl_supported= 1;
}


sub check_debug_support ($) {
  my $mysqld_variables= shift;

  if ( ! $mysqld_variables->{'debug'} )
  {
    #mtr_report(" - binaries are not debug compiled");
    $debug_compiled_binaries= 0;

    if ( $opt_debug )
    {
      mtr_error("Can't use --debug, binaries does not support it");
    }
    return;
  }
  mtr_report(" - binaries are debug compiled");
  $debug_compiled_binaries= 1;
}


#
# Helper function to handle configuration-based subdirectories which Visual
# Studio uses for storing binaries.  If opt_vs_config is set, this returns
# a path based on that setting; if not, it returns paths for the default
# /release/ and /debug/ subdirectories.
#
# $exe can be undefined, if the directory itself will be used
#
sub vs_config_dirs ($$) {
  my ($path_part, $exe) = @_;

  $exe = "" if not defined $exe;

  # Don't look in these dirs when not on windows
  return () unless IS_WINDOWS;

  if ($opt_vs_config)
  {
    return ("$basedir/$path_part/$opt_vs_config/$exe");
  }

  return ("$basedir/$path_part/release/$exe",
          "$basedir/$path_part/relwithdebinfo/$exe",
          "$basedir/$path_part/debug/$exe");
}


sub check_ndbcluster_support ($) {
  my $mysqld_variables= shift;

  if ($opt_skip_ndbcluster)
  {
    mtr_report(" - skipping ndbcluster");
    return;
  }

  if ( ! $mysqld_variables{'ndb-connectstring'} )
  {
    mtr_report(" - skipping ndbcluster, mysqld not compiled with ndbcluster");
    $opt_skip_ndbcluster= 2;
    return;
  }

  mtr_report(" - using ndbcluster when necessary, mysqld supports it");

  return;
}


sub ndbcluster_wait_started($$){
  my $cluster= shift;
  my $ndb_waiter_extra_opt= shift;
  my $path_waitlog= join('/', $opt_vardir, $cluster->name(), "ndb_waiter.log");

  my $args;
  mtr_init_args(\$args);
  mtr_add_arg($args, "--defaults-file=%s", $path_config_file);
  mtr_add_arg($args, "--defaults-group-suffix=%s", $cluster->suffix());
  mtr_add_arg($args, "--timeout=%d", $opt_start_timeout);

  if ($ndb_waiter_extra_opt)
  {
    mtr_add_arg($args, "$ndb_waiter_extra_opt");
  }

  # Start the ndb_waiter which will connect to the ndb_mgmd
  # and poll it for state of the ndbd's, will return when
  # all nodes in the cluster is started

  my $res= My::SafeProcess->run
    (
     name          => "ndb_waiter ".$cluster->name(),
     path          => $exe_ndb_waiter,
     args          => \$args,
     output        => $path_waitlog,
     error         => $path_waitlog,
     append        => 1,
    );

  # Check that ndb_mgmd(s) are still alive
  foreach my $ndb_mgmd ( in_cluster($cluster, ndb_mgmds()) )
  {
    my $proc= $ndb_mgmd->{proc};
    if ( ! $proc->wait_one(0) )
    {
      mtr_warning("$proc died");
      return 2;
    }
  }

  # Check that all started ndbd(s) are still alive
  foreach my $ndbd ( in_cluster($cluster, ndbds()) )
  {
    my $proc= $ndbd->{proc};
    next unless defined $proc;
    if ( ! $proc->wait_one(0) )
    {
      mtr_warning("$proc died");
      return 3;
    }
  }

  if ($res)
  {
    mtr_verbose("ndbcluster_wait_started failed");
    return 1;
  }
  return 0;
}


sub ndb_mgmd_wait_started($) {
  my ($cluster)= @_;

  my $retries= 100;
  while ($retries)
  {
    my $result= ndbcluster_wait_started($cluster, "--no-contact");
    if ($result == 0)
    {
      # ndb_mgmd is started
      mtr_verbose("ndb_mgmd is started");
      return 0;
    }
    elsif ($result > 1)
    {
      mtr_warning("Cluster process failed while waiting for start");
      return $result;
    }

    mtr_milli_sleep(100);
    $retries--;
  }

  return 1;
}


sub ndb_mgmd_start ($$) {
  my ($cluster, $ndb_mgmd)= @_;

  mtr_verbose("ndb_mgmd_start");

  my $dir= $ndb_mgmd->value("DataDir");
  mkpath($dir) unless -d $dir;

  my $args;
  mtr_init_args(\$args);
  mtr_add_arg($args, "--defaults-file=%s", $path_config_file);
  mtr_add_arg($args, "--defaults-group-suffix=%s", $cluster->suffix());
  mtr_add_arg($args, "--mycnf");
  mtr_add_arg($args, "--nodaemon");

  my $path_ndb_mgmd_log= "$dir/ndb_mgmd.log";

  $ndb_mgmd->{'proc'}= My::SafeProcess->new
    (
     name          => $ndb_mgmd->after('cluster_config.'),
     path          => $exe_ndb_mgmd,
     args          => \$args,
     output        => $path_ndb_mgmd_log,
     error         => $path_ndb_mgmd_log,
     append        => 1,
     verbose       => $opt_verbose,
    );
  mtr_verbose("Started $ndb_mgmd->{proc}");

  # FIXME Should not be needed
  # Unfortunately the cluster nodes will fail to start
  # if ndb_mgmd has not started properly
  if (ndb_mgmd_wait_started($cluster))
  {
    mtr_warning("Failed to wait for start of ndb_mgmd");
    return 1;
  }

  return 0;
}


sub ndbd_start {
  my ($cluster, $ndbd)= @_;

  mtr_verbose("ndbd_start");

  my $dir= $ndbd->value("DataDir");
  mkpath($dir) unless -d $dir;

  my $args;
  mtr_init_args(\$args);
  mtr_add_arg($args, "--defaults-file=%s", $path_config_file);
  mtr_add_arg($args, "--defaults-group-suffix=%s", $cluster->suffix());
  mtr_add_arg($args, "--nodaemon");

# > 5.0 { 'character-sets-dir' => \&fix_charset_dir },


  my $path_ndbd_log= "$dir/ndbd.log";
  my $proc= My::SafeProcess->new
    (
     name          => $ndbd->after('cluster_config.'),
     path          => $exe_ndbd,
     args          => \$args,
     output        => $path_ndbd_log,
     error         => $path_ndbd_log,
     append        => 1,
     verbose       => $opt_verbose,
    );
  mtr_verbose("Started $proc");

  $ndbd->{proc}= $proc;

  return;
}


sub ndbcluster_start ($) {
  my $cluster= shift;

  mtr_verbose("ndbcluster_start '".$cluster->name()."'");

  foreach my $ndb_mgmd ( in_cluster($cluster, ndb_mgmds()) )
  {
    next if started($ndb_mgmd);
    ndb_mgmd_start($cluster, $ndb_mgmd);
  }

  foreach my $ndbd ( in_cluster($cluster, ndbds()) )
  {
    next if started($ndbd);
    ndbd_start($cluster, $ndbd);
  }

  return 0;
}


sub create_config_file_for_extern {
  my %opts=
    (
     socket     => '/tmp/mysqld.sock',
     port       => 3306,
     user       => $opt_user,
     password   => '',
     @_
    );

  mtr_report("Creating my.cnf file for extern server...");
  my $F= IO::File->new($path_config_file, "w")
    or mtr_error("Can't write to $path_config_file: $!");

  print $F "[client]\n";
  while (my ($option, $value)= each( %opts )) {
    print $F "$option= $value\n";
    mtr_report(" $option= $value");
  }

  print $F <<EOF

# binlog reads from [client] and [mysqlbinlog]
[mysqlbinlog]
character-sets-dir= $path_charsetsdir

# mysql_fix_privilege_tables.sh don't read from [client]
[mysql_fix_privilege_tables]
socket            = $opts{'socket'}
port              = $opts{'port'}
user              = $opts{'user'}
password          = $opts{'password'}


EOF
;

  $F= undef; # Close file
}


#
# Kill processes left from previous runs, normally
# there should be none so make sure to warn
# if there is one
#
sub kill_leftovers ($) {
  my $rundir= shift;
  return unless ( -d $rundir );

  mtr_report("Checking leftover processes...");

  # Scan the "run" directory for process id's to kill
  opendir(RUNDIR, $rundir)
    or mtr_error("kill_leftovers, can't open dir \"$rundir\": $!");
  while ( my $elem= readdir(RUNDIR) )
  {
    # Only read pid from files that end with .pid
    if ( $elem =~ /.*[.]pid$/ )
    {
      my $pidfile= "$rundir/$elem";
      next unless -f $pidfile;
      my $pid= mtr_fromfile($pidfile);
      unlink($pidfile);
      unless ($pid=~ /^(\d+)/){
	# The pid was not a valid number
	mtr_warning("Got invalid pid '$pid' from '$elem'");
	next;
      }
      mtr_report(" - found old pid $pid in '$elem', killing it...");

      my $ret= kill("KILL", $pid);
      if ($ret == 0) {
	mtr_report("   process did not exist!");
	next;
      }

      my $check_counter= 100;
      while ($ret > 0 and $check_counter--) {
	mtr_milli_sleep(100);
	$ret= kill(0, $pid);
      }
      mtr_report($check_counter ? "   ok!" : "   failed!");
    }
    else
    {
      mtr_warning("Found non pid file '$elem' in '$rundir'")
	if -f "$rundir/$elem";
    }
  }
  closedir(RUNDIR);
}

#
# Check that all the ports that are going to
# be used are free
#
sub check_ports_free ($)
{
  my $bthread= shift;
  my $portbase = $bthread * 10 + 10000;
  for ($portbase..$portbase+9){
    if (mtr_ping_port($_)){
      mtr_report(" - 'localhost:$_' was not free");
      return 0; # One port was not free
    }
  }

  return 1; # All ports free
}


sub initialize_servers {

  if ( using_extern() )
  {
    # Running against an already started server, if the specified
    # vardir does not already exist it should be created
    if ( ! -d $opt_vardir )
    {
      setup_vardir();
    }
    else
    {
      mtr_verbose("No need to create '$opt_vardir' it already exists");
    }
  }
  else
  {
    # Kill leftovers from previous run
    # using any pidfiles found in var/run
    kill_leftovers("$opt_vardir/run");

    if ( ! $opt_start_dirty )
    {
      remove_stale_vardir();
      setup_vardir();

      mysql_install_db(default_mysqld(), "$opt_vardir/install.db");
    }
  }
}


#
# Remove all newline characters expect after semicolon
#
sub sql_to_bootstrap {
  my ($sql) = @_;
  my @lines= split(/\n/, $sql);
  my $result= "\n";
  my $delimiter= ';';

  foreach my $line (@lines) {

    # Change current delimiter if line starts with "delimiter"
    if ( $line =~ /^delimiter (.*)/ ) {
      my $new= $1;
      # Remove old delimiter from end of new
      $new=~ s/\Q$delimiter\E$//;
      $delimiter = $new;
      mtr_debug("changed delimiter to $delimiter");
      # No need to add the delimiter to result
      next;
    }

    # Add newline if line ends with $delimiter
    # and convert the current delimiter to semicolon
    if ( $line =~ /\Q$delimiter\E$/ ){
      $line =~ s/\Q$delimiter\E$/;/;
      $result.= "$line\n";
      mtr_debug("Added default delimiter");
      next;
    }

    # Remove comments starting with --
    if ( $line =~ /^\s*--/ ) {
      mtr_debug("Discarded $line");
      next;
    }

    # Replace @HOSTNAME with localhost
    $line=~ s/\'\@HOSTNAME\@\'/localhost/;

    # Default, just add the line without newline
    # but with a space as separator
    $result.= "$line ";

  }
  return $result;
}


sub default_mysqld {
  # Generate new config file from template
  my $config= My::ConfigFactory->new_config
    ( {
       basedir         => $basedir,
       template_path   => "include/default_my.cnf",
       vardir          => $opt_vardir,
       tmpdir          => $opt_tmpdir,
       baseport        => 0,
       user            => $opt_user,
       password        => '',
      }
    );

  my $mysqld= $config->group('mysqld.1')
    or mtr_error("Couldn't find mysqld.1 in default config");
  return $mysqld;
}


sub mysql_install_db {
  my ($mysqld, $datadir)= @_;

  my $install_datadir= $datadir || $mysqld->value('datadir');
  my $install_basedir= $mysqld->value('basedir');
  my $install_lang= $mysqld->value('language');
  my $install_chsdir= $mysqld->value('character-sets-dir');

  mtr_report("Installing system database...");

  my $args;
  mtr_init_args(\$args);
  mtr_add_arg($args, "--no-defaults");
  mtr_add_arg($args, "--bootstrap");
  mtr_add_arg($args, "--basedir=%s", $install_basedir);
  mtr_add_arg($args, "--datadir=%s", $install_datadir);
  mtr_add_arg($args, "--loose-innodb=OFF");
  mtr_add_arg($args, "--loose-skip-falcon");
  mtr_add_arg($args, "--loose-skip-ndbcluster");
  mtr_add_arg($args, "--tmpdir=%s", "$opt_vardir/tmp/");
  mtr_add_arg($args, "--core-file");

  if ( $opt_debug )
  {
    mtr_add_arg($args, "--debug=d:t:i:A,%s/log/bootstrap.trace",
		$path_vardir_trace);
  }

  mtr_add_arg($args, "--language=%s", $install_lang);
  mtr_add_arg($args, "--character-sets-dir=%s", $install_chsdir);

  # If DISABLE_GRANT_OPTIONS is defined when the server is compiled (e.g.,
  # configure --disable-grant-options), mysqld will not recognize the
  # --bootstrap or --skip-grant-tables options.  The user can set
  # MYSQLD_BOOTSTRAP to the full path to a mysqld which does accept
  # --bootstrap, to accommodate this.
  my $exe_mysqld_bootstrap =
    $ENV{'MYSQLD_BOOTSTRAP'} || find_mysqld($install_basedir);

  # ----------------------------------------------------------------------
  # export MYSQLD_BOOTSTRAP_CMD variable containing <path>/mysqld <args>
  # ----------------------------------------------------------------------
  $ENV{'MYSQLD_BOOTSTRAP_CMD'}= "$exe_mysqld_bootstrap " . join(" ", @$args);



  # ----------------------------------------------------------------------
  # Create the bootstrap.sql file
  # ----------------------------------------------------------------------
  my $bootstrap_sql_file= "$opt_vardir/tmp/bootstrap.sql";

  my $path_sql= my_find_file($install_basedir,
			     ["mysql", "sql/share", "share/mysql",
			      "share", "scripts"],
			     "mysql_system_tables.sql",
			     NOT_REQUIRED);

  if (-f $path_sql )
  {
    my $sql_dir= dirname($path_sql);
    # Use the mysql database for system tables
    mtr_tofile($bootstrap_sql_file, "use mysql\n");

    # Add the offical mysql system tables
    # for a production system
    mtr_appendfile_to_file("$sql_dir/mysql_system_tables.sql",
			   $bootstrap_sql_file);

    # Add the mysql system tables initial data
    # for a production system
    mtr_appendfile_to_file("$sql_dir/mysql_system_tables_data.sql",
			   $bootstrap_sql_file);

    # Add test data for timezone - this is just a subset, on a real
    # system these tables will be populated either by mysql_tzinfo_to_sql
    # or by downloading the timezone table package from our website
    mtr_appendfile_to_file("$sql_dir/mysql_test_data_timezone.sql",
			   $bootstrap_sql_file);

    # Fill help tables, just an empty file when running from bk repo
    # but will be replaced by a real fill_help_tables.sql when
    # building the source dist
    mtr_appendfile_to_file("$sql_dir/fill_help_tables.sql",
			   $bootstrap_sql_file);

  }
  else
  {
    # Install db from init_db.sql that exist in early 5.1 and 5.0
    # versions of MySQL
    my $init_file= "$install_basedir/mysql-test/lib/init_db.sql";
    mtr_report(" - from '$init_file'");
    my $text= mtr_grab_file($init_file) or
      mtr_error("Can't open '$init_file': $!");

    mtr_tofile($bootstrap_sql_file,
	       sql_to_bootstrap($text));
  }

  # Remove anonymous users
  mtr_tofile($bootstrap_sql_file,
	     "DELETE FROM mysql.user where user= '';\n");

  # Create mtr database
  mtr_tofile($bootstrap_sql_file,
	     "CREATE DATABASE mtr;\n");

  # Add help tables and data for warning detection and supression
  mtr_tofile($bootstrap_sql_file,
             sql_to_bootstrap(mtr_grab_file("include/mtr_warnings.sql")));

  # Add procedures for checking server is restored after testcase
  mtr_tofile($bootstrap_sql_file,
             sql_to_bootstrap(mtr_grab_file("include/mtr_check.sql")));

  # Log bootstrap command
  my $path_bootstrap_log= "$opt_vardir/log/bootstrap.log";
  mtr_tofile($path_bootstrap_log,
	     "$exe_mysqld_bootstrap " . join(" ", @$args) . "\n");

  # Create directories mysql and test
  mkpath("$install_datadir/mysql");
  mkpath("$install_datadir/test");

  if ( My::SafeProcess->run
       (
	name          => "bootstrap",
	path          => $exe_mysqld_bootstrap,
	args          => \$args,
	input         => $bootstrap_sql_file,
	output        => $path_bootstrap_log,
	error         => $path_bootstrap_log,
	append        => 1,
	verbose       => $opt_verbose,
       ) != 0)
  {
    mtr_error("Error executing mysqld --bootstrap\n" .
              "Could not install system database from $bootstrap_sql_file\n" .
	      "see $path_bootstrap_log for errors");
  }
}


sub run_testcase_check_skip_test($)
{
  my ($tinfo)= @_;

  # ----------------------------------------------------------------------
  # If marked to skip, just print out and return.
  # Note that a test case not marked as 'skip' can still be
  # skipped later, because of the test case itself in cooperation
  # with the mysqltest program tells us so.
  # ----------------------------------------------------------------------

  if ( $tinfo->{'skip'} )
  {
    mtr_report_test_skipped($tinfo) unless $start_only;
    return 1;
  }

  return 0;
}


sub run_query {
  my ($tinfo, $mysqld, $query)= @_;

  my $args;
  mtr_init_args(\$args);
  mtr_add_arg($args, "--defaults-file=%s", $path_config_file);
  mtr_add_arg($args, "--defaults-group-suffix=%s", $mysqld->after('mysqld'));

  mtr_add_arg($args, "-e %s", $query);

  my $res= My::SafeProcess->run
    (
     name          => "run_query -> ".$mysqld->name(),
     path          => $exe_mysql,
     args          => \$args,
     output        => '/dev/null',
     error         => '/dev/null'
    );

  return $res
}


sub do_before_run_mysqltest($)
{
  my $tinfo= shift;

  # Remove old files produced by mysqltest
  my $base_file= mtr_match_extension($tinfo->{result_file},
				     "result"); # Trim extension
  if (defined $base_file ){
    unlink("$base_file.reject");
    unlink("$base_file.progress");
    unlink("$base_file.log");
    unlink("$base_file.warnings");
  }

  if ( $mysql_version_id < 50000 ) {
    # Set environment variable NDB_STATUS_OK to 1
    # if script decided to run mysqltest cluster _is_ installed ok
    $ENV{'NDB_STATUS_OK'} = "1";
  } elsif ( $mysql_version_id < 50100 ) {
    # Set environment variable NDB_STATUS_OK to YES
    # if script decided to run mysqltest cluster _is_ installed ok
    $ENV{'NDB_STATUS_OK'} = "YES";
  }
}


#
# Check all server for sideffects
#
# RETURN VALUE
#  0 ok
#  1 Check failed
#  >1 Fatal errro

sub check_testcase($$)
{
  my ($tinfo, $mode)= @_;
  my $tname= $tinfo->{name};

  # Start the mysqltest processes in parallel to save time
  # also makes it possible to wait for any process to exit during the check
  my %started;
  foreach my $mysqld ( mysqlds() )
  {
    if ( defined $mysqld->{'proc'} )
    {
      my $proc= start_check_testcase($tinfo, $mode, $mysqld);
      $started{$proc->pid()}= $proc;
    }
  }

  # Return immediately if no check proceess was started
  return 0 unless ( keys %started );

  my $timeout_proc= My::SafeProcess->timer(check_timeout());

  while (1){
    my $result;
    my $proc= My::SafeProcess->wait_any();
    mtr_report("Got $proc");

    if ( delete $started{$proc->pid()} ) {

      my $err_file= $proc->user_data();
      my $base_file= mtr_match_extension($err_file, "err"); # Trim extension

      # One check testcase process returned
      my $res= $proc->exit_status();

      if ( $res == 0){
	# Check completed without problem

	# Remove the .err file the check generated
	unlink($err_file);

	# Remove the .result file the check generated
	if ( $mode eq 'after' ){
	  unlink("$base_file.result");
	}

	if ( keys(%started) == 0){
	  # All checks completed

	  $timeout_proc->kill();

	  return 0;
	}
	# Wait for next process to exit
	next;
      }
      else
      {
	if ( $mode eq "after" and $res == 1 )
	{
	  # Test failed, grab the report mysqltest has created
	  my $report= mtr_grab_file($err_file);
	  $tinfo->{check}.=
	    "\nMTR's internal check of the test case '$tname' failed.
This means that the test case does not preserve the state that existed
before the test case was executed.  Most likely the test case did not
do a proper clean-up.
This is the diff of the states of the servers before and after the
test case was executed:\n";
	  $tinfo->{check}.= $report;

	  # Check failed, mark the test case with that info
	  $tinfo->{'check_testcase_failed'}= 1;
	  $result= 1;
	}
	elsif ( $res )
	{
	  my $report= mtr_grab_file($err_file);
	  $tinfo->{comment}.=
	    "Could not execute 'check-testcase' $mode ".
	      "testcase '$tname' (res: $res):\n";
	  $tinfo->{comment}.= $report;

	  $result= 2;
	}

	# Remove the .result file the check generated
	unlink("$base_file.result");

      }
    }
    elsif ( $proc eq $timeout_proc ) {
      $tinfo->{comment}.= "Timeout $timeout_proc for ".
	"'check-testcase' expired after ".check_timeout().
	  " seconds";
      $result= 4;
    }
    else {
      # Unknown process returned, most likley a crash, abort everything
      $tinfo->{comment}=
	"The server $proc crashed while running ".
	"'check testcase $mode test'".
	get_log_from_proc($proc, $tinfo->{name});
      $result= 3;
    }

    # Kill any check processes still running
    map($_->kill(), values(%started));

    $timeout_proc->kill();

    return $result;
  }

  mtr_error("INTERNAL_ERROR: check_testcase");
}


# Start run mysqltest on one server
#
# RETURN VALUE
#  0 OK
#  1 Check failed
#
sub start_run_one ($$) {
  my ($mysqld, $run)= @_;

  my $name= "$run-".$mysqld->name();

  my $args;
  mtr_init_args(\$args);

  mtr_add_arg($args, "--defaults-file=%s", $path_config_file);
  mtr_add_arg($args, "--defaults-group-suffix=%s", $mysqld->after('mysqld'));

  mtr_add_arg($args, "--silent");
  mtr_add_arg($args, "--skip-safemalloc");
  mtr_add_arg($args, "--test-file=%s", "include/$run.test");

  my $errfile= "$opt_vardir/tmp/$name.err";
  my $proc= My::SafeProcess->new
    (
     name          => $name,
     path          => $exe_mysqltest,
     error         => $errfile,
     output        => $errfile,
     args          => \$args,
     user_data     => $errfile,
     verbose       => $opt_verbose,
    );
  mtr_verbose("Started $proc");
  return $proc;
}


#
# Run script on all servers, collect results
#
# RETURN VALUE
#  0 ok
#  1 Failure

sub run_on_all($$)
{
  my ($tinfo, $run)= @_;

  # Start the mysqltest processes in parallel to save time
  # also makes it possible to wait for any process to exit during the check
  # and to have a timeout process
  my %started;
  foreach my $mysqld ( mysqlds() )
  {
    if ( defined $mysqld->{'proc'} )
    {
      my $proc= start_run_one($mysqld, $run);
      $started{$proc->pid()}= $proc;
    }
  }

  # Return immediately if no check proceess was started
  return 0 unless ( keys %started );

  my $timeout_proc= My::SafeProcess->timer(check_timeout());

  while (1){
    my $result;
    my $proc= My::SafeProcess->wait_any();
    mtr_report("Got $proc");

    if ( delete $started{$proc->pid()} ) {

      # One mysqltest process returned
      my $err_file= $proc->user_data();
      my $res= $proc->exit_status();

      # Append the report from .err file
      $tinfo->{comment}.= " == $err_file ==\n";
      $tinfo->{comment}.= mtr_grab_file($err_file);
      $tinfo->{comment}.= "\n";

      # Remove the .err file
      unlink($err_file);

      if ( keys(%started) == 0){
	# All completed
	$timeout_proc->kill();
	return 0;
      }

      # Wait for next process to exit
      next;
    }
    elsif ( $proc eq $timeout_proc ) {
      $tinfo->{comment}.= "Timeout $timeout_proc for '$run' ".
	"expired after ". check_timeout().
	  " seconds";
    }
    else {
      # Unknown process returned, most likley a crash, abort everything
      $tinfo->{comment}.=
	"The server $proc crashed while running '$run'".
	get_log_from_proc($proc, $tinfo->{name});
    }

    # Kill any check processes still running
    map($_->kill(), values(%started));

    $timeout_proc->kill();

    return 1;
  }
  mtr_error("INTERNAL_ERROR: run_on_all");
}


sub mark_log {
  my ($log, $tinfo)= @_;
  my $log_msg= "CURRENT_TEST: $tinfo->{name}\n";
  mtr_tofile($log, $log_msg);
}


sub find_testcase_skipped_reason($)
{
  my ($tinfo)= @_;

  # Set default message
  $tinfo->{'comment'}= "Detected by testcase(no log file)";

  # Open the test log file
  my $F= IO::File->new($path_current_testlog)
    or return;
  my $reason;

  while ( my $line= <$F> )
  {
    # Look for "reason: <reason for skipping test>"
    if ( $line =~ /reason: (.*)/ )
    {
      $reason= $1;
    }
  }

  if ( ! $reason )
  {
    mtr_warning("Could not find reason for skipping test in $path_current_testlog");
    $reason= "Detected by testcase(reason unknown) ";
  }
  $tinfo->{'comment'}= $reason;
}


sub find_analyze_request
{
  # Open the test log file
  my $F= IO::File->new($path_current_testlog)
    or return;
  my $analyze;

  while ( my $line= <$F> )
  {
    # Look for "reason: <reason for skipping test>"
    if ( $line =~ /analyze: (.*)/ )
    {
      $analyze= $1;
    }
  }

  return $analyze;
}


# The test can leave a file in var/tmp/ to signal
# that all servers should be restarted
sub restart_forced_by_test
{
  my $restart = 0;
  foreach my $mysqld ( mysqlds() )
  {
    my $datadir = $mysqld->value('datadir');
    my $force_restart_file = "$datadir/mtr/force_restart";
    if ( -f $force_restart_file )
    {
      mtr_verbose("Restart of servers forced by test");
      $restart = 1;
      last;
    }
  }
  return $restart;
}


# Return timezone value of tinfo or default value
sub timezone {
  my ($tinfo)= @_;
  return $tinfo->{timezone} || "GMT-3";
}


# Storage for changed environment variables
my %old_env;

#
# Run a single test case
#
# RETURN VALUE
#  0 OK
#  > 0 failure
#

sub run_testcase ($) {
  my $tinfo=  shift;

  mtr_verbose("Running test:", $tinfo->{name});

  # Allow only alpanumerics pluss _ - + . in combination names
  my $combination= $tinfo->{combination};
  if ($combination && $combination !~ /^\w[-\w\.\+]+$/)
  {
    mtr_error("Combination '$combination' contains illegal characters");
  }
  # -------------------------------------------------------
  # Init variables that can change between each test case
  # -------------------------------------------------------
  my $timezone= timezone($tinfo);
  $ENV{'TZ'}= $timezone;
  mtr_verbose("Setting timezone: $timezone");

  if ( ! using_extern() )
  {
    my @restart= servers_need_restart($tinfo);
    if ( @restart != 0) {
      stop_servers($tinfo, @restart );
    }

    if ( started(all_servers()) == 0 )
    {

      # Remove old datadirs
      clean_datadir() unless $opt_start_dirty;

      # Restore old ENV
      while (my ($option, $value)= each( %old_env )) {
	if (defined $value){
	  mtr_verbose("Restoring $option to $value");
	  $ENV{$option}= $value;

	} else {
	  mtr_verbose("Removing $option");
	  delete($ENV{$option});
	}
      }
      %old_env= ();

      mtr_verbose("Generating my.cnf from '$tinfo->{template_path}'");

      # Generate new config file from template
      $config= My::ConfigFactory->new_config
	( {
	   basedir         => $basedir,
	   template_path   => $tinfo->{template_path},
	   extra_template_path => $tinfo->{extra_template_path},
	   vardir          => $opt_vardir,
	   tmpdir          => $opt_tmpdir,
	   baseport        => $baseport,
	   #hosts          => [ 'host1', 'host2' ],
	   user            => $opt_user,
	   password        => '',
	   ssl             => $opt_ssl_supported,
	   embedded        => $opt_embedded_server,
	  }
	);

      # Write the new my.cnf
      $config->save($path_config_file);

      # Remember current config so a restart can occur when a test need
      # to use a different one
      $current_config_name= $tinfo->{template_path};

      #
      # Set variables in the ENV section
      #
      foreach my $option ($config->options_in_group("ENV"))
      {
	# Save old value to restore it before next time
	$old_env{$option->name()}= $ENV{$option->name()};

	mtr_verbose($option->name(), "=",$option->value());
	$ENV{$option->name()}= $option->value();
      }
    }

    # Write start of testcase to log
    mark_log($path_current_testlog, $tinfo);

    if (start_servers($tinfo))
    {
      report_failure_and_restart($tinfo);
      return 1;
    }
  }

  # --------------------------------------------------------------------
  # If --start or --start-dirty given, stop here to let user manually
  # run tests
  # If --wait-all is also given, do the same, but don't die if one
  # server exits
  # ----------------------------------------------------------------------

  if ( $start_only )
  {
    mtr_print("\nStarted", started(all_servers()));
    mtr_print("Using config for test", $tinfo->{name});
    mtr_print("Port and socket path for server(s):");
    foreach my $mysqld ( mysqlds() )
    {
      mtr_print ($mysqld->name() . "  " . $mysqld->value('port') .
	      "  " . $mysqld->value('socket'));
    }
    mtr_print("Waiting for server(s) to exit...");
    if ( $opt_wait_all ) {
      My::SafeProcess->wait_all();
      mtr_print( "All servers exited" );
      exit(1);
    }
    else {
      my $proc= My::SafeProcess->wait_any();
      if ( grep($proc eq $_, started(all_servers())) )
      {
        mtr_print("Server $proc died");
        exit(1);
      }
      mtr_print("Unknown process $proc died");
      exit(1);
    }
  }

  my $test_timeout_proc= My::SafeProcess->timer(testcase_timeout());

  do_before_run_mysqltest($tinfo);

  if ( $opt_check_testcases and check_testcase($tinfo, "before") ){
    # Failed to record state of server or server crashed
    report_failure_and_restart($tinfo);

    # Stop the test case timer
    $test_timeout_proc->kill();

    return 1;
  }

  my $test= start_mysqltest($tinfo);
  # Set only when we have to keep waiting after expectedly died server
  my $keep_waiting_proc = 0;

  while (1)
  {
    my $proc;
    if ($keep_waiting_proc)
    {
      # Any other process exited?
      $proc = My::SafeProcess->check_any();
      if ($proc)
      {
	mtr_verbose ("Found exited process $proc");
	# If that was the timeout, cancel waiting
	if ( $proc eq $test_timeout_proc )
	{
	  $keep_waiting_proc = 0;
	}
      }
      else
      {
	$proc = $keep_waiting_proc;
      }
    }
    else
    {
      $proc= My::SafeProcess->wait_any();
    }

    # Will be restored if we need to keep waiting
    $keep_waiting_proc = 0;

    unless ( defined $proc )
    {
      mtr_error("wait_any failed");
    }
    mtr_verbose("Got $proc");

    # ----------------------------------------------------
    # Was it the test program that exited
    # ----------------------------------------------------
    if ($proc eq $test)
    {
      # Stop the test case timer
      $test_timeout_proc->kill();

      my $res= $test->exit_status();

      if ($res == 0 and $opt_warnings and check_warnings($tinfo) )
      {
	# Test case suceeded, but it has produced unexpected
	# warnings, continue in $res == 1
	$res= 1;
      }

      if ( $res == 0 )
      {
	my $check_res;
	if ( restart_forced_by_test() )
	{
	  stop_all_servers($opt_shutdown_timeout);
	}
	elsif ( $opt_check_testcases and
	     $check_res= check_testcase($tinfo, "after"))
	{
	  if ($check_res == 1) {
	    # Test case had sideeffects, not fatal error, just continue
	    stop_all_servers($opt_shutdown_timeout);
	    mtr_report("Resuming tests...\n");
	  }
	  else {
	    # Test case check failed fatally, probably a server crashed
	    report_failure_and_restart($tinfo);
	    return 1;
	  }
	}
	mtr_report_test_passed($tinfo);
      }
      elsif ( $res == 62 )
      {
	# Testcase itself tell us to skip this one
	$tinfo->{skip_detected_by_test}= 1;
	# Try to get reason from test log file
	find_testcase_skipped_reason($tinfo);
	mtr_report_test_skipped($tinfo);
      }
      elsif ( $res == 65 )
      {
	# Testprogram killed by signal
	$tinfo->{comment}=
	  "testprogram crashed(returned code $res)";
	report_failure_and_restart($tinfo);
      }
      elsif ( $res == 1 )
      {
	# Check if the test tool requests that
	# an analyze script should be run
	my $analyze= find_analyze_request();
	if ($analyze){
	  run_on_all($tinfo, "analyze-$analyze");
	}

	# Test case failure reported by mysqltest
	report_failure_and_restart($tinfo);
      }
      else
      {
	# mysqltest failed, probably crashed
	$tinfo->{comment}=
	  "mysqltest failed with unexpected return code $res";
	report_failure_and_restart($tinfo);
      }

      # Save info from this testcase run to mysqltest.log
      if( -f $path_current_testlog)
      {
	mtr_appendfile_to_file($path_current_testlog, $path_testlog);
	unlink($path_current_testlog);
      }

      return ($res == 62) ? 0 : $res;

    }

    # ----------------------------------------------------
    # Check if it was an expected crash
    # ----------------------------------------------------
    my $check_crash = check_expected_crash_and_restart($proc);
    if ($check_crash)
    {
      # Keep waiting if it returned 2, if 1 don't wait or stop waiting.
      $keep_waiting_proc = 0 if $check_crash == 1;
      $keep_waiting_proc = $proc if $check_crash == 2;
      next;
    }

    # ----------------------------------------------------
    # Stop the test case timer
    # ----------------------------------------------------
    $test_timeout_proc->kill();

    # ----------------------------------------------------
    # Check if it was a server that died
    # ----------------------------------------------------
    if ( grep($proc eq $_, started(all_servers())) )
    {
      # Server failed, probably crashed
      $tinfo->{comment}=
	"Server $proc failed during test run" .
	get_log_from_proc($proc, $tinfo->{name});

      # ----------------------------------------------------
      # It's not mysqltest that has exited, kill it
      # ----------------------------------------------------
      $test->kill();

      report_failure_and_restart($tinfo);
      return 1;
    }

    # Try to dump core for mysqltest and all servers
    foreach my $proc ($test, started(all_servers())) 
    {
      mtr_print("Trying to dump core for $proc");
      if ($proc->dump_core())
      {
	$proc->wait_one(20);
      }
    }

    # ----------------------------------------------------
    # It's not mysqltest that has exited, kill it
    # ----------------------------------------------------
    $test->kill();

    # ----------------------------------------------------
    # Check if testcase timer expired
    # ----------------------------------------------------
    if ( $proc eq $test_timeout_proc )
    {
      my $log_file_name= $opt_vardir."/log/".$tinfo->{shortname}.".log";
      $tinfo->{comment}=
        "Test case timeout after ".testcase_timeout().
	  " seconds\n\n";
      # Add 20 last executed commands from test case log file
      if  (-e $log_file_name)
      {
        $tinfo->{comment}.=
	   "== $log_file_name == \n".
	     mtr_lastlinesfromfile($log_file_name, 20)."\n";
      }
      $tinfo->{'timeout'}= testcase_timeout(); # Mark as timeout
      run_on_all($tinfo, 'analyze-timeout');

      report_failure_and_restart($tinfo);
      return 1;
    }

    mtr_error("Unhandled process $proc exited");
  }
  mtr_error("Should never come here");
}


# Extract server log from after the last occurrence of named test
# Return as an array of lines
#

sub extract_server_log ($$) {
  my ($error_log, $tname) = @_;

  # Open the servers .err log file and read all lines
  # belonging to current tets into @lines
  my $Ferr = IO::File->new($error_log)
    or mtr_error("Could not open file '$error_log' for reading: $!");

  my @lines;
  my $found_test= 0;		# Set once we've found the log of this test
  while ( my $line = <$Ferr> )
  {
    if ($found_test)
    {
      # If test wasn't last after all, discard what we found, test again.
      if ( $line =~ /^CURRENT_TEST:/)
      {
	@lines= ();
	$found_test= $line =~ /^CURRENT_TEST: $tname/;
      }
      else
      {
	push(@lines, $line);
      }
    }
    else
    {
      # Search for beginning of test, until found
      $found_test= 1 if ($line =~ /^CURRENT_TEST: $tname/);
    }
  }
  $Ferr = undef; # Close error log file

  # mysql_client_test.test sends a COM_DEBUG packet to the server
  # to provoke a SAFEMALLOC leak report, ignore any warnings
  # between "Begin/end safemalloc memory dump"
  if ( grep(/Begin safemalloc memory dump:/, @lines) > 0)
  {
    my $discard_lines= 1;
    foreach my $line ( @lines )
    {
      if ($line =~ /Begin safemalloc memory dump:/){
	$discard_lines = 1;
      } elsif ($line =~ /End safemalloc memory dump./){
	$discard_lines = 0;
      }

      if ($discard_lines){
	$line = "ignored";
      }
    }
  }
  return @lines;
}

# Get log from server identified from its $proc object, from named test
# Return as a single string
#

sub get_log_from_proc ($$) {
  my ($proc, $name)= @_;
  my $srv_log= "";

  foreach my $mysqld (mysqlds()) {
    if ($mysqld->{proc} eq $proc) {
      my @srv_lines= extract_server_log($mysqld->value('#log-error'), $name);
      $srv_log= "\nServer log from this test:\n" . join ("", @srv_lines);
      last;
    }
  }
  return $srv_log;
}

# Perform a rough examination of the servers
# error log and write all lines that look
# suspicious into $error_log.warnings
#
sub extract_warning_lines ($$) {
  my ($error_log, $tname) = @_;

  my @lines= extract_server_log($error_log, $tname);

# Write all suspicious lines to $error_log.warnings file
  my $warning_log = "$error_log.warnings";
  my $Fwarn = IO::File->new($warning_log, "w")
    or die("Could not open file '$warning_log' for writing: $!");
  print $Fwarn "Suspicious lines from $error_log\n";

  my @patterns =
    (
     qr/^Warning:|mysqld: Warning|\[Warning\]/,
     qr/^Error:|\[ERROR\]/,
     qr/^==\d*==/, # valgrind errors
     qr/InnoDB: Warning|InnoDB: Error/,
     qr/^safe_mutex:|allocated at line/,
     qr/missing DBUG_RETURN/,
     qr/Attempting backtrace/,
     qr/Assertion .* failed/,
    );

  foreach my $line ( @lines )
  {
    foreach my $pat ( @patterns )
    {
      if ( $line =~ /$pat/ )
      {
	print $Fwarn $line;
	last;
      }
    }
  }
  $Fwarn = undef; # Close file

}


# Run include/check-warnings.test
#
# RETURN VALUE
#  0 OK
#  1 Check failed
#
sub start_check_warnings ($$) {
  my $tinfo=    shift;
  my $mysqld=   shift;

  my $name= "warnings-".$mysqld->name();

  my $log_error= $mysqld->value('#log-error');
  # To be communicated to the test
  $ENV{MTR_LOG_ERROR}= $log_error;
  extract_warning_lines($log_error, $tinfo->{name});

  my $args;
  mtr_init_args(\$args);

  mtr_add_arg($args, "--defaults-file=%s", $path_config_file);
  mtr_add_arg($args, "--defaults-group-suffix=%s", $mysqld->after('mysqld'));

  mtr_add_arg($args, "--skip-safemalloc");
  mtr_add_arg($args, "--test-file=%s", "include/check-warnings.test");
  mtr_add_arg($args, "--verbose");

  if ( $opt_embedded_server )
  {

    # Get the args needed for the embedded server
    # and append them to args prefixed
    # with --sever-arg=

    my $mysqld=  $config->group('embedded')
      or mtr_error("Could not get [embedded] section");

    my $mysqld_args;
    mtr_init_args(\$mysqld_args);
    my $extra_opts= get_extra_opts($mysqld, $tinfo);
    mysqld_arguments($mysqld_args, $mysqld, $extra_opts);
    mtr_add_arg($args, "--server-arg=%s", $_) for @$mysqld_args;
  }

  my $errfile= "$opt_vardir/tmp/$name.err";
  my $proc= My::SafeProcess->new
    (
     name          => $name,
     path          => $exe_mysqltest,
     error         => $errfile,
     output        => $errfile,
     args          => \$args,
     user_data     => $errfile,
     verbose       => $opt_verbose,
    );
  mtr_verbose("Started $proc");
  return $proc;
}


#
# Loop through our list of processes and check the error log
# for unexepcted errors and warnings
#
sub check_warnings ($) {
  my ($tinfo)= @_;
  my $res= 0;

  my $tname= $tinfo->{name};

  # Clear previous warnings
  delete($tinfo->{warnings});

  # Start the mysqltest processes in parallel to save time
  # also makes it possible to wait for any process to exit during the check
  my %started;
  foreach my $mysqld ( mysqlds() )
  {
    if ( defined $mysqld->{'proc'} )
    {
      my $proc= start_check_warnings($tinfo, $mysqld);
      $started{$proc->pid()}= $proc;
    }
  }

  # Return immediately if no check proceess was started
  return 0 unless ( keys %started );

  my $timeout_proc= My::SafeProcess->timer(check_timeout());

  while (1){
    my $result= 0;
    my $proc= My::SafeProcess->wait_any();
    mtr_report("Got $proc");

    if ( delete $started{$proc->pid()} ) {
      # One check warning process returned
      my $res= $proc->exit_status();
      my $err_file= $proc->user_data();

      if ( $res == 0 or $res == 62 ){

	if ( $res == 0 ) {
	  # Check completed with problem
	  my $report= mtr_grab_file($err_file);
	  # Log to var/log/warnings file
	  mtr_tofile("$opt_vardir/log/warnings",
		     $tname."\n".$report);

	  $tinfo->{'warnings'}.= $report;
	  $result= 1;
	}

	if ( $res == 62 ) {
	  # Test case was ok and called "skip"
	  # Remove the .err file the check generated
	  unlink($err_file);
	}

	if ( keys(%started) == 0){
	  # All checks completed

	  $timeout_proc->kill();

	  return $result;
	}
	# Wait for next process to exit
	next;
      }
      else
      {
	my $report= mtr_grab_file($err_file);
	$tinfo->{comment}.=
	  "Could not execute 'check-warnings' for ".
	    "testcase '$tname' (res: $res):\n";
	$tinfo->{comment}.= $report;

	$result= 2;
      }
    }
    elsif ( $proc eq $timeout_proc ) {
      $tinfo->{comment}.= "Timeout $timeout_proc for ".
	"'check warnings' expired after ".check_timeout().
	  " seconds";
      $result= 4;
    }
    else {
      # Unknown process returned, most likley a crash, abort everything
      $tinfo->{comment}=
	"The server $proc crashed while running 'check warnings'".
	get_log_from_proc($proc, $tinfo->{name});
      $result= 3;
    }

    # Kill any check processes still running
    map($_->kill(), values(%started));

    $timeout_proc->kill();

    return $result;
  }

  mtr_error("INTERNAL_ERROR: check_warnings");
}


#
# Loop through our list of processes and look for and entry
# with the provided pid, if found check for the file indicating
# expected crash and restart it.
#
sub check_expected_crash_and_restart {
  my ($proc)= @_;

  foreach my $mysqld ( mysqlds() )
  {
    next unless ( $mysqld->{proc} eq $proc );

    # Check if crash expected by looking at the .expect file
    # in var/tmp
    my $expect_file= "$opt_vardir/tmp/".$mysqld->name().".expect";
    if ( -f $expect_file )
    {
      mtr_verbose("Crash was expected, file '$expect_file' exists");

      for (my $waits = 0;  $waits < 50;  $waits++)
      {
	# If last line in expect file starts with "wait"
	# sleep a little and try again, thus allowing the
	# test script to control when the server should start
	# up again. Keep trying for up to 5s at a time.
	my $last_line= mtr_lastlinesfromfile($expect_file, 1);
	if ($last_line =~ /^wait/ )
	{
	  mtr_verbose("Test says wait before restart") if $waits == 0;
	  mtr_milli_sleep(100);
	  next;
	}

	unlink($expect_file);

	# Start server with same settings as last time
	mysqld_start($mysqld, $mysqld->{'started_opts'});

	return 1;
      }
      # Loop ran through: we should keep waiting after a re-check
      return 2;
    }
  }

  # Not an expected crash
  return 0;
}


# Remove all files and subdirectories of a directory
sub clean_dir {
  my ($dir)= @_;
  mtr_verbose("clean_dir: $dir");
  finddepth(
	  { no_chdir => 1,
	    wanted => sub {
	      if (-d $_){
		# A dir
		if ($_ eq $dir){
		  # The dir to clean
		  return;
		} else {
		  mtr_verbose("rmdir: '$_'");
		  rmdir($_) or mtr_warning("rmdir($_) failed: $!");
		}
	      } else {
		# Hopefully a file
		mtr_verbose("unlink: '$_'");
		unlink($_) or mtr_warning("unlink($_) failed: $!");
	      }
	    }
	  },
	    $dir);
}


sub clean_datadir {

  mtr_verbose("Cleaning datadirs...");

  if (started(all_servers()) != 0){
    mtr_error("Trying to clean datadir before all servers stopped");
  }

  foreach my $cluster ( clusters() )
  {
    my $cluster_dir= "$opt_vardir/".$cluster->{name};
    mtr_verbose(" - removing '$cluster_dir'");
    rmtree($cluster_dir);

  }

  foreach my $mysqld ( mysqlds() )
  {
    my $mysqld_dir= dirname($mysqld->value('datadir'));
    if (-d $mysqld_dir ) {
      mtr_verbose(" - removing '$mysqld_dir'");
      rmtree($mysqld_dir);
    }
  }

  # Remove all files in tmp and var/tmp
  clean_dir("$opt_vardir/tmp");
  if ($opt_tmpdir ne "$opt_vardir/tmp"){
    clean_dir($opt_tmpdir);
  }
}


#
# Save datadir before it's removed
#
sub save_datadir_after_failure($$) {
  my ($dir, $savedir)= @_;

  mtr_report(" - saving '$dir'");
  my $dir_name= basename($dir);
  rename("$dir", "$savedir/$dir_name");
}


sub remove_ndbfs_from_ndbd_datadir {
  my ($ndbd_datadir)= @_;
  # Remove the ndb_*_fs directory from ndbd.X/ dir
  foreach my $ndbfs_dir ( glob("$ndbd_datadir/ndb_*_fs") )
  {
    next unless -d $ndbfs_dir; # Skip if not a directory
    rmtree($ndbfs_dir);
  }
}


sub after_failure ($) {
  my ($tinfo)= @_;

  mtr_report("Saving datadirs...");

  my $save_dir= "$opt_vardir/log/";
  $save_dir.= $tinfo->{name};
  # Add combination name if any
  $save_dir.= "-$tinfo->{combination}"
    if defined $tinfo->{combination};

  # Save savedir  path for server
  $tinfo->{savedir}= $save_dir;

  mkpath($save_dir) if ! -d $save_dir;

  # Save the used my.cnf file
  copy($path_config_file, $save_dir);

  # Copy the tmp dir
  copytree("$opt_vardir/tmp/", "$save_dir/tmp/");

  if ( clusters() ) {
    foreach my $cluster ( clusters() ) {
      my $cluster_dir= "$opt_vardir/".$cluster->{name};

      # Remove the fileystem of each ndbd
      foreach my $ndbd ( in_cluster($cluster, ndbds()) )
      {
        my $ndbd_datadir= $ndbd->value("DataDir");
        remove_ndbfs_from_ndbd_datadir($ndbd_datadir);
      }

      save_datadir_after_failure($cluster_dir, $save_dir);
    }
  }
  else {
    foreach my $mysqld ( mysqlds() ) {
      my $data_dir= $mysqld->value('datadir');
      save_datadir_after_failure(dirname($data_dir), $save_dir);
    }
  }
}


sub report_failure_and_restart ($) {
  my $tinfo= shift;

  stop_all_servers();

  $tinfo->{'result'}= 'MTR_RES_FAILED';

  my $test_failures= $tinfo->{'failures'} || 0;
  $tinfo->{'failures'}=  $test_failures + 1;


  if ( $tinfo->{comment} )
  {
    # The test failure has been detected by mysql-test-run.pl
    # when starting the servers or due to other error, the reason for
    # failing the test is saved in "comment"
    ;
  }

  if ( !defined $tinfo->{logfile} )
  {
    my $logfile= $path_current_testlog;
    if ( defined $logfile )
    {
      if ( -f $logfile )
      {
	# Test failure was detected by test tool and its report
	# about what failed has been saved to file. Save the report
	# in tinfo
	$tinfo->{logfile}= mtr_fromfile($logfile);
      }
      else
      {
	# The test tool report didn't exist, display an
	# error message
	$tinfo->{logfile}= "Could not open test tool report '$logfile'";
      }
    }
  }

  after_failure($tinfo);

  mtr_report_test($tinfo);

}


sub run_sh_script {
  my ($script)= @_;

  return 0 unless defined $script;

  mtr_verbose("Running '$script'");
  my $ret= system("/bin/sh $script") >> 8;
  return $ret;
}


sub mysqld_stop {
  my $mysqld= shift or die "usage: mysqld_stop(<mysqld>)";

  my $args;
  mtr_init_args(\$args);

  mtr_add_arg($args, "--no-defaults");
  mtr_add_arg($args, "--character-sets-dir=%s", $mysqld->value('character-sets-dir'));
  mtr_add_arg($args, "--user=%s", $opt_user);
  mtr_add_arg($args, "--password=");
  mtr_add_arg($args, "--port=%d", $mysqld->value('port'));
  mtr_add_arg($args, "--host=%s", $mysqld->value('#host'));
  mtr_add_arg($args, "--connect_timeout=20");
  mtr_add_arg($args, "--protocol=tcp");

  mtr_add_arg($args, "shutdown");

  My::SafeProcess->run
    (
     name          => "mysqladmin shutdown ".$mysqld->name(),
     path          => $exe_mysqladmin,
     args          => \$args,
     error         => "/dev/null",

    );
}


sub mysqld_arguments ($$$) {
  my $args=              shift;
  my $mysqld=            shift;
  my $extra_opts=        shift;

  mtr_add_arg($args, "--defaults-file=%s",  $path_config_file);

  # When mysqld is run by a root user(euid is 0), it will fail
  # to start unless we specify what user to run as, see BUG#30630
  my $euid= $>;
  if (!IS_WINDOWS and $euid == 0 and
      (grep(/^--user/, @$extra_opts)) == 0) {
    mtr_add_arg($args, "--user=root");
  }

  if ( $opt_valgrind_mysqld )
  {
    mtr_add_arg($args, "--skip-safemalloc");

    if ( $mysql_version_id < 50100 )
    {
      mtr_add_arg($args, "--skip-bdb");
    }
  }

  if ( $mysql_version_id >= 50106 )
  {
    # Turn on logging to file
    mtr_add_arg($args, "--log-output=file");
  }

  # Check if "extra_opt" contains skip-log-bin
  my $skip_binlog= grep(/^(--|--loose-)skip-log-bin/, @$extra_opts);

  # Indicate to mysqld it will be debugged in debugger
  if ( $glob_debugger )
  {
    mtr_add_arg($args, "--gdb");
  }

  my $found_skip_core= 0;
  foreach my $arg ( @$extra_opts )
  {
    # Allow --skip-core-file to be set in <testname>-[master|slave].opt file
    if ($arg eq "--skip-core-file")
    {
      $found_skip_core= 1;
    }
    elsif ($skip_binlog and mtr_match_prefix($arg, "--binlog-format"))
    {
      ; # Dont add --binlog-format when running without binlog
    }
    elsif ($arg eq "--loose-skip-log-bin" and
           $mysqld->option("log-slave-updates"))
    {
      ; # Dont add --skip-log-bin when mysqld have --log-slave-updates in config
    }
    else
    {
      mtr_add_arg($args, "%s", $arg);
    }
  }
  $opt_skip_core = $found_skip_core;
  if ( !$found_skip_core )
  {
    mtr_add_arg($args, "%s", "--core-file");
  }

  return $args;
}



sub mysqld_start ($$) {
  my $mysqld=            shift;
  my $extra_opts=        shift;

  mtr_verbose(My::Options::toStr("mysqld_start", @$extra_opts));

  my $exe= find_mysqld($mysqld->value('basedir'));
  my $wait_for_pid_file= 1;

  mtr_error("Internal error: mysqld should never be started for embedded")
    if $opt_embedded_server;

  my $args;
  mtr_init_args(\$args);

  if ( $opt_valgrind_mysqld )
  {
    valgrind_arguments($args, \$exe);
  }

  mtr_add_arg($args, "--defaults-group-suffix=%s", $mysqld->after('mysqld'));
  mysqld_arguments($args,$mysqld,$extra_opts);

  if ( $opt_debug )
  {
    mtr_add_arg($args, "--debug=d:t:i:A,%s/log/%s.trace",
		$path_vardir_trace, $mysqld->name());
  }

  if (IS_WINDOWS)
  {
    # Trick the server to send output to stderr, with --console
    mtr_add_arg($args, "--console");
  }

  if ( $opt_gdb || $opt_manual_gdb )
  {
    gdb_arguments(\$args, \$exe, $mysqld->name());
  }
  elsif ( $opt_ddd || $opt_manual_ddd )
  {
    ddd_arguments(\$args, \$exe, $mysqld->name());
  }
  elsif ( $opt_debugger )
  {
    debugger_arguments(\$args, \$exe, $mysqld->name());
  }
  elsif ( $opt_manual_debug )
  {
     print "\nStart " .$mysqld->name()." in your debugger\n" .
           "dir: $glob_mysql_test_dir\n" .
           "exe: $exe\n" .
	   "args:  " . join(" ", @$args)  . "\n\n" .
	   "Waiting ....\n";

     # Indicate the exe should not be started
    $exe= undef;
  }
  else
  {
    # Default to not wait until pid file has been created
    $wait_for_pid_file= 0;
  }

  # Remove the old pidfile if any
  unlink($mysqld->value('pid-file'));

  my $output= $mysqld->value('#log-error');
  if ( $opt_valgrind and $opt_debug )
  {
    # When both --valgrind and --debug is selected, send
    # all output to the trace file, making it possible to
    # see the exact location where valgrind complains
    $output= "$opt_vardir/log/".$mysqld->name().".trace";
  }

  if ( defined $exe )
  {
    $mysqld->{'proc'}= My::SafeProcess->new
      (
       name          => $mysqld->name(),
       path          => $exe,
       args          => \$args,
       output        => $output,
       error         => $output,
       append        => 1,
       verbose       => $opt_verbose,
       nocore        => $opt_skip_core,
       host          => undef,
       shutdown      => sub { mysqld_stop($mysqld) },
      );
    mtr_verbose("Started $mysqld->{proc}");
  }

  if ( $wait_for_pid_file &&
       !sleep_until_file_created($mysqld->value('pid-file'),
				 $opt_start_timeout,
				 $mysqld->{'proc'}))
  {
    my $mname= $mysqld->name();
    mtr_error("Failed to start mysqld $mname with command $exe");
  }

  # Remember options used when starting
  $mysqld->{'started_opts'}= $extra_opts;

  return;
}


sub stop_all_servers () {
  my $shutdown_timeout = $_[0] or 0;

  mtr_verbose("Stopping all servers...");

  # Kill all started servers
  My::SafeProcess::shutdown($shutdown_timeout,
			    started(all_servers()));

  # Remove pidfiles
  foreach my $server ( all_servers() )
  {
    my $pid_file= $server->if_exist('pid-file');
    unlink($pid_file) if defined $pid_file;
  }

  # Mark servers as stopped
  map($_->{proc}= undef, all_servers());

}


# Find out if server should be restarted for this test
sub server_need_restart {
  my ($tinfo, $server)= @_;

  if ( using_extern() )
  {
    mtr_verbose_restart($server, "no restart for --extern server");
    return 0;
  }

  if ( $tinfo->{'force_restart'} ) {
    mtr_verbose_restart($server, "forced in .opt file");
    return 1;
  }

  if ( $tinfo->{template_path} ne $current_config_name)
  {
    mtr_verbose_restart($server, "using different config file");
    return 1;
  }

  if ( $tinfo->{'master_sh'}  || $tinfo->{'slave_sh'} )
  {
    mtr_verbose_restart($server, "sh script to run");
    return 1;
  }

  if ( ! started($server) )
  {
    mtr_verbose_restart($server, "not started");
    return 1;
  }

  my $started_tinfo= $server->{'started_tinfo'};
  if ( defined $started_tinfo )
  {

    # Check if timezone of  test that server was started
    # with differs from timezone of next test
    if ( timezone($started_tinfo) ne timezone($tinfo) )
    {
      mtr_verbose_restart($server, "different timezone");
      return 1;
    }
  }

  # Temporary re-enable the "always restart slave" hack
  # this should be removed asap, but will require that each rpl
  # testcase cleanup better after itself - ie. stop and reset
  # replication
  # Use the "#!use-slave-opt" marker to detect that this is a "slave"
  # server
  if ( $server->option("#!use-slave-opt") ){
    mtr_verbose_restart($server, "Always restart slave(s)");
    return 1;
  }

  my $is_mysqld= grep ($server eq $_, mysqlds());
  if ($is_mysqld)
  {

    # Check that running process was started with same options
    # as the current test requires
    my $extra_opts= get_extra_opts($server, $tinfo);
    my $started_opts= $server->{'started_opts'};

    if (!My::Options::same($started_opts, $extra_opts) )
    {
      my $use_dynamic_option_switch= 0;
      if (!$use_dynamic_option_switch)
      {
	mtr_verbose_restart($server, "running with different options '" .
			    join(" ", @{$extra_opts}) . "' != '" .
			    join(" ", @{$started_opts}) . "'" );
	return 1;
      }

      mtr_verbose(My::Options::toStr("started_opts", @$started_opts));
      mtr_verbose(My::Options::toStr("extra_opts", @$extra_opts));

      # Get diff and check if dynamic switch is possible
      my @diff_opts= My::Options::diff($started_opts, $extra_opts);
      mtr_verbose(My::Options::toStr("diff_opts", @diff_opts));

      my $query= My::Options::toSQL(@diff_opts);
      mtr_verbose("Attempting dynamic switch '$query'");
      if (run_query($tinfo, $server, $query)){
	mtr_verbose("Restart: running with different options '" .
		    join(" ", @{$extra_opts}) . "' != '" .
		    join(" ", @{$started_opts}) . "'" );
	return 1;
      }

      # Remember the dynamically set options
      $server->{'started_opts'}= $extra_opts;
    }
  }

  # Default, no restart
  return 0;
}


sub servers_need_restart($) {
  my ($tinfo)= @_;
  return grep { server_need_restart($tinfo, $_); } all_servers();
}



#
# Return list of specific servers
#  - there is no servers in an empty config
#
sub _like   { return $config ? $config->like($_[0]) : (); }
sub mysqlds { return _like('mysqld.'); }
sub ndbds   { return _like('cluster_config.ndbd.');}
sub ndb_mgmds { return _like('cluster_config.ndb_mgmd.'); }
sub clusters  { return _like('mysql_cluster.'); }
sub all_servers { return ( mysqlds(), ndb_mgmds(), ndbds() ); }


#
# Filter a list of servers and return only those that are part
# of the specified cluster
#
sub in_cluster {
  my ($cluster)= shift;
  # Return only processes for a specific cluster
  return grep { $_->suffix() eq $cluster->suffix() } @_;
}



#
# Filter a list of servers and return the SafeProcess
# for only those that are started or stopped
#
sub started { return grep(defined $_, map($_->{proc}, @_));  }
sub stopped { return grep(!defined $_, map($_->{proc}, @_)); }


sub envsubst {
  my $string= shift;

  if ( ! defined $ENV{$string} )
  {
    mtr_error(".opt file references '$string' which is not set");
  }

  return $ENV{$string};
}


sub get_extra_opts {
  my ($mysqld, $tinfo)= @_;

  my $opts=
    $mysqld->option("#!use-slave-opt") ?
      $tinfo->{slave_opt} : $tinfo->{master_opt};

  # Expand environment variables
  foreach my $opt ( @$opts )
  {
    $opt =~ s/\$\{(\w+)\}/envsubst($1)/ge;
    $opt =~ s/\$(\w+)/envsubst($1)/ge;
  }
  return $opts;
}


sub stop_servers($$) {
  my ($tinfo, @servers)= @_;

  # Remember if we restarted for this test case (count restarts)
  $tinfo->{'restarted'}= 1;

  if ( join('|', @servers) eq join('|', all_servers()) )
  {
    # All servers are going down, use some kind of order to
    # avoid too many warnings in the log files

   mtr_report("Restarting all servers");

    #  mysqld processes
    My::SafeProcess::shutdown( $opt_shutdown_timeout, started(mysqlds()) );

    # cluster processes
    My::SafeProcess::shutdown( $opt_shutdown_timeout,
			       started(ndbds(), ndb_mgmds()) );
  }
  else
  {
    mtr_report("Restarting ", started(@servers));

     # Stop only some servers
    My::SafeProcess::shutdown( $opt_shutdown_timeout,
			       started(@servers) );
  }

  foreach my $server (@servers)
  {
    # Mark server as stopped
    $server->{proc}= undef;

    # Forget history
    delete $server->{'started_tinfo'};
    delete $server->{'started_opts'};
    delete $server->{'started_cnf'};
  }
}


#
# start_servers
#
# Start servers not already started
#
# RETURN
#  0 OK
#  1 Start failed
#
sub start_servers($) {
  my ($tinfo)= @_;

  # Start clusters
  foreach my $cluster ( clusters() )
  {
    ndbcluster_start($cluster);
  }

  # Start mysqlds
  foreach my $mysqld ( mysqlds() )
  {
    if ( $mysqld->{proc} )
    {
      # Already started

      # Write start of testcase to log file
      mark_log($mysqld->value('#log-error'), $tinfo);

      next;
    }

    my $datadir= $mysqld->value('datadir');
    if ($opt_start_dirty)
    {
      # Don't delete anything if starting dirty
      ;
    }
    else
    {

      my @options= ('log-bin', 'relay-log');
      foreach my $option_name ( @options )  {
	next unless $mysqld->option($option_name);

	my $file_name= $mysqld->value($option_name);
	next unless
	  defined $file_name and
	    -e $file_name;

	mtr_debug(" -removing '$file_name'");
	unlink($file_name) or die ("unable to remove file '$file_name'");
      }

      if (-d $datadir ) {
	mtr_verbose(" - removing '$datadir'");
	rmtree($datadir);
      }
    }

    my $mysqld_basedir= $mysqld->value('basedir');
    if ( $basedir eq $mysqld_basedir )
    {
      if (! $opt_start_dirty)	# If dirty, keep possibly grown system db
      {
	# Copy datadir from installed system db
	for my $path ( "$opt_vardir", "$opt_vardir/..") {
	  my $install_db= "$path/install.db";
	  copytree($install_db, $datadir)
	    if -d $install_db;
	}
	mtr_error("Failed to copy system db to '$datadir'")
	  unless -d $datadir;
      }
    }
    else
    {
      mysql_install_db($mysqld); # For versional testing

      mtr_error("Failed to install system db to '$datadir'")
	unless -d $datadir;

    }

    # Create the servers tmpdir
    my $tmpdir= $mysqld->value('tmpdir');
    mkpath($tmpdir) unless -d $tmpdir;

    # Write start of testcase to log file
    mark_log($mysqld->value('#log-error'), $tinfo);

    # Run <tname>-master.sh
    if ($mysqld->option('#!run-master-sh') and
       run_sh_script($tinfo->{master_sh}) )
    {
      $tinfo->{'comment'}= "Failed to execute '$tinfo->{master_sh}'";
      return 1;
    }

    # Run <tname>-slave.sh
    if ($mysqld->option('#!run-slave-sh') and
	run_sh_script($tinfo->{slave_sh}))
    {
      $tinfo->{'comment'}= "Failed to execute '$tinfo->{slave_sh}'";
      return 1;
    }

    if (!$opt_embedded_server)
    {
      my $extra_opts= get_extra_opts($mysqld, $tinfo);
      mysqld_start($mysqld,$extra_opts);

      # Save this test case information, so next can examine it
      $mysqld->{'started_tinfo'}= $tinfo;
    }

  }

  # Wait for clusters to start
  foreach my $cluster ( clusters() )
  {
    if (ndbcluster_wait_started($cluster, ""))
    {
      # failed to start
      $tinfo->{'comment'}= "Start of '".$cluster->name()."' cluster failed";
      return 1;
    }
  }

  # Wait for mysqlds to start
  foreach my $mysqld ( mysqlds() )
  {
    next if !started($mysqld);

    if (sleep_until_file_created($mysqld->value('pid-file'),
				 $opt_start_timeout,
				 $mysqld->{'proc'}) == 0) {
      $tinfo->{comment}=
	"Failed to start ".$mysqld->name();

      my $logfile= $mysqld->value('#log-error');
      if ( defined $logfile and -f $logfile )
      {
        my @srv_lines= extract_server_log($logfile, $tinfo->{name});
	$tinfo->{logfile}= "Server log is:\n" . join ("", @srv_lines);
      }
      else
      {
	$tinfo->{logfile}= "Could not open server logfile: '$logfile'";
      }
      return 1;
    }
  }
  return 0;
}


#
# Run include/check-testcase.test
# Before a testcase, run in record mode and save result file to var/tmp
# After testcase, run and compare with the recorded file, they should be equal!
#
# RETURN VALUE
#  The newly started process
#
sub start_check_testcase ($$$) {
  my $tinfo=    shift;
  my $mode=     shift;
  my $mysqld=   shift;

  my $name= "check-".$mysqld->name();
  # Replace dots in name with underscore to avoid that mysqltest
  # misinterpret's what the filename extension is :(
  $name=~ s/\./_/g;

  my $args;
  mtr_init_args(\$args);

  mtr_add_arg($args, "--defaults-file=%s", $path_config_file);
  mtr_add_arg($args, "--defaults-group-suffix=%s", $mysqld->after('mysqld'));

  mtr_add_arg($args, "--skip-safemalloc");

  mtr_add_arg($args, "--result-file=%s", "$opt_vardir/tmp/$name.result");
  mtr_add_arg($args, "--test-file=%s", "include/check-testcase.test");
  mtr_add_arg($args, "--verbose");

  if ( $mode eq "before" )
  {
    mtr_add_arg($args, "--record");
  }
  my $errfile= "$opt_vardir/tmp/$name.err";
  my $proc= My::SafeProcess->new
    (
     name          => $name,
     path          => $exe_mysqltest,
     error         => $errfile,
     output        => $errfile,
     args          => \$args,
     user_data     => $errfile,
     verbose       => $opt_verbose,
    );

  mtr_report("Started $proc");
  return $proc;
}


sub run_mysqltest ($) {
  my $proc= start_mysqltest(@_);
  $proc->wait();
}


sub start_mysqltest ($) {
  my ($tinfo)= @_;
  my $exe= $exe_mysqltest;
  my $args;

  mtr_init_args(\$args);

  mtr_add_arg($args, "--defaults-file=%s", $path_config_file);
  mtr_add_arg($args, "--silent");
  mtr_add_arg($args, "--skip-safemalloc");
  mtr_add_arg($args, "--tmpdir=%s", $opt_tmpdir);
  mtr_add_arg($args, "--character-sets-dir=%s", $path_charsetsdir);
  mtr_add_arg($args, "--logdir=%s/log", $opt_vardir);

  # Log line number and time  for each line in .test file
  mtr_add_arg($args, "--mark-progress")
    if $opt_mark_progress;

  mtr_add_arg($args, "--database=test");

  if ( $opt_ps_protocol )
  {
    mtr_add_arg($args, "--ps-protocol");
  }

  if ( $opt_sp_protocol )
  {
    mtr_add_arg($args, "--sp-protocol");
  }

  if ( $opt_view_protocol )
  {
    mtr_add_arg($args, "--view-protocol");
  }

  if ( $opt_cursor_protocol )
  {
    mtr_add_arg($args, "--cursor-protocol");
  }

  if ( $opt_strace_client )
  {
    $exe=  $opt_strace_client || "strace";
    mtr_add_arg($args, "-o");
    mtr_add_arg($args, "%s/log/mysqltest.strace", $opt_vardir);
    mtr_add_arg($args, "$exe_mysqltest");
  }

  mtr_add_arg($args, "--timer-file=%s/log/timer", $opt_vardir);

  if ( $opt_compress )
  {
    mtr_add_arg($args, "--compress");
  }

  if ( $opt_sleep )
  {
    mtr_add_arg($args, "--sleep=%d", $opt_sleep);
  }

  if ( $opt_ssl )
  {
    # Turn on SSL for _all_ test cases if option --ssl was used
    mtr_add_arg($args, "--ssl");
  }

  if ( $opt_embedded_server )
  {

    # Get the args needed for the embedded server
    # and append them to args prefixed
    # with --sever-arg=

    my $mysqld=  $config->group('embedded')
      or mtr_error("Could not get [embedded] section");

    my $mysqld_args;
    mtr_init_args(\$mysqld_args);
    my $extra_opts= get_extra_opts($mysqld, $tinfo);
    mysqld_arguments($mysqld_args, $mysqld, $extra_opts);
    mtr_add_arg($args, "--server-arg=%s", $_) for @$mysqld_args;

    if (IS_WINDOWS)
    {
      # Trick the server to send output to stderr, with --console
      mtr_add_arg($args, "--server-arg=--console");
    }
  }

  # ----------------------------------------------------------------------
  # export MYSQL_TEST variable containing <path>/mysqltest <args>
  # ----------------------------------------------------------------------
  $ENV{'MYSQL_TEST'}= mtr_args2str($exe_mysqltest, @$args);

  # ----------------------------------------------------------------------
  # Add arguments that should not go into the MYSQL_TEST env var
  # ----------------------------------------------------------------------
  if ( $opt_valgrind_mysqltest )
  {
    # Prefix the Valgrind options to the argument list.
    # We do this here, since we do not want to Valgrind the nested invocations
    # of mysqltest; that would mess up the stderr output causing test failure.
    my @args_saved = @$args;
    mtr_init_args(\$args);
    valgrind_arguments($args, \$exe);
    mtr_add_arg($args, "%s", $_) for @args_saved;
  }

  mtr_add_arg($args, "--test-file=%s", $tinfo->{'path'});

  # Number of lines of resut to include in failure report
  mtr_add_arg($args, "--tail-lines=20");

  if ( defined $tinfo->{'result_file'} ) {
    mtr_add_arg($args, "--result-file=%s", $tinfo->{'result_file'});
  }

  client_debug_arg($args, "mysqltest");

  if ( $opt_record )
  {
    mtr_add_arg($args, "--record");

    # When recording to a non existing result file
    # the name of that file is in "record_file"
    if ( defined $tinfo->{'record_file'} ) {
      mtr_add_arg($args, "--result-file=%s", $tinfo->{record_file});
    }
  }

  if ( $opt_client_gdb )
  {
    gdb_arguments(\$args, \$exe, "client");
  }
  elsif ( $opt_client_ddd )
  {
    ddd_arguments(\$args, \$exe, "client");
  }
  elsif ( $opt_client_debugger )
  {
    debugger_arguments(\$args, \$exe, "client");
  }

  my $proc= My::SafeProcess->new
    (
     name          => "mysqltest",
     path          => $exe,
     args          => \$args,
     append        => 1,
     error         => $path_current_testlog,
     verbose       => $opt_verbose,
    );
  mtr_verbose("Started $proc");
  return $proc;
}


#
# Modify the exe and args so that program is run in gdb in xterm
#
sub gdb_arguments {
  my $args= shift;
  my $exe=  shift;
  my $type= shift;

  # Write $args to gdb init file
  my $str= join(" ", @$$args);
  my $gdb_init_file= "$opt_vardir/tmp/gdbinit.$type";

  # Remove the old gdbinit file
  unlink($gdb_init_file);

  if ( $type eq "client" )
  {
    # write init file for client
    mtr_tofile($gdb_init_file,
	       "set args $str\n" .
	       "break main\n");
  }
  else
  {
    # write init file for mysqld
    mtr_tofile($gdb_init_file,
	       "set args $str\n" .
	       "break mysql_parse\n" .
	       "commands 1\n" .
	       "disable 1\n" .
	       "end\n" .
	       "run");
  }

  if ( $opt_manual_gdb )
  {
     print "\nTo start gdb for $type, type in another window:\n";
     print "gdb -cd $glob_mysql_test_dir -x $gdb_init_file $$exe\n";

     # Indicate the exe should not be started
     $$exe= undef;
     return;
  }

  $$args= [];
  mtr_add_arg($$args, "-title");
  mtr_add_arg($$args, "$type");
  mtr_add_arg($$args, "-e");

  if ( $exe_libtool )
  {
    mtr_add_arg($$args, $exe_libtool);
    mtr_add_arg($$args, "--mode=execute");
  }

  mtr_add_arg($$args, "gdb");
  mtr_add_arg($$args, "-x");
  mtr_add_arg($$args, "$gdb_init_file");
  mtr_add_arg($$args, "$$exe");

  $$exe= "xterm";
}


#
# Modify the exe and args so that program is run in ddd
#
sub ddd_arguments {
  my $args= shift;
  my $exe=  shift;
  my $type= shift;

  # Write $args to ddd init file
  my $str= join(" ", @$$args);
  my $gdb_init_file= "$opt_vardir/tmp/gdbinit.$type";

  # Remove the old gdbinit file
  unlink($gdb_init_file);

  if ( $type eq "client" )
  {
    # write init file for client
    mtr_tofile($gdb_init_file,
	       "set args $str\n" .
	       "break main\n");
  }
  else
  {
    # write init file for mysqld
    mtr_tofile($gdb_init_file,
	       "file $$exe\n" .
	       "set args $str\n" .
	       "break mysql_parse\n" .
	       "commands 1\n" .
	       "disable 1\n" .
	       "end");
  }

  if ( $opt_manual_ddd )
  {
     print "\nTo start ddd for $type, type in another window:\n";
     print "ddd -cd $glob_mysql_test_dir -x $gdb_init_file $$exe\n";

     # Indicate the exe should not be started
     $$exe= undef;
     return;
  }

  my $save_exe= $$exe;
  $$args= [];
  if ( $exe_libtool )
  {
    $$exe= $exe_libtool;
    mtr_add_arg($$args, "--mode=execute");
    mtr_add_arg($$args, "ddd");
  }
  else
  {
    $$exe= "ddd";
  }
  mtr_add_arg($$args, "--command=$gdb_init_file");
  mtr_add_arg($$args, "$save_exe");
}


#
# Modify the exe and args so that program is run in the selected debugger
#
sub debugger_arguments {
  my $args= shift;
  my $exe=  shift;
  my $debugger= $opt_debugger || $opt_client_debugger;

  if ( $debugger =~ /vcexpress|vc|devenv/ )
  {
    # vc[express] /debugexe exe arg1 .. argn

    # Add /debugexe and name of the exe before args
    unshift(@$$args, "/debugexe");
    unshift(@$$args, "$$exe");

    # Set exe to debuggername
    $$exe= $debugger;

  }
  elsif ( $debugger =~ /windbg/ )
  {
    # windbg exe arg1 .. argn

    # Add name of the exe before args
    unshift(@$$args, "$$exe");

    # Set exe to debuggername
    $$exe= $debugger;

  }
  elsif ( $debugger eq "dbx" )
  {
    # xterm -e dbx -r exe arg1 .. argn

    unshift(@$$args, $$exe);
    unshift(@$$args, "-r");
    unshift(@$$args, $debugger);
    unshift(@$$args, "-e");

    $$exe= "xterm";

  }
  else
  {
    mtr_error("Unknown argument \"$debugger\" passed to --debugger");
  }
}


#
# Modify the exe and args so that program is run in valgrind
#
sub valgrind_arguments {
  my $args= shift;
  my $exe=  shift;

  if ( $opt_callgrind)
  {
    mtr_add_arg($args, "--tool=callgrind");
    mtr_add_arg($args, "--base=$opt_vardir/log");
  }
  else
  {
    mtr_add_arg($args, "--tool=memcheck"); # From >= 2.1.2 needs this option
    mtr_add_arg($args, "--leak-check=yes");
    mtr_add_arg($args, "--num-callers=16");
    mtr_add_arg($args, "--suppressions=%s/valgrind.supp", $glob_mysql_test_dir)
      if -f "$glob_mysql_test_dir/valgrind.supp";
  }

  # Add valgrind options, can be overriden by user
  mtr_add_arg($args, '%s', $_) for (@valgrind_args);

  mtr_add_arg($args, $$exe);

  $$exe= $opt_valgrind_path || "valgrind";

  if ($exe_libtool)
  {
    # Add "libtool --mode-execute" before the test to execute
    # if running in valgrind(to avoid valgrinding bash)
    unshift(@$args, "--mode=execute", $$exe);
    $$exe= $exe_libtool;
  }
}


#
# Usage
#
sub usage ($) {
  my ($message)= @_;

  if ( $message )
  {
    print STDERR "$message\n";
  }

  print <<HERE;

$0 [ OPTIONS ] [ TESTCASE ]

Options to control what engine/variation to run

  embedded-server       Use the embedded server, i.e. no mysqld daemons
  ps-protocol           Use the binary protocol between client and server
  cursor-protocol       Use the cursor protocol between client and server
                        (implies --ps-protocol)
  view-protocol         Create a view to execute all non updating queries
  sp-protocol           Create a stored procedure to execute all queries
  compress              Use the compressed protocol between client and server
  ssl                   Use ssl protocol between client and server
  skip-ssl              Dont start server with support for ssl connections
  vs-config             Visual Studio configuration used to create executables
                        (default: MTR_VS_CONFIG environment variable)

  defaults-file=<config template> Use fixed config template for all
                        tests
  defaults_extra_file=<config template> Extra config template to add to
                        all generated configs
  combination=<opt>     Use at least twice to run tests with specified 
                        options to mysqld
  skip-combinations     Ignore combination file (or options)

Options to control directories to use
  tmpdir=DIR            The directory where temporary files are stored
                        (default: ./var/tmp).
  vardir=DIR            The directory where files generated from the test run
                        is stored (default: ./var). Specifying a ramdisk or
                        tmpfs will speed up tests.
  mem                   Run testsuite in "memory" using tmpfs or ramdisk
                        Attempts to find a suitable location
                        using a builtin list of standard locations
                        for tmpfs (/dev/shm)
                        The option can also be set using environment
                        variable MTR_MEM=[DIR]
  client-bindir=PATH    Path to the directory where client binaries are located
  client-libdir=PATH    Path to the directory where client libraries are located


Options to control what test suites or cases to run

  force                 Continue to run the suite after failure
  with-ndbcluster-only  Run only tests that include "ndb" in the filename
  skip-ndb[cluster]     Skip all tests that need cluster
  do-test=PREFIX or REGEX
                        Run test cases which name are prefixed with PREFIX
                        or fulfills REGEX
  skip-test=PREFIX or REGEX
                        Skip test cases which name are prefixed with PREFIX
                        or fulfills REGEX
  start-from=PREFIX     Run test cases starting test prefixed with PREFIX where
                        prefix may be suite.testname or just testname
  suite[s]=NAME1,..,NAMEN
                        Collect tests in suites from the comma separated
                        list of suite names.
                        The default is: "$DEFAULT_SUITES"
  skip-rpl              Skip the replication test cases.
  big-test              Also run tests marked as "big"
  enable-disabled       Run also tests marked as disabled
  print-testcases       Don't run the tests but print details about all the
                        selected tests, in the order they would be run.

Options that specify ports

  mtr-build-thread=#    Specify unique number to calculate port number(s) from.
  build-thread=#        Can be set in environment variable MTR_BUILD_THREAD.
                        Set  MTR_BUILD_THREAD="auto" to automatically aquire
                        a build thread id that is unique to current host

Options for test case authoring

  record TESTNAME       (Re)genereate the result file for TESTNAME
  check-testcases       Check testcases for sideeffects
  mark-progress         Log line number and elapsed time to <testname>.progress

Options that pass on options

  mysqld=ARGS           Specify additional arguments to "mysqld"

Options to run test on running server

  extern option=value   Run only the tests against an already started server
                        the options to use for connection to the extern server
                        must be specified using name-value pair notation
                        For example:
                         ./$0 --extern socket=/tmp/mysqld.sock

Options for debugging the product

  client-ddd            Start mysqltest client in ddd
  client-debugger=NAME  Start mysqltest in the selected debugger
  client-gdb            Start mysqltest client in gdb
  ddd                   Start mysqld in ddd
  debug                 Dump trace output for all servers and client programs
  debugger=NAME         Start mysqld in the selected debugger
  gdb                   Start the mysqld(s) in gdb
  manual-debug          Let user manually start mysqld in debugger, before
                        running test(s)
  manual-gdb            Let user manually start mysqld in gdb, before running
                        test(s)
  manual-ddd            Let user manually start mysqld in ddd, before running
                        test(s)
  strace-client=[path]  Create strace output for mysqltest client, optionally
                        specifying name and path to the trace program to use.
                        Example: $0 --strace-client=ktrace
  max-save-core         Limit the number of core files saved (to avoid filling
                        up disks for heavily crashing server). Defaults to
                        $opt_max_save_core, set to 0 for no limit. Set
                        it's default with MTR_MAX_SAVE_CORE
  max-save-datadir      Limit the number of datadir saved (to avoid filling
                        up disks for heavily crashing server). Defaults to
                        $opt_max_save_datadir, set to 0 for no limit. Set
                        it's default with MTR_MAX_SAVE_DATDIR
  max-test-fail         Limit the number of test failurs before aborting
                        the current test run. Defaults to
                        $opt_max_test_fail, set to 0 for no limit. Set
                        it's default with MTR_MAX_TEST_FAIL

Options for valgrind

  valgrind              Run the "mysqltest" and "mysqld" executables using
                        valgrind with default options
  valgrind-all          Synonym for --valgrind
  valgrind-mysqltest    Run the "mysqltest" and "mysql_client_test" executable
                        with valgrind
  valgrind-mysqld       Run the "mysqld" executable with valgrind
  valgrind-options=ARGS Deprecated, use --valgrind-option
  valgrind-option=ARGS  Option to give valgrind, replaces default option(s),
                        can be specified more then once
  valgrind-path=<EXE>   Path to the valgrind executable
  callgrind             Instruct valgrind to use callgrind

Misc options
  user=USER             User for connecting to mysqld(default: $opt_user)
  comment=STR           Write STR to the output
  notimer               Don't show test case execution time
  verbose               More verbose output(use multiple times for even more)
  verbose-restart       Write when and why servers are restarted
  start                 Only initialize and start the servers, using the
                        startup settings for the first specified test case
                        Example:
                         $0 --start alias &
  start-dirty           Only start the servers (without initialization) for
                        the first specified test case
  wait-all              If --start or --start-dirty option is used, wait for all
                        servers to exit before finishing the process
  fast                  Run as fast as possible, dont't wait for servers
                        to shutdown etc.
  parallel=N            Run tests in N parallel threads (default=1)
                        Use parallel=auto for auto-setting of N
  repeat=N              Run each test N number of times
  retry=N               Retry tests that fail N times, limit number of failures
                        to $opt_retry_failure
  retry-failure=N       Limit number of retries for a failed test
  reorder               Reorder tests to get fewer server restarts
  help                  Get this help text

  testcase-timeout=MINUTES Max test case run time (default $opt_testcase_timeout)
  suite-timeout=MINUTES Max test suite run time (default $opt_suite_timeout)
  shutdown-timeout=SECONDS Max number of seconds to wait for server shutdown
                        before killing servers (default $opt_shutdown_timeout)
  warnings              Scan the log files for warnings. Use --nowarnings
                        to turn off.

  sleep=SECONDS         Passed to mysqltest, will be used as fixed sleep time
  gcov                  Collect coverage information after the test.
                        The result is a gcov file per source and header file.
  experimental=<file>   Refer to list of tests considered experimental;
                        failures will be marked exp-fail instead of fail.
  report-features       First run a "test" that reports mysql features
  timestamp             Print timestamp before each test report line
  timediff              With --timestamp, also print time passed since
                        *previous* test started

HERE
  exit(1);

}

