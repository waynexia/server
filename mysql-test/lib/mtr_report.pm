# -*- cperl -*-
# Copyright (C) 2004-2006 MySQL AB
# 
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

# This is a library file used by the Perl version of mysql-test-run,
# and is part of the translation of the Bourne shell script with the
# same name.

package mtr_report;
use strict;

use base qw(Exporter);
our @EXPORT= qw(report_option mtr_print_line mtr_print_thick_line
		mtr_print_header mtr_report mtr_report_stats
		mtr_warning mtr_error mtr_debug mtr_verbose
		mtr_verbose_restart mtr_report_test_passed
		mtr_report_test_failed mtr_report_test_skipped
		mtr_report_stats);

require "mtr_io.pl";

my $tot_real_time= 0;

our $timestamp= 0;

sub report_option {
  my ($opt, $value)= @_;

  # Convert - to _ in option name
  $opt =~ s/-/_/;
  no strict 'refs';
  ${$opt}= $value;
}

sub SHOW_SUITE_NAME() { return  1; };

sub _mtr_report_test_name ($) {
  my $tinfo= shift;
  my $tname= $tinfo->{name};

  # Remove suite part of name
  $tname =~ s/.*\.// unless SHOW_SUITE_NAME;

  # Add combination name if any
  $tname.= " '$tinfo->{combination}'"
    if defined $tinfo->{combination};

  print _timestamp();
  printf "%-30s ", $tname;
}


sub mtr_report_test_skipped ($) {
  my $tinfo= shift;
  _mtr_report_test_name($tinfo);

  $tinfo->{'result'}= 'MTR_RES_SKIPPED';
  if ( $tinfo->{'disable'} )
  {
    mtr_report("[ disabled ]  $tinfo->{'comment'}");
  }
  elsif ( $tinfo->{'comment'} )
  {
    if ( $tinfo->{skip_detected_by_test} )
    {
      mtr_report("[ skip.]  $tinfo->{'comment'}");
    }
    else
    {
      mtr_report("[ skip ]  $tinfo->{'comment'}");
    }
  }
  else
  {
    mtr_report("[ skip ]");
  }
}


sub mtr_report_test_passed ($$) {
  my ($tinfo, $use_timer)= @_;
  _mtr_report_test_name($tinfo);

  my $timer=  "";
  if ( $use_timer and -f "$::opt_vardir/log/timer" )
  {
    $timer= mtr_fromfile("$::opt_vardir/log/timer");
    $tot_real_time += ($timer/1000);
    $timer= sprintf "%12s", $timer;
  }
  # Set as passed unless already set
  if ( not defined $tinfo->{'result'} ){
    $tinfo->{'result'}= 'MTR_RES_PASSED';
  }
  mtr_report("[ pass ]   $timer");
}


sub mtr_report_test_failed ($$) {
  my ($tinfo, $logfile)= @_;
  _mtr_report_test_name($tinfo);

  $tinfo->{'result'}= 'MTR_RES_FAILED';
  my $test_failures= $tinfo->{'failures'} || 0;
  $tinfo->{'failures'}=  $test_failures + 1;
  if ( defined $tinfo->{'timeout'} )
  {
    mtr_report("[ fail ]  timeout");
    return;
  }
  else
  {
    mtr_report("[ fail ]");
  }

  if ( $tinfo->{'comment'} )
  {
    # The test failure has been detected by mysql-test-run.pl
    # when starting the servers or due to other error, the reason for
    # failing the test is saved in "comment"
    mtr_report("\nERROR: $tinfo->{'comment'}");
  }
  elsif ( defined $logfile and -f $logfile )
  {
    # Test failure was detected by test tool and its report
    # about what failed has been saved to file. Display the report.
    print "\n";
    mtr_printfile($logfile);
    print "\n";
  }
  else
  {
    # Neither this script or the test tool has recorded info
    # about why the test has failed. Should be debugged.
    mtr_report("\nUnexpected termination, probably when starting mysqld");;
  }
}


sub mtr_report_stats ($) {
  my $tests= shift;

  # ----------------------------------------------------------------------
  # Find out how we where doing
  # ----------------------------------------------------------------------

  my $tot_skiped= 0;
  my $tot_passed= 0;
  my $tot_failed= 0;
  my $tot_tests=  0;
  my $tot_restarts= 0;
  my $found_problems= 0; # Some warnings in the logfiles are errors...

  foreach my $tinfo (@$tests)
  {
    if ( $tinfo->{'result'} eq 'MTR_RES_SKIPPED' )
    {
      $tot_skiped++;
    }
    elsif ( $tinfo->{'result'} eq 'MTR_RES_PASSED' )
    {
      $tot_tests++;
      $tot_passed++;
    }
    elsif ( $tinfo->{'result'} eq 'MTR_RES_FAILED' )
    {
      $tot_tests++;
      $tot_failed++;
    }
    if ( $tinfo->{'restarted'} )
    {
      $tot_restarts++;
    }
  }

  # ----------------------------------------------------------------------
  # Print out a summary report to screen
  # ----------------------------------------------------------------------
  print "The servers were restarted $tot_restarts times\n";

  if ( $::opt_timer )
  {
    use English;

    mtr_report("Spent", sprintf("%.3f", $tot_real_time),"of",
	       time - $BASETIME, "seconds executing testcases");
  }

  # ----------------------------------------------------------------------
  # If a debug run, there might be interesting information inside
  # the "var/log/*.err" files. We save this info in "var/log/warnings"
  # ----------------------------------------------------------------------

  if ( $::opt_warnings )
  {
    # Save and report if there was any fatal warnings/errors in err logs

    my $warnlog= "$::opt_vardir/log/warnings";

    unless ( open(WARN, ">$warnlog") )
    {
      mtr_warning("can't write to the file \"$warnlog\": $!");
    }
    else
    {
      # We report different types of problems in order
      foreach my $pattern ( "^Warning:",
			    "\\[Warning\\]",
			    "\\[ERROR\\]",
			    "^Error:", "^==.* at 0x",
			    "InnoDB: Warning",
			    "^safe_mutex:",
			    "missing DBUG_RETURN",
			    "mysqld: Warning",
			    "allocated at line",
			    "Attempting backtrace", "Assertion .* failed" )
      {
        foreach my $errlog ( sort glob("$::opt_vardir/log/*.err") )
        {
	  my $testname= "";
          unless ( open(ERR, $errlog) )
          {
            mtr_warning("can't read $errlog");
            next;
          }
          my $leak_reports_expected= undef;
          while ( <ERR> )
          {
            # There is a test case that purposely provokes a
            # SAFEMALLOC leak report, even though there is no actual
            # leak. We need to detect this, and ignore the warning in
            # that case.
            if (/Begin safemalloc memory dump:/) {
              $leak_reports_expected= 1;
            } elsif (/End safemalloc memory dump./) {
              $leak_reports_expected= undef;
            }

            # Skip some non fatal warnings from the log files
            if (
		/\"SELECT UNIX_TIMESTAMP\(\)\" failed on master/ or
		/Aborted connection/ or
		/Client requested master to start replication from impossible position/ or
		/Could not find first log file name in binary log/ or
		/Enabling keys got errno/ or
		/Error reading master configuration/ or
		/Error reading packet/ or
		/Event Scheduler/ or
		/Failed to open log/ or
		/Failed to open the existing master info file/ or
		/Forcing shutdown of [0-9]* plugins/ or
		/Got error [0-9]* when reading table/ or
		/Incorrect definition of table/ or
		/Incorrect information in file/ or
		/InnoDB: Warning: we did not need to do crash recovery/ or
		/Invalid \(old\?\) table or database name/ or
		/Lock wait timeout exceeded/ or
		/Log entry on master is longer than max_allowed_packet/ or
                /unknown option '--loose-/ or
                /unknown variable 'loose-/ or
		/You have forced lower_case_table_names to 0 through a command-line option/ or
		/Setting lower_case_table_names=2/ or
		/NDB Binlog:/ or
		/NDB: failed to setup table/ or
		/NDB: only row based binary logging/ or
		/Neither --relay-log nor --relay-log-index were used/ or
		/Query partially completed/ or
		/Slave I.O thread aborted while waiting for relay log/ or
		/Slave SQL thread is stopped because UNTIL condition/ or
		/Slave SQL thread retried transaction/ or
		/Slave \(additional info\)/ or
		/Slave: .*Duplicate column name/ or
		/Slave: .*master may suffer from/ or
		/Slave: According to the master's version/ or
		/Slave: Column [0-9]* type mismatch/ or
		/Slave: Error .* doesn't exist/ or
		/Slave: Error .*Deadlock found/ or
		/Slave: Error .*Unknown table/ or
		/Slave: Error in Write_rows event: / or
		/Slave: Field .* of table .* has no default value/ or
		/Slave: Query caused different errors on master and slave/ or
		/Slave: Table .* doesn't exist/ or
		/Slave: Table width mismatch/ or
		/Slave: The incident LOST_EVENTS occured on the master/ or
		/Slave: Unknown error.* 1105/ or
		/Slave: Can't drop database.* database doesn't exist/ or
                /Slave SQL:.*(?:Error_code: \d+|Query:.*)/ or
		/Sort aborted/ or
		/Time-out in NDB/ or
		/Warning:\s+One can only use the --user.*root/ or
		/Warning:\s+Setting lower_case_table_names=2/ or
		/Warning:\s+Table:.* on (delete|rename)/ or
		/You have an error in your SQL syntax/ or
		/deprecated/ or
		/description of time zone/ or
		/equal MySQL server ids/ or
		/error .*connecting to master/ or
		/error reading log entry/ or
		/lower_case_table_names is set/ or
		/skip-name-resolve mode/ or
		/slave SQL thread aborted/ or
		/Slave: .*Duplicate entry/ or
		# Special case for Bug #26402 in show_check.test
		# Question marks are not valid file name parts
		# on Windows platforms. Ignore this error message. 
		/\QCan't find file: '.\test\????????.frm'\E/ or
		# Special case, made as specific as possible, for:
		# Bug #28436: Incorrect position in SHOW BINLOG EVENTS causes
		#             server coredump
		/\QError in Log_event::read_log_event(): 'Sanity check failed', data_len: 258, event_type: 49\E/ or
                /Statement is not safe to log in statement format/ or

                # Test case for Bug#14233 produces the following warnings:
                /Stored routine 'test'.'bug14233_1': invalid value in column mysql.proc/ or
                /Stored routine 'test'.'bug14233_2': invalid value in column mysql.proc/ or
                /Stored routine 'test'.'bug14233_3': invalid value in column mysql.proc/ or

                # BUG#29807 - innodb_mysql.test: Cannot find table test/t2
                #             from the internal data dictionary
                /Cannot find table test\/bug29807 from the internal data dictionary/ or

                # BUG#29839 - lowercase_table3.test: Cannot find table test/T1
                #             from the internal data dictiona
                /Cannot find table test\/BUG29839 from the internal data dictionary/
	       )
            {
              next;                       # Skip these lines
            }
	    if ( /CURRENT_TEST: (.*)/ )
	    {
	      $testname= $1;
	    }
            if ( /$pattern/ )
            {
              if ($leak_reports_expected) {
                next;
              }
              $found_problems= 1;
              print WARN basename($errlog) . ": $testname: $_";
            }
          }
        }
      }

      if ( $::opt_check_testcases )
      {
        # Look for warnings produced by mysqltest in testname.warnings
        foreach my $test_warning_file
	  ( glob("$::glob_mysql_test_dir/r/*.warnings") )
        {
          $found_problems= 1;
	  print WARN "Check myqltest warnings in $test_warning_file\n";
        }
      }

      if ( $found_problems )
      {
	mtr_warning("Got errors/warnings while running tests, please examine",
		    "\"$warnlog\" for details.");
      }
    }
  }

  print "\n";

  # Print a list of check_testcases that failed(if any)
  if ( $::opt_check_testcases )
  {
    my @check_testcases= ();

    foreach my $tinfo (@$tests)
    {
      if ( defined $tinfo->{'check_testcase_failed'} )
      {
	push(@check_testcases, $tinfo->{'name'});
      }
    }

    if ( @check_testcases )
    {
      print "Check of testcase failed for: ";
      print join(" ", @check_testcases);
      print "\n\n";
    }
  }

  # Print a list of testcases that failed
  if ( $tot_failed != 0 )
  {
    my $ratio=  $tot_passed * 100 / $tot_tests;
    print "Failed $tot_failed/$tot_tests tests, ";
    printf("%.2f", $ratio);
    print "\% were successful.\n\n";

    # Print the list of test that failed in a format
    # that can be copy pasted to rerun only failing tests
    print "Failing test(s):";

    foreach my $tinfo (@$tests)
    {
      if ( $tinfo->{'result'} eq 'MTR_RES_FAILED' )
      {
        print " $tinfo->{'name'}";
      }
    }
    print "\n\n";

    # Print info about reporting the error
    print
      "The log files in var/log may give you some hint of what went wrong.\n\n",
      "If you want to report this error, please read first ",
      "the documentation\n",
      "at http://dev.mysql.com/doc/mysql/en/mysql-test-suite.html\n\n";

   }
  else
  {
    print "All $tot_tests tests were successful.\n";
  }

  if ( $tot_failed != 0 || $found_problems)
  {
    mtr_error("there were failing test cases");
  }
}


##############################################################################
#
#  Text formatting
#
##############################################################################

sub mtr_print_line () {
  print '-' x 60, "\n";
}


sub mtr_print_thick_line {
  my $char= shift || '=';
  print $char x 60, "\n";
}


sub mtr_print_header () {
  print "\n";
  if ( $::opt_timer )
  {
    print "TEST                            RESULT        TIME (ms)\n";
  }
  else
  {
    print "TEST                            RESULT\n";
  }
  mtr_print_line();
  print "\n";
}


##############################################################################
#
#  Log and reporting functions
#
##############################################################################

use Time::localtime;

sub _timestamp {
  return "" unless $timestamp;

  my $tm= localtime();
  return sprintf("%02d%02d%02d %2d:%02d:%02d ",
		 $tm->year % 100, $tm->mon+1, $tm->mday,
		 $tm->hour, $tm->min, $tm->sec);
}


# Print message to screen
sub mtr_report (@) {
  print join(" ", @_), "\n";
}


# Print warning to screen
sub mtr_warning (@) {
  print STDERR _timestamp(), "mysql-test-run: WARNING: ", join(" ", @_), "\n";
}


# Print error to screen and then exit
sub mtr_error (@) {
  print STDERR _timestamp(), "mysql-test-run: *** ERROR: ", join(" ", @_), "\n";
  exit(1);
}


sub mtr_debug (@) {
  if ( $::opt_verbose > 1 )
  {
    print STDERR _timestamp(), "####: ", join(" ", @_), "\n";
  }
}


sub mtr_verbose (@) {
  if ( $::opt_verbose )
  {
    print STDERR _timestamp(), "> ",join(" ", @_),"\n";
  }
}


sub mtr_verbose_restart (@) {
  my ($server, @args)= @_;
  my $proc= $server->{proc};
  if ( $::opt_verbose_restart )
  {
    print STDERR _timestamp(), "> Restart $proc - ",join(" ", @args),"\n";
  }
}


1;
