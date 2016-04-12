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
# Contributor(s):	Dan Christian <DanChristian65@gmail.com>
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

# Utilities to make automated, multi-test report generation possible.
# This reads the output from report.pl and generates multi-test comparisons.
# 'do' this from another perl script

use strict;
use warnings;
use  File::Find;
use  File::Basename;

if (!%main::closeOnErrorProtocols) { # get information about protocols
  do 'bin/protoconf.pl' || die "Error loading bin/protoconf.pl: $@\n";
}

################ Constants (exported to main program)
%main::headerNames = (
                      '%t' => 'title',
                      '%b' => 'banner',
                      '%d' => 'test_duration',
                      '%R' => 'rampup_time',
                      '%r' => 'reported_time',
                      '%c' => 'clients',
                      '%C' => 'requested_clients',
                      '%f' => 'report_directory',
                      '%o' => 'output_file',
                      '%p' => ' [problems]', # value will start with a space
                      );

# mstone timer field order/labels
#NAME 0       1         2          3          4    5    6    7
#SVN  Try     Error     BytesR     BytesW     Time TMin TMax TStd
@main::colNames = ('Try', 'Error', 'BytesR', 'BytesW',
                   'Time', 'TMin', 'TMax', 'TStd');

#NAME 0       1         2          3
#SVN  Try/min Error/min BytesR/sec BytesW/sec
@main::rateNames = ('Try/min', 'Error/min', 'BytesR/sec', 'BytesW/sec');

# handle common command line arguments
sub ParseCommonArgs {
  while ($ARGV[0] && ($ARGV[0] =~ /^-[cdhtw]/o)) { 
    my $cmd = shift @ARGV;
    if ($cmd eq '-c') {
      $main::saveDir = shift @ARGV;
    } elsif ($cmd eq '-h') {
      print "Usage: $0 [-c copyDir] [-d reportDir] [-t htmlFile] [-w wikiFile] dirs...\n";
      print "  -c copyDir    Directory to copy tests and reports to (~/www/goodruns)\n";
      print "  -d reportDir  Directory to write report into (results/20070807.1444-svn-load)\n";
      print "  -t htmlFile   Name of html report ('report.html')\n";
      print "  -w htmlFile   Name of wiki version of report ('report.wiki')\n";
      print "  dirs...       Test results (results/{20070807.1444,20070807.1448})\n";
      exit 0;
    } elsif ($cmd eq '-d') {
      $main::reportDir = shift @ARGV;
    } elsif ($cmd eq '-t') {
      $main::htmlFile = shift @ARGV;
    } elsif ($cmd eq '-w') {
      $main::wikiFile = shift @ARGV;
    } else {
      die "Unknown switch $cmd\n";
    }
  }
}

################ Utility Routines
# simple function to formatted a number into n, n K, n M, or n G
# copied from ../bin/util.pl
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

# Usage: PatternReplace (string, \%dict{pat -> str})
# Returns modified string.
sub PatternReplace {
  my $str = shift;
  my $dict = shift;
  #print "PatternReplace '$str': "; # DEBUG
  foreach my $k (keys %$dict) {
    my $n = $dict->{$k};
    #print "'$k'->'$n' ";        # DEBUG
    if (!defined $n) {
        print "Internal error: PatternReplace: no mapping for '$k'\n";
        next;
    }
    $str =~ s/$k/$n/g;
  }
  #print "-> '$str'\n"; # DEBUG
  return $str;
}


################ Parsing Routines
# Usage: ParseHeader \*FILE
# Returns \%(%t=>title, %b=>banner, %d=>duration, %R=>rampup, %r=>reported,
#           %c=>clients, %C=>requested_clients)
# Stops processing at second blank link
sub ParseHeader {
  my $infile = shift;

  my %results = ();
  my $blanks = 0;
  while (<$infile>) {
    chomp;
    ++$blanks unless ($_);
    last if ($blanks >= 2);
    if ($_ =~ m/^\t\t(\S.+)$/) {
      unless ($results{'%t'}) {
        $results{'%t'} = $1;    # title comes first
      } else {
        $results{'%b'} = $1;    # banner
      }
      next;
    }
    if ($_ =~ m/^Test duration: (\d+ \w+).  Rampup: (\d+ \w+).  Reported duration (\S+)/o) {
      $results{'%d'} = $1;
      $results{'%R'} = $2;
      $results{'%r'} = $3;
      #print STDERR "duration: $1 Rampup: $2 reported: $3\n"; # DEBUG
      next;
    }
    if ($_ =~ m/^Number of reporting clients: (\d+) of (\d+)/o) {
      $results{'%c'} = $1;
      $results{'%C'} = $2;
      #print STDERR "reporting clients: $1 of $2\n"; # DEBUG
      next;
    }
  }
  return \%results
}

# Helper ot strip off the protocol and just return the counter
# Typically passed to ParseTimerFields
sub StripProto {
    my $counter = shift;
    if ($counter =~ m/^(\S+):(.*)$/) {
        if (($1 ne "Total") && (!$main::protocol)) {
            #print "Found protocol '$1'\n"; # DEBUG
            $main::protocol = $1;
        }
        return $2;
    } else {
        print "Error parsing PROTO:TIMER from '$_'\n";
        return '';
    }
}

# Usage: ParseTimerFields (\*FILE, \%data{name -> (fields)}, [\&CleanFunc])
# Matches: /^(\S*NAME)\s+(.*)$/ then extracts fields from line
# Stops processing at first blank link
# Sample lines that we match
#SVN:total   210   0     0 0 1.447 0.000 17.426 3.304
#SVN:total/s 0.175 0.000 0 0
# CleanFunc must take a name and return a cleaned name.
#  Ex: "SVN:total" -> "total"
sub ParseTimerFields {
  my $infile = shift;
  my $results = shift;
  my $nameClean = shift;
  while (<$infile>) {
    chomp;
    last unless ($_);

    if ($_ =~ m/^(\S+:\S+)\s+(.*)$/) { # match a timer name
      my @dat = split /\s+/, $2;
      my $name = $1;
      $name = &$nameClean($1) if ($nameClean);
      #print STDERR "found: ($1) '$name' @dat\n";  # DEBUG
      $results->{$name} = \@dat;
    } else {
      #print STDERR "skip: $_\n";  # DEBUG
    }
  }
}

# Basically a general version of ParseTimerFields+SelectFields
# Usage: ParseFields \*FILE \@timerList \@fieldList
# Returns \@resultsList
# Stops processing at first blank link
sub ParseFields {
  my $infile = shift;
  my $timers = shift;
  my $fields = shift;

  my @results = ();
  while (<$infile>) {
    chomp;
    last unless ($_);
    foreach my $t (@$timers) {
      my @val = ($_ =~ m/^\s*$t\s*$/);
      if (@val) { # match a timer name
        #print STDERR "found: @val\n";  # DEBUG
        push @results, @val;
        last;
      }
    }
    #print STDERR "skip: $_\n";  # DEBUG
  }
  return \@results;
}


################ Select/print Routines
# Usage: SelectFields (\@timers, \@fields, \%data{timer -> (fields)})
# Each timer can either specify the fields or default to @fields.
# Example @timers: ("connect[4,7]")  # Gets connect time and std dev.
# Returns \@resultsList
sub SelectFields {
  my $timers = shift;
  my $fields = shift;
  my $data = shift;
  my @results = ();
  foreach my $tentry (@$timers) {
    my $t = $tentry;
    my @fieldList = @$fields;
    if ($t =~ m/(\S+)\s*\[(.+)\]/) {
      $t = $1;                  # just the field name
      @fieldList = split /,\s*/, $2; # the specific field list
    }
    unless (exists $data->{$t}) {
      print "Data field is not defined: '$t'\n";
      next;
    }
    my $flist = $data->{$t};
    #print "SelectFields{$t} -> @$flist [@fieldList]\n";                # DEBUG
    foreach my $f (@fieldList) {
      my $val = @$flist[$f];
      push @results, $val;
    }
  }
  return \@results;
}

# Usage: SelectHeader (\@timers, \@fields, \@value_names})
# Returns \@resultsList
# Just like SelectFields, but print header info
sub SelectHeader {
  my $timers = shift;
  my $fields = shift;
  my $cols = shift;
  my @results = ();
  foreach my $tentry (@$timers) {
    my $t = $tentry;
    my @fieldList = @$fields;
    if ($t =~ m/(\S+)\s*\[(.+)\]/) {
      $t = $1;                  # just the field name
      @fieldList = split /,\s*/, $2; # the specific field list
    }
    foreach my $f (@fieldList) {
      my $val = @$cols[$f];
      push @results, "$t:$val";
    }
  }
  return \@results;
}

# Usage:  PrintResultLine (\*STDOUT, $start, \@fieldList, $end)
# Returns \@fieldList (just like ParseTimeData would).
sub PrintResultLine {
  my $outFile = shift;
  my $start = shift;
  my $fields = shift;
  my $end = shift;
  my $separator = shift || die "PrintResultsLine: too few arguments.\n";
  my @AllField = ($start, @$fields);
  print $outFile join($separator, @AllField), "$end";
}


################ Routines that analyze test data
sub CheckStderr {
    my $dirname = shift;
    my $skipRE = shift;
    my $infile = "$dirname/stderr";

    unless (open ERRFILE, "<$infile") {
        return "Error opening '$infile'\n";
    }

    # regex to search for.  Each is a list and members will be added later.
    my @patlist = (['aborted'], ['error'], ['fail'], ['fault'], ['malformed'],
                  );
    my $ii = 0;
    foreach my $pat (@patlist) {
        $patlist[$ii][1] = 0;            # number of matching lines
        $patlist[$ii][2] = -1;           # time of first match
        my $str = $patlist[$ii][0];
        $patlist[$ii][3] = qr/$str/i;    # compiled regex
        $ii++;
    }

    my $timestamp = "";
    while (<ERRFILE>) {         # detect common problem indicators
        if (/$skipRE/o) {
            #print "Skipping: $_\n";   # DEBUG
            next;
        }
        if (/\S+\[\d+\]\s+t=(\d+):/oi) { # store last seen time
            $timestamp = $1
        }
        $ii = 0;
        foreach my $pat (@patlist) {
            if (/$patlist[$ii][3]/) {
                $patlist[$ii][1]++;
                $patlist[$ii][2] = $timestamp if (($timestamp)
                                                  && ($patlist[$ii][2] < 0));
            }
            $ii++;
        }
    }
    close(ERRFILE);
    my $status = "";
    $ii = 0;
    foreach my $pat (@patlist) {
        if ($patlist[$ii][1] > 0) {
            $status .= " '$patlist[$ii][0]':$patlist[$ii][1]";
            $status .= "\@$patlist[$ii][2]" unless ($patlist[$ii][2] < 0);
        }
        $ii++;
    }
    return $status;
}

# Do basic Sanity check on connections and clients.
# Usage: ConnectSanityCheck \%headers, \%fields, protocol [, stranded_limit]
# Stranded limit is what fraction of valid checkouts to allow (default 0)
# Returns string with problems (empty if none)
sub ConnectSanityCheck {
    my $gen_data = shift;
    my $fields = shift;
    my $proto = shift || "";
    my $stranded_limit = shift || 0;
    my $problems = "";

    my $conn_name = expandTimerText("conn", $proto);
    if (!exists $fields->{$conn_name}) {
        print "Warning: could not find timer '$conn_name'\n";
        return $problems;
    }
    my $dat = $fields->{$conn_name}[1];
    $problems .= " ${conn_name}_errors:$dat" if ($dat > 0);

    $dat = $fields->{$conn_name}[0] - $fields->{$conn_name}[1];
    if ($dat < $gen_data->{'%c'}) {
        $problems .= " not_all_client_connected:$dat";
    } else {
        my $logout_name = expandTimerText("logout", $proto);
        my $da2 = $fields->{$logout_name}[0];
        if (($dat -$da2) > ($dat * $stranded_limit)) {
            $problems .= " hung_connects:$dat!=$da2";
        }
    }

    return $problems;
}

# Insert text into a file after a tagline
# InsertTestIndex (filename, tagstring, newtext)
sub InsertIndexEntry {
  my $dir = shift || die "InsertIndexEntry: Missing argument";
  my $newtext = shift || die "InsertIndexEntry: Missing argument";
  my $filename = "$dir/index.html";
  my $foundit = 0;

  open(OLD, "<$filename") ||
    die "InsertIndexEntry: Could not open input $filename: $!";
  open(NEW, ">$filename+") ||
    die "InsertIndexEntry: Could not open output $filename+: $!";

  while (<OLD>) {
    print NEW $_;		# copy (including tagline)
    next unless (/^<!-- INSERT TAGS HERE/o); # matched tagline

    print NEW "$newtext\n";	# insert new text
    $foundit++;
    last;			# only change first occurance
  }

  if ($foundit) {		# copy rest of file
    while (<OLD>) {
      chomp;
      next if ($_ eq $newtext); # skip any duplicate lines
      print NEW "$_\n";
    }
  }

  close (OLD);
  close (NEW);
  if ($foundit) {
    chmod (0644, "$filename+");
    rename ("$filename", "$filename~");
    rename ("$filename+", "$filename");
    #print "Updated $filename\n"; # DEBUG
    return $foundit;
  } else {
    print "No change to $filename\n"; # DEBUG
    unlink ("$filename+");
    return 0;
  }
}

# Create an empty index file.
# Usage: MakeEmtpyIndex "/home/me/www"
sub MakeEmptyIndex {
  my $outDir = shift;
  open(INDEXNEW, ">$outDir/index.html") || 
    die "Couldn't open $outDir/index.html: $!";

  print INDEXNEW <<END;
<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 3.2//EN">
<HTML>
<TITLE>
MStone Final Results
</TITLE>
<HEAD>
</HEAD>
<BODY TEXT="#000000" BGCOLOR="#FFFFFF" LINK="#0000AA" VLINK="#440044" ALINK="#FF0000">
<TABLE BORDER=2>
<CAPTION> MStone Final Test Results </CAPTION>
<TR>
<TH>Run</TH> <TH>Title</TH> <TH> Notes </TH>
    <TH>Duration</TH> <TH>Clients</TH> <TH>Log</TH> <TH>Problems</TH> 
</TR>
<!-- INSERT TAGS HERE - DO NOT DELETE THIS LINE -->
</TABLE>
</BODY>
</HTML>
END
  close (INDEXNEW);
}

# Create new index entry from final run data
# This is just like what makeindex does, but with information from the test
# We use title+banner instead of testname+title (parse all.wld for testname?)
sub MakeIndexEntry {
  my $genData = shift;
  my $template =
    '<TR><TD><BR><A HREF="%f/results.html">%f</A></TD> <TD>%t</TD> <TD>%b</TD> <TD>%r</TD> <TD>%c</TD> <TD><A HREF="%f/stderr">stderr</A></TD><TD>%p</TD></TR>';
  return PatternReplace($template, $genData);
}

# Make sure directories and files are world readable
# Use as a "find" callback
sub WorldReadablePerms {
  die "Missing argument to WorldReadablePerms" unless ($_);
  my $perms = (stat($_))[2] & 07777;
  if ($_ eq '.') {
    if (!($perms & 05)) {
      my $p2 = $perms | 05;
      #printf ("Found directory $_ %o -> %o\n", $perms, $p2);
      chmod $p2, $_;
    }
  } else {
    if (!($perms & 04)) {
      my $p2 = $perms | 04;
      #printf ("Found file $_ %o -> %o\n", $perms, $p2);
      chmod $p2, $_;
    }
  }
}

# Copy a test run to a report results directory
# Example: CopyRun "results/20070605.1200", "/home/me/www", $indexEntry
sub CopyTestRun {
  my $fromDir = shift;
  my $destDir = shift;
  my $indexEntry = shift;

  # copy data (we try hard links first, then a regular copy)
  my $cmd = "cp -rpl $fromDir $destDir/ 2> /dev/null || cp -rp $fromDir $destDir/";
  #print "doing: $cmd\n";      # DEBUG
  system $cmd;
  # Insert index_entry
  if ($indexEntry) {
    MakeEmptyIndex ($destDir) unless (-r "$destDir/index.html");
    InsertIndexEntry ($destDir, $indexEntry);
  }

  my $base = basename ($fromDir);
  # TODO?  make test report files read only
  find (\&WorldReadablePerms, "$destDir/$base");
  # TODO?  compress tmp files (in background)
}
