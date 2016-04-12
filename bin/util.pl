# util.pl -- utilities which used to live in bin/args.pl

sub my_system {
    my ($file, $line);
    $file = shift;
    $line = shift;
    my $ret;
    if (($ret = system(@_)) != 0) {
	my $msg = "$file:$line: @_\n\t";
	my ($excode, $sig, $cored) = ($ret >> 8, $ret & 127, $ret & 128);
	if ($sig) {
	    $msg .= "died with signal $sig";
	    $msg .= ' (core dumped)' if ($cored);
	    $msg .= "\n";
	} else {
	    $msg .= "exited abnormally with code $excode\n";
	}
	return $msg;
    }
    undef;
}

sub warn_system {
    my ($pack, $file, $line) = caller;
    my $msg = my_system($file, $line, @_);
    warn $msg if $msg;
}

sub die_system {
    my ($pack, $file, $line) = caller;
    my $msg = my_system($file, $line, @_);
    die $msg if $msg;
}

# Utility functions
# Create a unique hash array. Programming Perl, 2nd edition, p291 (p217?)
package ArrayInstance;
sub new {
    my $type = shift;
    my %params = @_;
    my $self = {};
    return bless $self, $type;
}

package main;

# run a command in the background, return its PID
# Uses fork: will not run on NT in perl 5.004
# if the server is "localhost", ignore the rcmd part
# if stdin, stdout, and/or stderr is set, redirect those for the sub process
sub forkproc {
    my $rcmd = shift;
    my $server = shift;
    my $command = shift;
    my $stdin = shift;
    my $stdout = shift;
    my $stderr = shift;

    if (my $pid = fork()) {
	return $pid;		# parent
    }

    # rest of this is in the child
    if ($stdin) {		# redirect stdin if needed
	close (STDIN);
	open STDIN, "<$stdin"
	    || die "Couldn't open $stdin for input\n";
    }

    if ($stdout) {		# redirect stdout if needed
	close (STDOUT);
	open STDOUT, ">>$stdout"
	    || die "Couldn't open $stdout for output\n";
    }

    if ($stderr) {		# redirect stderr if needed
	close (STDERR);
	open STDERR, ">>$stderr"
	    || die "Couldn't open $stderr for output\n";
    }

    if ($server =~ /^localhost$/i) {
	exec $command;
	die "Coundn't exec $command:$!\n";
    } else {
	exec split (/\s+/, $rcmd), $server, $command;
	die "Coundn't exec $rcmd $server $command:$!\n";
    } 
}

# Run PRETEST, MONITOR, or POSTTEST scripts
# Usage: runScripts block_name default_command wait_all
#   block_name  PRETEST, MONITOR, or POSTTEST
#   default_command  What to run if command not specified.
#   wait_all         0 to not wait, 1 to wait, -1 to add to @forceDownPids
sub runScripts {
  my $blockname = shift || die "runScripts: missing block name";
  my $defcmd = shift;
  my $wait_all = shift || die "runScripts: missing wait";

  my $sectname = lc($blockname);

  foreach $section (@workload) { # start monitors
    next unless ($section->{sectionTitle} =~ /$blockname/i);
    my $myCmd = ($section->{COMMAND}) ? $section->{COMMAND} : $defcmd;
    unless ($myCmd) {
      print "Warning: Ignoring $blockname with no Command}\n";
      next;
    }

    my @pidlist = ();
    my @hlist = @allClients;
    my $slist = $section->{sectionParams};
    if ($slist) {
      $slist =~ s/HOSTS=\s*//; # strip off initial bit
      @hlist = split /[\s,]/, $slist;
    }

    my $forceDown = 0;
    $myCmd =~ s/,/ /g;	# turn commas into spaces BACK COMPATIBIILITY
    $myCmd =~ s/%f/$params{FREQUENCY}/; # fill in frequency variable

    if ($wait < 0 && $myCmd =~ m/%c/o) { # dont force down if count is used
      $count = $testsecs / $params{FREQUENCY};
      $myCmd =~ s/%c/$count/; # fill in count variable
    } else {
      $forceDown = 1;
    }
    my $rsh = ($section->{RSH}) ? $section->{RSH} : $params{RSH};

    foreach $cli (@hlist) {
      my $name = ($section->{NAME}) ? "log-$section->{NAME}" : "$sectname.txt";
      $logfile = "$resultdir/$cli";
      if ( -f "$logfile-$name") { # check for multiple monitors
        my $tail = 'a';
        while ( -f "$logfile$tail-$name" ) { $tail++; }
        $logfile .= $tail;
      }
      $logfile .= "-$name";
      printf "Running $sectname on $cli \t($logfile)\n";
      open PRE, ">>$logfile";
      print PRE "========\n";
      print PRE "= $myCmd =\n";
      print PRE "========\n";
      close PRE;
      print STDERR "$cli: $myCmd\n"; # log the actual command
      $pid = forkproc ($rsh, $cli, $myCmd,
                       "/dev/null", "$logfile");
      push @forceDownPids, $pid if ($forceDown && $wait_all < 0); # save PID for shutdown
      push @pidlist, $pid if ($wait_all > 0);
    }
    foreach $p (@pidlist) {
      wait();		# wait for everything to finish
    }
  }
}

# Run post processing for PRETEST, MONITOR, or POSTTEST scripts
# Usage: processScripts block_name default_command wait_all
#   block_name  PRETEST, MONITOR, or POSTTEST
# Returns: list of processed output files.
sub processScripts {
  my $blockname = shift || die "processScripts: missing block name";

  my $sectname = lc($blockname);
  my @filelist = ();

  foreach $section (@workload) {
    next unless ($section->{sectionTitle} =~ /$blockname/i);
    next unless ($section->{COMMAND});

    my @hlist = @allClients;
    if ($slist) {
      $slist =~ s/HOSTS=\s*//; # strip off initial bit
      @hlist = split /[\s,]/, $slist;
    }

    foreach $cli (@hlist) {
      my $name = ($section->{NAME}) ? "log-$section->{NAME}" : "$sectname.txt";
      my @files = <$resultdir/$cli*-$name>;
      foreach $f (@files) {
        if ($section->{PROCESS}) {
          my $oname = $f;
          $oname =~ s/-log-/-/;
          my @proclist = split (/\s+/, $section->{PROCESS});
          if ($proclist[0] =~ m/\.pl$/o) { # user our perl config
            @proclist = (("perl/bin/perl", "-Ibin", "--"), @proclist); 
          }
          warn_system (@proclist, $f, $oname,
                       $realTestSecs, $params{FREQUENCY},
                       $params{CHARTWIDTH}, $params{CHARTHEIGHT});
          push @filelist, (-r $oname) ? $oname : $f;
        } else {
          push @filelist, $f;
        }
      }
    }
  }
  return @filelist;
}

# Relocate file to tmp directory (if it is in the results directory),
#  and put a ~ on the end of it.
# ASSUMES tmp and results are on the same partition (on NT, same drive).
# Usage: fileBackup (filename)
sub fileBackup {
    my $filename = shift;
    my $bfile = $filename;

    (-f $filename) || return 0;	# file doent exist
    $bfile =~ s/$resultbase/$tmpbase/; # move to tmp
    $bfile .= "~";		# indicate that this is a backup
    (-f $bfile) && unlink ($bfile);
    #print "Backing up $filename to $bfile\n"; # DEBUG
    rename ($filename, $bfile) || unlink ($filename);
}

# Insert text into a file after a tagline
# fileInsertAfter (filename, tagstring, newtext)
sub fileInsertAfter {
    my $filename = shift || die "fileInsertAfter: missing filename";
    my $tagline = shift || die "fileInsertAfter: missing tagline";
    my $newtext = shift || die "fileInsertAfter: missing text";
    my $foundit = 0;

    open(OLD, "<$filename") ||
	open(OLD, "gunzip -c $filename |") ||
	    die "fileInsertAfter: Could not open input $filename: $!";
    open(NEW, ">$filename+") ||
	die "fileInsertAfter: Could not open output $filename+: $!";

    while (<OLD>) {
	print NEW $_;		# copy (including tagline)

	next unless (/$tagline/); # matched tagline

	print NEW $newtext;	# insert new text
	$foundit++;
	last;			# only change first occurance
    }

    if ($foundit) {		# copy rest of file
	while (<OLD>) {
	    print NEW $_;
	}
    }

    close (OLD);
    close (NEW);
    if ($foundit) {
	fileBackup ($filename);
	rename ("$filename+", "$filename");
	#print "Updated $filename\n"; # DEBUG
	return $foundit;
    } else {
	($params{DEBUG}) && print "No change to $filename\n"; # DEBUG
	unlink ("$filename+");
	return 0;
    }
}

# Do text for text replacements in a file.
# Perl wildcards are automatically quoted.
# fileReplace (filename, matchPat, oldtext, newtext)
sub fileReplaceText {
    my $filename = shift || die "fileReplaceText: missing filename";
    my $tagline = shift || die "fileReplaceText: missing tagline ($filename)";
    my $oldtext = shift;
    my $newtext = shift;
    my $foundit = 0;

    return if ($newtext eq "");	# nothing to do
    return if ($oldtext eq "");	# nothing can be done

    open(OLD, "<$filename") ||
	open(OLD, "gunzip -c $filename |") ||
	    die "fileReplaceText: Could not open input $filename: $!";
    open(NEW, ">$filename+") ||
	die "fileReplaceText: Could not open output $filename+: $!";

    $oldtext =~ s/([][{}*+?^.\/])/\\$1/g; # quote regex syntax

    while (<OLD>) {
	if (/$tagline/i) { # matched tagline
	    $foundit++;
	    s/$oldtext/$newtext/; # do the replace
	}
	print NEW $_;
    }

    close (OLD);
    close (NEW);
    if ($foundit) {
	fileBackup ($filename);
	rename ("$filename+", "$filename");
	#print "Updated $filename\n"; # DEBUG
	return $foundit;
    } else {
	($params{DEBUG}) && print "No change to $filename\n"; # DEBUG
	unlink ("$filename+");
	return 0;
    }
}

# copy a file to a new name.  Handles possible compression.  OS independent.
# fileCopy (filename, newname)
sub fileCopy {
    my $filename = shift || die "fileReplaceText: missing filename";
    my $newname = shift || die "fileReplaceText: missing newname ($filename)";

    open(OLD, "<$filename") ||
	open(OLD, "gunzip -c $filename |") ||
	    die "fileReplaceText: Could not open input $filename: $!";
    open(NEW, ">$newname") ||
	die "fileReplaceText: Could not open output $newname: $!";

    while (<OLD>) {		# copy it
	print NEW $_;
    }

    close (OLD);
    close (NEW);
    return 0;
}

# display a file to STDOUT.  Handles possible compression
sub fileShow {
    my $filename = shift || die "fileShow: missing filename";
    open(SHOWIT, "<$filename") ||
	open(SHOWIT, "gunzip -c $filename.gz |") ||
	    die "fileShow: Couldn't open $filename: $!";
    while (<SHOWIT>) { print; }
    close(SHOWIT);
}

# sub function to figure time extents
# (start, end) = dataMinMax counterName \@protocols oldstarttime oldendtime
# Use 0 for uninitialized start or end
sub dataMinMax {
    my $name = shift;
    my $protos = shift;
    my $start = shift;
    my $end = shift;
    
    # make global
    # create the plot script and data files
    # Figure out the encompassing time extent
    foreach $p (@$protos) {	# create the plot data files
	my @times = sort numeric keys %{ $graphs{$p}->{$name}};
	if ($#times <= 0) {
	    next;
	}
	if (($start == 0) || ($times[0] < $start)) {
	    $start = $times[0];
	}
	if (($end == 0) || ($times[0] > $end)) {
	    $end = $times[$#times];
	}
    }
    #printf ("Data $name start=$start end=$end (%d points)...\n",
	#    $end - $start);
    return ($start, $end);
}

# simple function to formatted a number into n, n K, n M, or n G
sub kformat {
    my $n = shift;
    my $r = "";
    if ($n > (1024*1024*1024)) {
	$r = sprintf "%.2fG", $n / (1024*1024*1024);
    } elsif ($n > (1024*1024)) {
	$r = sprintf "%.2fM", $n / (1024*1024);
    } elsif ($n > 1024) {
	$r = sprintf "%.2fK", $n / 1024;
    } else {
	$r = sprintf "%d ", $n;
    }
    return $r;
}

# Function to format a time into Ts, Tms, or Tus.
# The goal is to make a table of times uncluttered and easy to read.
# I dont convert to minutes or hours because the non-1000x multipliers
#   are hard to back solve in your head for comparisons
sub tformat {
    my $n = shift;
    my $r = "";
    if ($n == 0.0) {
	$r = "0.0";		# make exactly 0 explicit
    } elsif ($n < 0.001) {
	$r = sprintf "%.2fus", $n * 1000 * 1000;
    } elsif ($n < 1.0) {
	$r = sprintf "%.2fms", $n * 1000;
    } elsif ($n >= 1000.0) {
	$r = sprintf "%.0fs", $n;
    } elsif ($n >= 100.0) {
	$r = sprintf "%.1fs", $n;
    } else {
	$r = sprintf "%.3fs", $n;
    }
    return $r;
}

# Function to format a time into Ts, Tmin, Thr, or Tday.
# The goal is to make test durations easy to read
sub timeFormat {
    my $n = shift;
    my $r = "";
    # try to keep 3 significant digits using human friendly thresholds.
    if ($n < 600) {             # less than 10 min
	$r = sprintf "%.0fsec", $n;
    } elsif ($n < 6000) {       # less than 100 min
	$r = sprintf "%.1fmin", $n / 60;
    } elsif ($n < 72000) {      # less than 10 hr
	$r = sprintf "%.1fhr", $n / 3600;
    } elsif ($n < (100 * 3600)) { # less than 100 hr
	$r = sprintf "%.0fhr", $n / 3600;
    } else {
	$r = sprintf "%.1fday", $n / (24 * 3600);
    }
    return $r;
}

#Usage: commify (1234567) returns 1,234,567
sub commify {			# perl cookbook p64-65
    my $text = reverse $_[0];
    $text  =~ s/(\d\d\d)(?=\d)(?!\d*\.)/$1,/g;
    return scalar reverse $text;
}

# subroutine to enable numeric sorts.  Programming Perl p218
# Use: sort numeric ...
sub numeric { $a <=> $b; }

# on NT, turn slash to backslash, then print.  Else print.
sub pathprint {
    my $str = shift;
    $str =~ s!/!\\!g if ($params{NT});	# turn slash to back slash
    print $str;
}
    
# figureTimeNumber number
# Given an number like: 60m, 1h, 100s, 4d, 200
# Return 60, 1, 100, 4, 200
sub figureTimeNumber {
    my $arg = shift;

    ($arg =~ /([0-9]+)(s|sec|second|seconds|m|min|minute|minutes|h|hr|hour|hours|d|day|days)$/i)
	&& return $1;
    return $arg;			# return default
}

# figureTimeUnits number, default
# Given an number like: 60m, 1h, 100s, 4d
# Return a string of minutes, hours, seconds, days
# Else return the second argument
sub figureTimeUnits {
    my $arg = shift;

    ($arg =~ /(s|sec|second|seconds)$/i)	&& return "seconds";
    ($arg =~ /(m|min|minute|minutes)$/i)	&& return "minutes";
    ($arg =~ /(h|hr|hour|hours)$/i)	&& return "hours";
    ($arg =~ /(d|day|days)$/i)		&& return "days";

    return shift;		# return default
}

# figureTimeSeconds number, defaultUnits
# Given an number like: 60m, 2h, 100s, 4d
# Return 60*60, 2*60*60, 100, 4*24*60*60
sub figureTimeSeconds {
    my $arg = shift;

    ($arg =~ /([0-9]+)(s|sec|second|seconds)$/i)	&& return $1;
    ($arg =~ /([0-9]+)(m|min|minute|minutes)$/i)	&& return (60*$1);
    ($arg =~ /([0-9]+)(h|hr|hour|hours)$/i)	&& return (60*60*$1);
    ($arg =~ /([0-9]+)(d|day|days)$/i)		&& return (24*60*60*$1);

    if ($_) { 
	my $def = shift;
	return $arg * figureTimeSeconds ("1$def"); # return scaled by default
    } else {
	return $arg;		# return it
    }
}

# read the workload file and store it as a list of hashes
# Each hash always has the fields: sectionTitle and sectionParams
# usage: readWorkloadFile filename, \@list
sub readWorkloadFile {
    my $filename = shift || die "readWorkloadFile: Missing file name";
    my $plist = shift || die "readWorkloadFile: Missing return list";
    my $level = 0;		# file inclusion level
    my @handles;

    my $fh = "$filename$level";

    ($params{DEBUG}) && print "Reading workload from $filename.\n";
    open($fh, "<$filename") ||
	open($fh, "gunzip -c $filename.gz |") ||
	    die "readWorkloadFile Couldn't open testbed $filename: $!";
    $includedFiles{$filename} = 1; # mark file as included

    my $sparm=0;
    my $conline = "";
    
    while($fh) {
	while(<$fh>) {
	    s/#.*//;		# strip any comments from line (quoting?)
	    s/\s*$//;		# strip trailing white space
	    if ($conline) {	# utilize line continue
		$_ = $conline . "\\\n" . $_;
		$conline = "";
	    }
	    if (m/\\$/o) {	# check for quoted line continue
		s/\\$//;	# 
		$conline = $_;
		next;
	    }
	    s/^\s*//;		# strip initial white space
	    m/^$/o &&  next;	# continue if blank line

	    # handle include and includeOnce statements
	    if ((m/^<(include)\s+(\S+)\s*>/i)
		|| (m/^<(includeonce)\s+(\S+)\s*>/i)) {
		my $incfile = $2;
		if (($1 =~ m/^includeonce/i) && ($includedFiles{$incfile})) {
		    ($params{DEBUG})
			&& print "readWorkloadFile:includeOnce $incfile already read.\n";
		    next;
		}
		($params{DEBUG})
		    && print "readWorkloadFile include $incfile from $filename.\n";
		$includedFiles{$incfile} = 1; # mark file
		push @handles, $fh; # push current handle on to stack
		if ($level++ > 99) { # check recursion and make handles unique
		    die "readWorkloadFile: include level too deep: $filename $level\n";
		}
		$fh = "$incfile$level";
		open($fh, "<$incfile") ||
		    open($fh, "gunzip -c $incfile.gz |") ||
			die "readWorkloadFile Couldn't open testbed file $incfile: $!";
		$filename = $incfile;	# for error messages
		next;
	    }

	    if (m!^</(\w+)>$!o) { # end of section
		my $end = $1;
		unless ($sparm->{"sectionTitle"} =~ /$end/i) {
		    die "readWorkloadFile Mismatched section $filename: $. '$sparm->{sectionTitle}' '$end'\n";
		    return 0;
		}
		($params{DEBUG}) && print "</$sparm->{sectionTitle}>\n";
		push @$plist, $sparm;
		$sparm = 0;
		next;
	    }

	    if (m!^<(\w+)\s*(.*)>$!o) { # start of section
		my $sec = $1;
		my $more = $2;

		if ($sparm) {
		    die "readWorkloadFile Missing section end $filename: $. '$sparm->{sectionTitle}'\n";
		}
		if ($sec =~ /CONFIG/i) { # special case, map to existing global
		    $sparm = \%params;
		} elsif ($sec =~ /DEFAULT/i) { # special case, only one DEFAULT
		    if ($defaultSection) { # use existing defaultSection
			$sparm = $defaultSection;
		    } else {	# create a new one
			$sparm = ArrayInstance->new();
			$sparm->{"sectionTitle"} = uc $sec; # ignore case
			$sparm->{"lineList"} = ();
			$defaultSection = $sparm;
		    }
		} else {
		    $sparm = ArrayInstance->new();
		    $sparm->{"sectionTitle"} = uc $sec; # ignore case
		    $sparm->{"lineList"} = ();
		}
		$sparm->{"sectionParams"} = $more; # take newest more info
		($params{DEBUG})
		    && print "<$sparm->{sectionTitle} $sparm->{sectionParams}>\n";
		next;
	    }

	    # must be in a section, get parameters
	    unless ($sparm) {
		die "readWorkloadFile Entry encountered outside a section $filename: $. $_\n";
		return 0;
	    }
	    my ($nm, $val) = split (/[\s=]+/, $_, 2);
	    $nm = uc $nm;	# ignore case
	    ($params{DEBUG}) && print "    $nm = $val\n";
	    push @{$sparm->{"lineList"}}, $_; # save lines in original order
	    $sparm->{$nm} = $val;
	    next;
	}
	close ($fh);
	$fh = pop @handles || last; # empty include stack
	$filename = $fh;
	$sparm = 0;		# can only include whole sections
    }
    return 1;			# success
}

# Write out a workload list to a file
# Optionally, pass in a list of sectionTitle's it should ignore
# usage: writeWorkloadFile filename \@list [\@skipList]
sub writeWorkloadFile {
    my $filename = shift || die "writeWorkloadFile: Missing file name";
    my $plist = shift || die "writeWorkloadFile: Missing return list";
    my $skip = shift;
    my @skipH;
    my $configSeen = 0;
    my $defaultSeen = 0;
    my @paramH;

    if ($skip) {
	foreach $s (@$skip) {	# turn list into a hash
	    $skipH{(uc $s)} = $s; # fix case for index
	}
    }
    foreach $s (@workloadParameters) { # turn list into a hash
	$paramH{(uc $s)} = $s;	# fix case for index
    }

    ($params{DEBUG}) && print "Writing workload to:  $filename\n";
    unless (open(WORKOUT, ">$filename")) {
	die "Couldn't open testbed $filename: $!";
    }
    
    foreach $sparm (@$plist) {	# each hash reference in the list
	if (($skip)
	    && ($skipH{$sparm->{"sectionTitle"}})) {
	    #($params{DEBUG}) &&
	    #print "Skipping section $sparm->{sectionTitle}\n";
	    next;
	}
	
	# all CONFIG,DEFAULT sections point to the same hash, output once only
	if ($sparm->{"sectionTitle"} =~ /^CONFIG$/) {
	    next if $configSeen;
	    $configSeen++;
	}
	if ($sparm->{"sectionTitle"} =~ /^DEFAULT$/) {
	    next if $defaultSeen;
	    $defaultSeen++;
	}
	if ($sparm->{sectionParams}) { # write section with extra args
	    print WORKOUT "<$sparm->{sectionTitle} $sparm->{sectionParams}>\n";
	} else {
	    print WORKOUT "<$sparm->{sectionTitle}>\n";
	}
	if ($sparm->{"sectionTitle"} =~ /^(CONFIG|CLIENT)$/) {
	    # for Config or Client, output the hash  to get computed config
	    foreach $k (sort keys %$sparm) { # output each parameter
		# skip sectionTitle and sectionParams
		($k =~ /^(sectionTitle|sectionParams|lineList)$/) && next;
		printf WORKOUT "  %s\t%s\n",
		($paramH{$k}) ? $paramH{$k} : $k,
		$sparm->{$k};
	    }
	} else {	# write out the line list
	    foreach $l (@{$sparm->{"lineList"}}) {
		print WORKOUT "  $l\n";
	    }
	}
	print WORKOUT "</$sparm->{sectionTitle}>\n\n";
    }
    close WORKOUT;
}

# Usage: getClientFilename hostname section
sub getClientFilename  {
    my $cli = shift || die "Missing client name";
    my $section = shift || die "Missing section hash";
    return "$tmpdir/$cli-$section->{GROUP}.out"
	if ($params{USEGROUPS} && $section->{GROUP});
    return "$tmpdir/$cli.out"
}

sub setConfigDefaults {		# set CONFIG defaults
    # These are set after writing out the test copy to avoid clutter

    # Path to gnuplot executable
    $params{GNUPLOT}="gnuplot/gnuplot"
	unless ($params{GNUPLOT});

    # This is the directory the client lives in
    $params{TEMPDIR} = "/var/tmp"
	unless($params{TEMPDIR});

    # Set default remote shell
    #$params{RSH} = "rsh"
    $params{RSH} = "ssh -x"
	unless($params{RSH});

    # Set default remote copy
    #$params{RCP} = "rcp"
    $params{RCP} = "scp -q"
	unless($params{RCP});

    # Size of generated gifs
    $params{CHARTHEIGHT} = 480
	unless($params{CHARTHEIGHT});
    $params{CHARTWIDTH} = 640
	unless($params{CHARTWIDTH});
    $params{CHARTPOINTS} = int (($params{CHARTWIDTH}-60)*0.8)
	unless($params{CHARTPOINTS});


    # The name of the remote executable
    $params{CLIENTCOMMAND} = "./mailclient"
	unless ($params{CLIENTCOMMAND});

    # Set default monitoring command
    $params{MONITORCOMMAND} = "vmstat %f"
	unless($params{MONITORCOMMAND});

    # Set default switches to makeusers
    $params{MAKEUSERSARGS} = "-4"
	unless ($params{MAKEUSERSARGS});

    # Figure out @protocols, this sets the report order
    @protocols = ();
    {
	my %skipH;
	foreach $s (@nonProtocolSections) { # turn list into a hash
	    #print "$s ";
	    $skipH{(uc $s)} = $s; # fix case for index
	}
	print "\n";
	foreach $sparm (@workload) {	# each hash reference in the list
	    next if ($skipH{$sparm->{"sectionTitle"}});

	    ($params{DEBUG}) &&
		print "Found protocol ". $sparm->{"sectionTitle"} . "\n";

	    push @protocols, $sparm->{"sectionTitle"};
				# add to skip list so only added once
	    $skipH{(uc $sparm->{"sectionTitle"})} = $sparm->{"sectionTitle"};
	}
    }
    @protocolsAll = @protocols;
    push @protocolsAll, "Total" if ($#protocols >= 1);

    # figure out the graphs ???
}

sub parseArgs {			# get args
    while (@ARGV) {
	my $arg = shift(@ARGV);

	if ($arg =~ /^-a$/i) {	# was undocumented feature in 4.1
	    $params{ADDGRAPHS} = shift(@ARGV); # extra graphs
	    next;
	}
	if ($arg =~ /^-b$/i) {
	    $params{TITLE} = shift(@ARGV); # banner
	    next;
	}

	if ($arg =~ /^-d$/i) {
	    $params{DEBUG}++;	# Debug
	    next;
	}

	if ($arg =~ /^-h$/i) {	# Help
	    print "Usage: -w workfile [-t time] [-r ramptime] [-l load] [-v] [-d]\n";
	    print "\t[-b banner] [-n notes] [-s sysconfigfile] [-a add_graphs_file]\n";
	    print "\t[-c configfile] [PARAM=value]...\n";

	    die "Usage";
	}

	if ($arg =~ /^-l$/i) {	# "load", FIX: naming conventions
	    $params{CLIENTCOUNT} = shift(@ARGV); # desired client count
	    next;
	}

	if ($arg =~ /^-n$/i) {
	    $params{COMMENTS} = shift(@ARGV); # notes
	    next;
	}

	if ($arg =~ /^-r$/i) {
	    $params{RAMPTIME} = shift(@ARGV); # ramptime
	    next;
	}
	if ($arg =~ /^-s$/i) {
	    $params{SYSCONFIG} = shift(@ARGV); # system config html file
	    next;
	}
	if ($arg =~ /^-t$/i) {
	    $params{TIME} = shift(@ARGV); # test time
	    next;
	}

	if ($arg =~ /^-v$/i) {
	    $params{VERBOSE} = 1;	# verbose mode
	    next;
	}

	if ($arg =~ /^-w$/i) {	# workload file (may occur multiple times)
	    my $f = shift(@ARGV);
            $params{WORKLOAD} = $f unless ($params{WORKLOAD});
            my $testname = $f;
            $testname =~ s:.*conf/::;
            $testname =~ s:.*results/::;
            $testname =~ s:.wld::;
            if ($params{TESTNAME}) {
              $params{TESTNAME} .= "+$testname";
            } else {
              $params{TESTNAME} = $testname;
            }
	    readWorkloadFile ($f, \@workload) || die "Error reading workload: $@\n";
	    next;
	}

	if ($arg =~ /^-z$/i) {
	    $params{NT} = 1;		# NT mode
	    next;
	}

	# any other CONFIG parameter: FIELD=value
	if ($arg =~ /^(\w+)=(\S.*)$/) {
	    my $field = uc $1;
	    $params{$field} = $2;
	    next;
	}
	die "Unknown argument '$arg'";
    }

    $cwd = `pwd`;
    chomp $cwd;		# strip NL
}

# Find the previous runs from a similar time to the current run
# Usage: getPreviousResult();
# Returns: directory_name
sub getPreviousResult {
  my $thisdir = "$resultbase/$params{TSTAMP}";
  my $tstamp = $params{TSTAMP};
  $tstamp =~ s/[0-9][0-9][0-9][0-9a-z]$//; # strip hour minute letter
  my @dirlist = <$resultbase/$tstamp*[0-9][0-9a-z]>;

  #print "Related dirs: @dirlist\n"; # DEBUG
  while (@dirlist) {            # purge this and later entries
    if ($dirlist[-1] ge $thisdir) {
      my $discard = pop @dirlist;
      #print "Newer: $discard\n";# DEBUG
    } else {
      last;
    }
  }
  return '' unless (@dirlist);
  return $dirlist[-1];
}

1;
