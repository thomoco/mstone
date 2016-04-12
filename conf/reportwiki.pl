# 'do' this from another perl script
# Generate a report from one or more mstone runs (return 0 if all runs good)
# The wiki report is always generated.
# If $htmlFile is define (-t FILE), generate a html report as well
# If $saveDir is defined (-c DIR), copy the results there, and index
# Dan Christian.  June 2007
use warnings;
die "Usage: $0 [options (use -h for list)] dirs...\n" unless ($#ARGV >= 0);
do 'bin/reportutil.pl' || die "Error loading bin/reportutil.pl: $@\n";
die "Internal error\n" unless (@colNames); # shuts up odd warning
my $status = 0;

################ Configure report contents ################
# Generate a table for our wiki
# Uses $www as the URL top level 
# See reportutil.pl:%headerNames for the list of special characters
my $wikiPreamble = "Wiki table start\n";
# Start each table with:
my $wikiTableStart = "| %t | %r%p";
# Start each line with:
my $wikiStart = "| [[$www/%f/results.html][%t]] | %r%p";
# Separator between elements
my $wikiSep = " |  ";
# End each line with:
my $wikiEnd = " |\n";
my $wikiPostscript = "Wiki table end\n";

# Generate a table entry in html
# All URL references are relative, so $www isn't used.
# See reportutil.pl:%headerNames for the list of special characters
my $htmlPreamble = "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2//EN\">
<HTML>\n<TITLE>%o</TITLE>\n<HEAD>%o</HEAD>\n<BODY>
<TABLE BORDER=2>\n<CAPTION> MStone Test Sequence Report </CAPTION>\n";
# Label start of table with
my $htmlTableStart = "<TR><TD><BR>%t</TD><TD> ";
# Start each line with:
my $htmlStart = "<TR><TD><BR><A HREF=\"../%f/results.html\">%t</A></TD><TD> ";
# Separator between elements
my $htmlSep = " </TD><TD> ";
# End each line with:
my $htmlEnd = " </TD> <TD> %p </TD> </TR>\n";
my $htmlPostscript = "</TABLE>\n</BODY>\n</HTML>\n";

# Pattern to ignore when scanning stderr
my $skipRE = '/usr/bin.*/xauth|Handling commit error:|cd /var/tmp;';

ParseCommonArgs();

if ($htmlFile) {
  my $filename = $htmlFile;
  $filename = "$reportDir/$htmlFile" if (($htmlFile !~ /^\//o) && ($reportDir));
  open HTMLFILE, ">$filename" || die "Could not open file $filename\n";
  $filename =~ s/results\/// if (($htmlFile !~ /^\//o) && ($reportDir));
  my %hdr = ('%o'=> $filename);
  print HTMLFILE PatternReplace($htmlPreamble, \%hdr);
}

if ($wikiFile) {
  my $filename = $wikiFile;
  $filename = "$reportDir/$wikiFile" if (($wikiFile !~ /^\//o) && ($reportDir));
  open WIKIFILE, ">$filename" || die "Could not open file $filename\n";
  $filename =~ s/results\/// if (($wikiFile !~ /^\//o) && ($reportDir));
  my %hdr = ('%o'=> $filename);
  print WIKIFILE PatternReplace($wikiPreamble, \%hdr);
}

# Setup column labels
my $reportHeader = SelectHeader(\@timers, \@timeFields, \@colNames);
if (@rates) {
  my $rateHeader = SelectHeader(\@rates, \@rateFields, \@rateNames);
  push @$reportHeader, @{$rateHeader};
}
push @$reportHeader, ('RX_bytes', 'TX_bytes') if (@netTimers);

if ($customResultsCallback) {
  $reportHeader = &$customResultsCallback($reportHeader);
}
if ($htmlFile) {
  PrintResultLine (\*HTMLFILE,
                   PatternReplace($htmlTableStart, \%headerNames),
                   $reportHeader,
                   PatternReplace($htmlEnd, \%headerNames), $htmlSep);
}
if ($wikiFile) {
  PrintResultLine (\*WIKIFILE,
                   PatternReplace($wikiTableStart, \%headerNames),
                   $reportHeader,
                   PatternReplace($wikiEnd, \%headerNames), $wikiSep);
}

foreach my $dirname (@ARGV) {   # loop through each directory
  my $problems = CheckStderr($dirname, $skipRE);

  my $infile = "$dirname/results.txt";
  unless (open INFILE, "<$infile") {
    print STDERR "No result file for $dirname $problems\n";
    next;
  }
  my $headerData = ParseHeader(\*INFILE);
  my %fields = ();
  my %rateFields = ();
  if ($timers[0] =~ /.+:.+/) {  # user gave us PROTO:TIMER
    ParseTimerFields(\*INFILE, \%fields);
    ParseTimerFields(\*INFILE, \%rateFields);
  } else {            # Single protocol reports  strip off "protocol:"
    ParseTimerFields(\*INFILE, \%fields, \&StripProto);
    ParseTimerFields(\*INFILE, \%rateFields, \&StripProto);
  }
  #foreach my $k (keys %fields) { # DEBUG
  #  print "fields{$k} \t-> @{$fields{$k}}\n";
  #}
  #foreach my $k (keys %rateFields) { # DEBUG
  #  print "rateFields{$k} \t-> @{$rateFields{$k}}\n";
  #}
  unless (%$headerData && %fields) {
    print STDERR "No test data for $dirname $problems\n";
    next;
  }

  $problems .= ConnectSanityCheck($headerData, \%fields, $main::protocol, 0.01);
  #### Do extra test validation here ####
  my $checkout_name = expandTimerText("checkout", $main::protocol);
  if (exists $fields{$checkout_name}) {
      my $try = $fields{$checkout_name}[0];
      my $err = $fields{$checkout_name}[1];
      $problems .= " ${checkout_name}_errors:$err" if ($err > (0.01 * $try));
  } else {
      print "Warning: Could not find timer '$checkout_name'\n";
  }

  my $fieldResults = SelectFields(\@timers, \@timeFields, \%fields);
  if (@rates) {
    my $rateResults = SelectFields(\@rates, \@rateFields, \%rateFields);
    push @$fieldResults, @{$rateResults};
  }

  if (@netTimers) {             # Add Network bandwidth fields
    my @netTotal = (0.0, 0.0);  # total bytes: (RX, TX)
    foreach my $i (<$dirname/*-ifconfig.txt>) {
      if ($i =~ m/-log-ifconfig.txt/) {
        next;
      }
      unless (open IFCONFIGFILE, "<$i") {
        print STDERR "Error opening $i (to parse network data)\n";
        next;
      }
      my $netData = ParseFields(\*IFCONFIGFILE, \@netTimers, \@netFields);
      next unless (@$netData);
      my $rx = $netData->[0] - $netData->[1];
      my $tx = $netData->[2] - $netData->[3];
      # account for counter wrap (Note: if > 6GB of data, this is low by N*4GB)
      $rx += 4*1024*1024*1024 if ($rx < 0);
      $tx += 4*1024*1024*1024 if ($tx < 0);
      $netTotal[0] += $rx;
      $netTotal[1] += $tx;
    }
    if (@netTotal) {
      push @$fieldResults, (kformat($netTotal[0]), kformat($netTotal[1]));
    }
    close(IFCONFIGFILE);
  }

  # Now output a line with a summary of the test results
  $headerData->{'%p'} = $problems;
  my $name = $dirname;          # get short version of name
  $name =~ s/\/$//o;
  $name =~ s/^.*\///o;
  $headerData->{'%f'} = $name;
  if ($customResultsCallback) {
    $reportHeader = &$customResultsCallback($fieldResults,
                                            \%fields, \%rateFields, $dirname);
  }
  if ($htmlFile) {
    $headerData->{'%o'} = $htmlFile;
    PrintResultLine (\*HTMLFILE,
                     PatternReplace($htmlStart, $headerData), $fieldResults,
                     PatternReplace($htmlEnd, $headerData), $htmlSep);
  }
  if ($wikiFile) {
    $headerData->{'%o'} = $wikiFile;
    PrintResultLine (\*WIKIFILE,
                     PatternReplace($wikiStart, $headerData), $fieldResults,
                     PatternReplace($wikiEnd, $headerData), $wikiSep);
  }

  if ($saveDir) {
    CopyTestRun ($dirname, $saveDir, MakeIndexEntry ($headerData));
  }
  if ($problems) {
    print STDERR "$name has problems: $problems\n";
    $status += 1;
  }
}

if ($htmlFile) {
  print HTMLFILE $htmlPostscript;
  close HTMLFILE;
  chmod (0644, $htmlFile);
}

if ($wikiFile) {
  print WIKIFILE $wikiPostscript;
  close WIKIFILE;
}

if ($htmlFile) {   # put link to report in index
  my $name = $htmlFile;         # get short version of name
  if (($reportDir) && ($name =~ m/report.html$/o)) {
    $name = $reportDir;
  } else {
    $name =~ s/\.\w*$//o;         # strip extension
  }
  $name =~ s/^.*\///o;          # strip path
  my $filename = $htmlFile;
  if (($htmlFile !~ /^\//o) && ($reportDir)) {
    $filename = "$reportDir/$htmlFile";
    $filename =~ s/results\///;
  }

  my $report = "<TR><TD><BR><A HREF=\"$filename\">$name</A></TD></TR>";
  InsertIndexEntry ("results", $report);
  if ($saveDir) {
    CopyTestRun ($reportDir, $saveDir) if ($reportDir);
    InsertIndexEntry ($saveDir, $report);
  }
}

exit $status;
