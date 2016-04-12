#!/usr/bin/env perl
# The contents of this file are subject to the Netscape Public
# License Version 1.1 (the "License"); you may not use this file
# except in compliance with the License. You may obtain a copy of
# the License at http://www.mozilla.org/NPL/
#
# Software distributed under the License is distributed on an "AS
# IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
# implied. See the License for the specific language governing
# rights and limitations under the License.
#
# The Original Code is the Netscape Mailstone utility,
# released March 17, 2000.
#
# The Initial Developer of the Original Code is Netscape
# Communications Corporation. Portions created by Netscape are
# Copyright (C) 1997-2000 Netscape Communications Corporation. All
# Rights Reserved.
#
# Contributor(s):	Dan Christian <robodan@netscape.com>
#			Marcel DePaolis <marcel@netcape.com>
#			Mike Blakely
#
# Alternatively, the contents of this file may be used under the
# terms of the GNU Public License (the "GPL"), in which case the
# provisions of the GPL are applicable instead of those above.
# If you wish to allow use of your version of this file only
# under the terms of the GPL and not to allow others to use your
# version of this file under the NPL, indicate your decision by
# deleting the provisions above and replace them with the notice
# and other provisions required by the GPL.  If you do not delete
# the provisions above, a recipient may use your version of this
# file under either the NPL or the GPL.
#####################################################

#  see setup.pl for full usage
#	mailmaster [-d] [-c <config file>] ...

# This script reads in the client configuration files and will
# fork children to rsh the mailclient process on network clients,
# each child will write test results to /mailstone directory before
# dying.  The parent will the read and combine the results.
#
# Make sure the user running this script has rsh privilege across
# all client machines

package main;

use POSIX ":sys_wait_h";

print "Mstone version 4.9.4\n";
print "Copyright (c) Netscape Communications Corp. 1997-2000\n";

# this parses the command line and config file
do 'args.pl'|| die "$@\n";
sub die_system;
sub warn_system;

@cmdLine = @ARGV;  # save original command line for documentation
parseArgs();			# parse command line

{				# get unique date string
    my ($sec, $min, $hour, $mday, $mon, $year) = localtime;
    my $tstamp = sprintf ("%04d%02d%02d.%02d%02d",
		       1900+$year, 1+$mon, $mday, $hour, $min);

    if ( -d "$resultbase/$tstamp") { # check for runs within a minute
	my $tail = 'a';
	while ( -d "$resultbase/$tstamp$tail" ) { $tail++; }
	$tstamp .= $tail;
    }
    $params{TSTAMP} = $tstamp;
}

$resultdir = "$resultbase/$params{TSTAMP}";
$tmpdir = "$tmpbase/$params{TSTAMP}";
$resultstxt = "$resultdir/results.txt";
$resultshtml = "$resultdir/results.html";
$resultprev = getPreviousResult();
mkdir ("$resultbase", 0775);
mkdir ("$tmpbase", 0775);
mkdir ("$resultdir", 0775);
mkdir ("$tmpdir", 0775);

# Make sure we have everything
die "Must specify the test time" unless $params{TIME};
die "Must specify a workload file" unless $params{WORKLOAD};

$testsecs = figureTimeSeconds ($params{TIME}, "seconds");

# figure out the processes and thread, given the desired number
# takes into account all the constraints.  todo can be a float.
# Usage: figurePT section_hash clients_to_allocate host_count
# Updates PROCESSES and THREADS in section_hash
sub figurePT {
    my $sec = shift;            # section to set processes and threads
    my $todo = shift;           # number of clients needed
    my $hcnt = shift;           # hosts in this section

    my $p = 1;			# first guess at processes
    my $t = 1;                  # first guess at threads
    my $start = 1;		# process guess start range
    my $end = 250;		# process guess end range

    if ($todo < $hcnt) {        # mark this client as inactive
	$sec->{PROCESSES} = 0;
	$sec->{THREADS} = 0;
	return 0;
    }

    if (($section->{MAXCLIENTS}) && ($todo > $hcnt * $section->{MAXCLIENTS})) {
	$todo = $hcnt * $section->{MAXCLIENTS};	# trim to max client per host
    }
    $end = $todo / $hcnt if $end > $todo / $hcnt;

    if ($section->{MAXPROCESSES} && $section->{MAXPROCESSES} < $end) {
      $end = int ($section->{MAXPROCESSES});
    }

    if ($section->{PROCESSES}) { # they set this part already
	$start = int ($section->{PROCESSES});
	$end = $start;
	$p = $start;
	my $slist = $section->{sectionParams};
	$slist =~ s/HOSTS=\s*//; # strip off initial bit
	print "Using specified $p processes for clients $slist\n";
    }

    # Step through some process counts to reduce errors due to integer
    # allocations.  Not optimal, just good enough
    my $misses = 0;
    my $err = $todo - $hcnt * $p;
    for (my $n = $start; $n <= $end; $n++) { # try some process counts
	my $tryt = int ($todo / ($n * $hcnt));
	if (($sec->{MAXTHREADS}) && ($tryt > $sec->{MAXTHREADS})) {
	    $tryt = $sec->{MAXTHREADS};
	}
	# see if this is a better match than the last one
        my $tryerr = abs ($todo - ($hcnt * $n * $tryt));
	if ($tryerr < $err) {
	    $p = $n;
	    $t = $tryt;
            last unless ($tryerr);
            $err = $tryerr;
	    $misses = 0;
	} else {
	    $misses++;
	    last if ($misses > 3); # getting worse
	}
    }
    $sec->{PROCESSES} = $p;
    $sec->{THREADS} = $t;
    return $hcnt * $p * $t;
}

@allClients = ();            # list of all clients running this test

# Allocate CLIENTCOUNT to the client machines
# try NOT to turn this into a massive linear programming project
# works best to put bigger machines last
if ($params{CLIENTCOUNT}) {
    my $todo = $params{CLIENTCOUNT};
    my $softcli = 0;		# how many can we play with

    foreach $section (@workload) { # see which are already fixed
	next unless ($section->{sectionTitle} =~ /CLIENT/i);
	unless (($section->{PROCESSES}) && ($section->{THREADS})) {
	    my $slist = $section->{sectionParams};
	    $slist =~ s/HOSTS=\s*//; # strip off initial bit
	    my @hlist = split /[\s,]/, $slist;
	    my $hcnt = (1 + $#hlist);
	    $softcli += $hcnt;
	    next;
	}
	my $slist = $section->{sectionParams};
	$slist =~ s/HOSTS=\s*//; # strip off initial bit
	my @hlist = split /[\s,]/, $slist;
	my $hcnt = (1 + $#hlist);

	# subtract fixed entries
	my $tcount = ($section->{THREADS}) ? $section->{THREADS} : 1;
        my $clicount = $tcount * $section->{PROCESSES} * $hcnt;
        $todo -= $clicount;
	$clientProcCount += $section->{PROCESSES} * $hcnt; # total processes
        push @allClients, @hlist if ($clicount > 0);
	$params{DEBUG} &&
	    print "Fixed load group with $hcnt hosts: $section->{PROCESSES} x $tcount\n";
    }

    $params{DEBUG} &&
	    print "Allocating $todo clients over $softcli hosts\n";
    if ($softcli) {
	foreach $section (@workload) {
	    next unless ($section->{sectionTitle} =~ /CLIENT/i);
	    next if (($section->{PROCESSES}) && ($section->{THREADS}));
	    my $slist = $section->{sectionParams};
	    $slist =~ s/HOSTS=\s*//; # strip off initial bit
	    my @hlist = split /[\s,]/, $slist;
	    my $hcnt = (1 + $#hlist);

            my $clicount;
            if ($softcli >= $hcnt) {
              $clicount = figurePT ($section,
                                    int (0.5 + $todo * $hcnt / $softcli),
                                    $hcnt);
            } else {
              $clicount = figurePT ($section,
                                    int ($todo * $hcnt / $softcli), $hcnt);
            }
            if ($clicount) {
              $params{DEBUG} &&
                print "@hlist: $section->{PROCESSES} x $section->{THREADS}\n";
              $todo -= $clicount;
              $clientProcCount += $hcnt * $section->{PROCESSES}; # total procs
              push @allClients, @hlist if ($clicount > 0);
            }
            $softcli -= $hcnt;
	    last if ($softcli <= 0);
	}
    }
    if ($todo) {
	print "Warning: Could not allocate $todo of $params{CLIENTCOUNT} clients.\n";
	$params{CLIENTCOUNT} -= $todo;
    }
	    
   
} else { 	# figure out the client count from pre-configured sections
    my $cnt = 0;
    foreach $section (@workload) { # see which are already fixed
	next unless ($section->{sectionTitle} =~ /CLIENT/i);
  	#next unless ($section->{PROCESSES});
	next unless $section->{CLIENTS};

	my $clients = $section->{CLIENTS};
	my $slist = $section->{sectionParams};
	$slist =~ s/HOSTS=\s*//; # strip off initial bit
	my @hlist = split /[\s,]/, $slist;
	my $hcnt = scalar @hlist;

	$clients /= $hcnt;
	my $maxp = ($section->{MAXPROCESSES} || 10000);
	my $maxt = ($section->{MAXTHREADS} || 10000);
	if ($maxp * $maxt < $clients) {
	    die <<EOS;
Too many clients for hosts $section->{sectionParams}:
clients 	= $section->{CLIENTS}
maxThreads 	= $section->{MAXTHREADS}
maxProcesses 	= $section->{MAXPROCESSES}
EOS
	}
	my ($nt, $np);
	if ($maxt >= $clients) {
	    $nt = $clients;
	    $np = 1;
	} else {
	    $np = int (($clients / $maxt) + (($clients % $maxt) ? 1 : 0));
	    $nt = int ($clients / $np);
	}

	$section->{THREADS} = $nt;
	$section->{PROCESSES} = $np;
  	#$section->{CLIENTS} = $np * $nt * $hcnt;
	$cnt += $nt * $np * $hcnt;
	$clientProcCount += $np * $hcnt; # total processes
        push @allClients, @hlist if ($np > 0);
    }
    $params{CLIENTCOUNT} = $cnt;
    die "ClientCount not specified and no clients were fully configured!\n"
      unless ($params{CLIENTCOUNT} > 0);
}

# This has to be written into save workload file for later processing
unless ($params{FREQUENCY}) {	# unless frequency set on command line
    my $chartp = ($params{CHARTPOINTS}) ? $params{CHARTPOINTS} : 464;

    # approximate data points for good graphs (up to 2 times this)
    $params{FREQUENCY} = int ($testsecs / $chartp);
    if ($params{FREQUENCY} < 2) { # fastest is every 2 seconds
	$params{FREQUENCY} = 2;
    } elsif ($params{FREQUENCY} > 60) {	# slowest is every minute
	$params{FREQUENCY} = 60;
    }
}

{ # set a unique block id on every section
    my $id = 0;
    my $configSeen = 0;
    my $defaultSeen = 0;
    foreach $section (@workload) {
	if ($section->{"sectionTitle"} =~ /^CONFIG$/) {
	    next if $configSeen;
	    $configSeen++;
	}
	if ($section->{"sectionTitle"} =~ /^DEFAULT$/) {
	    next if $defaultSeen;
	    $defaultSeen++;
	}
	$id++;			# number 1, 2, ...
	if ($section->{"sectionTitle"} =~ /^(CONFIG|CLIENT)$/) {
	    $section->{BLOCKID} = $id;
	} else {
	    push @{$section->{"lineList"}}, "blockID\t$id\n";
	}
    }
}

# Write the version we pass to mailclient
writeWorkloadFile ("$resultdir/work.wld", \@workload,
		   \@scriptWorkloadSections);

# Write the complete inclusive version
writeWorkloadFile ("$resultdir/all.wld", \@workload);

die "No clients configured!\n" unless ($params{CLIENTCOUNT} > 0);

# SEAN: copy the wld.in file to the result directory for later
# statistics gathering.
my $wld = $params{WORKLOAD};
if (-f "$wld.in") {
    die_system "cp $wld.in $resultdir/wld.in";
} else {
    unless ($wld !~ /\.preload_(new|old|touch)$/) {
	warn "Can't find wld.in file for `$wld'\n" unless -f "$wld.in";
    }
}

setConfigDefaults();		# pick up any missing defaults

unless ($#protocolsAll >= 0) {
    die "No protocols found.  Test Failed!\n";
}

print "Starting time: ", scalar(localtime), "\n";

# redirect STDERR
open SAVEERR, ">&STDERR";
open(STDERR, ">$resultdir/stderr") || warn "Can't redirect STDERR:$!\n";
print STDERR  "command line: $0 @cmdLine\n"; # store complete invocation

$totalProcs = 0;		# number of clients started

# iterate over every client in the testbed, complete the cmd and rsh
{			# not NT (forking works)
    my @localPids;
    my @remotePids;
    print STDERR "\nRunning any PRETEST scripts.\n";
    runScripts ("PRETEST", "", 1);
    print STDERR "\nRunning any MONITOR scripts.\n";
    runScripts ("MONITOR", "$params{MONITORCOMMAND}", -1);

    print "Starting clients (errors logged to $resultdir/stderr)\n";
    print STDERR "\nRunning test clients.\n";
    foreach $section (@workload) {
	next unless ($section->{sectionTitle} =~ /CLIENT/i);
	next unless ($section->{PROCESSES}); # unused client

	my $slist = $section->{sectionParams};
        my @hlist = @allClients;
        if ($slist) {
          $slist =~ s/HOSTS=\s*//; # strip off initial bit
          @hlist = split /[\s,]/, $slist;
        }
	my $rsh = ($section->{RSH}) ? $section->{RSH} : $params{RSH};
	my $pcount = $section->{PROCESSES};
	my $tcount = ($section->{THREADS}) ? $section->{THREADS} : 0;
	my $tempdir;
	if ($section->{TEMPDIR}) {
	    $tempdir = $section->{TEMPDIR};
	} elsif ($params{TEMPDIR}) {
	    $tempdir = $params{TEMPDIR};
	}
	my $preCmd = (($section->{COMMAND})
                      ? $section->{COMMAND} : $params{CLIENTCOMMAND});
	$preCmd .= " -e" unless ($params{NOEVENTS});
	$preCmd .= " -s -t $params{TIME} -f $params{FREQUENCY}";
	$preCmd .= " -d" if ($params{DEBUG});
	$preCmd .= " -r" if ($params{TELEMETRY});

	if ($params{MAXERRORS}) {
	    # distribute error count over processes, rounding up
	    my $n = int (($params{MAXERRORS} + $clientProcCount - 1)
			 / $clientProcCount);
	    $n = 1 if ($n < 1);
	    $preCmd .= " -m $n";
	}
	if ($params{MAXBLOCKS}) {
	    # distribute block count over processes, rounding up
	    my $n = int (($params{MAXBLOCKS} + $clientProcCount - 1)
			 / $clientProcCount);
	    $n = 1 if ($n < 1);
	    $preCmd .= " -M $n";
	}
	$preCmd = "cd $tempdir; " . $preCmd if ($tempdir);
	$preCmd =~ s!/!\\!g if ($section->{ARCH} eq "NT4.0");
	$preCmd =~ s/;/&&/g if ($section->{ARCH} eq "NT4.0");

        my $total_clients = $section->{CLIENTS};
	my $residue = $total_clients - ($tcount * $pcount);
	foreach $cli (@hlist) {
	    my $stdout = getClientFilename ($cli, $section);
	    my $myCmd = $preCmd;
 	    $myCmd .= ($params{USEGROUPS} && $section->{GROUP})
		? " -H $section->{GROUP}" : " -H $cli";
	    my $foo = ($params{USEGROUPS} && $section->{GROUP})
		? $section->{GROUP} : undef;
            if ($params{RAMPTIME}) {
              if ($tcount > 1) {# Make 1/T process stagger, and (T-1)/T thread
                my $perThread = $params{RAMPTIME} / $tcount;
                my $threadRamp = $perThread * ($tcount - 1);
                $myCmd .= " -w -$perThread -R $threadRamp";
              } elsif ($clientProcCount > 1) { # stagger process start
                $myCmd .= " -w -$params{RAMPTIME}";
              }
              # else ramptime has no effect
            }

	    if ($tcount) {
		my $nt = $tcount;
		if ($residue > 0) {
		    ++$nt;
		    $residue -= $pcount;
		}
		$myCmd .= " -n $pcount -N $nt";
		printf "Starting $pcount x $nt on $cli%s\n", 
		    $foo?" (group = $foo)":'';
		$totalProcs += $pcount * $nt;
	    } else {
		my $np = $pcount;
		if ($residue > 0) {
		    ++$np;
		    --$residue;
		}
		$myCmd .= " -n $np";
		printf "Starting $np processes on $foo\n";
		$totalProcs += $np;
	    }

	    print STDERR "$cli: $myCmd\n"; # log the actual command
	    $pid = forkproc ($rsh, $cli, $myCmd,
			     "$resultdir/work.wld", $stdout);
	    if ($cli =~ /^localhost$/i) {
              push @localPids, $pid;
            } else {
              push @remotePids, $pid;
            }
	}
    }

    if (@localPids) {
	# print "Trapping extraneous local signals\n";
	# This doesnt trap quite right.  We dont die, but shell returns...
	$SIG{ALRM} = 'IGNORE';	# in case we get an ALRM from the mailclient
    }

    printf "\nRampup time: %d %s.  Test duration: %d %s.  Clients: %d\n",
      figureTimeNumber ($params{RAMPTIME}),
      figureTimeUnits ($params{RAMPTIME}, "seconds"),
      figureTimeNumber ($params{TIME}),
      figureTimeUnits ($params{TIME}, "seconds"),
      $totalProcs;

    do 'makeindex.pl' || warn "$@\n";	# html index

    # Watch execution.  Signal if over time.
    my @allPids = @localPids;
    push (@allPids, @remotePids);
    # round up, plus 2 grace minutes (should be configurable)
    my $waitLoops = (120.0 + figureTimeSeconds($params{TIME})) / 15.0;
    $waitLoops = int (0.5 + $waitLoops);
    my $loops = 0;
    $SIG{INT} = sub { print "Caught signal, exiting.\n"; $waitLoops = 0 };
    $SIG{QUIT} = sub { print "Caught signal, exiting.\n"; $waitLoops = 0 };
    while (@allPids && ($waitLoops-- > 0)) {
      sleep 15;
      my @livePids = ();
      foreach my $p (@allPids) {
        my $kid = waitpid ($p, WNOHANG);
        if ($kid > 0) {
          print "Process $p exited ($?).\n";
        } else  {
          push (@livePids, $p);
        }
      }
      @allPids = @livePids;
      last unless (@livePids);

      if (2 == $loops++ % 4) {
        # TODO: 'renice' ourselves to avoid affecting results.
        my $oldDebug = $params{DEBUG}; # be as quiet as possible
        my $oldVerbose = $params{VERBOSE};
        $params{DEBUG} = '';
        $params{VERBOSE} = '';
        if (3 == $loops % 80) { # remainder must be 1 more than above
          print     ("Index of runs: \tfile://$cwd/$resultbase/index.html\n");
          pathprint ("Results (HTML):\t$resultshtml\n");
        }
        print "Updating... ", scalar(localtime), ":  ";
        do 'reduce.pl';
        open SAVEERR2, ">&STDERR";
        open(STDERR, ">/dev/null");
        do 'report.pl';
        open STDERR, ">&SAVEERR2";
        $params{DEBUG} = $oldDebug;
        $params{VERBOSE} = $oldVerbose;
      }
    }

    if (@forceDownPids) {		# shut down after the first return.
	print "Shutting down monitors: @forceDownPids\n";
	kill 1 => @forceDownPids;	# sigHUP
	# kill 9 => @forceDownPids;	# sigTERM
    }

    if (@allPids) {             # force process to exit
      print "Forcing shutdown of @allPids\n";
      print STDERR "\nForcing shutdown of @allPids\n";
      foreach my $sig (1, 1, 2, 2, 15, 15, 9) {
        #print "  Sending signal $sig to: @allPids\n"; # DEBUG
        my $liveProc = kill ($sig, @allPids); # use process groups???
        my @livePids = ();
        sleep 1;
        foreach my $p (@allPids) {
          my $kid = waitpid ($p, WNOHANG);
          if ($kid > 0) {
            print "Process $p exited ($?).\n";
          } else  {
            push (@livePids, $p);
          }
        }
        @allPids = @livePids;
        last unless (@livePids);
        sleep 4;
      }
    }
    $SIG{INT} = 'DEFAULT';
    $SIG{QUIT} = 'DEFAULT';

    while ($pid != -1) {	# wait for all children
	$pid = wait();
    }

    print STDERR "\nRunning any POSTTEST scripts.\n";
    runScripts ("POSTTEST", "", 1);
}

print STDERR "\nDone.\n";
close(STDERR);
open STDERR, ">&SAVEERR";

print "\nClients done: ", scalar(localtime), "\n";
print "Collecting results\n";

do 'reduce.pl' || die "$@\n";	# generate graphs and sums

print "Generating results pages\n";

do 'report.pl' || die "$@\n";

# Now display that data to console
if ($params{VERBOSE}) {
    fileShow ($resultstxt);
    print "\n";
}
print "Processing done: ", scalar (localtime), "\n";

pathprint ("\nResults (text):\t$resultstxt\n");
pathprint (  "Results (HTML):\t$resultshtml\n");
print        "Index of runs: \tfile://$cwd/$resultbase/index.html\n";


# Now check for major problems in the stderr file
if (open(RESULTSTXT, "$resultdir/stderr")) {
    my $outLines = 0;
    my $abortLines = 0;
    my $errorLines = 0;
    my $failLines = 0;
    my $faultLines = 0;
    while (<RESULTSTXT>) {      # detect common problem indicators
      $outLines++;
      #TODO: be able to mask some failures
      $abortLines++ if (/aborted/oi);
      $errorLines++ if (/error/oi);
      $failLines++  if (/failed/oi);
      $faultLines++ if (/fault/oi);
    }
    close(RESULTSTXT);
    pathprint ("Error log: \t$resultdir/stderr (");
    print ("'aborted': $abortLines, ") if ($abortLines);
    print ("'error': $errorLines, ") if ($errorLines);
    print ("'failed': $failLines, ") if ($failLines);
    print ("'fault': $faultLines, ") if ($faultLines);
    print ("lines: $outLines)\n");
}

print "Mailmaster done: ", scalar(localtime), "\n"; exit 0;
