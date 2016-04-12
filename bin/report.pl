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

# Generate the text and html reports from each run.
# This file deals with the summary data only

# TODO: Should be packages
do 'genplot.pl' || die "$@\n";

@preTestFiles = ();
@monitorFiles = ();
@postTestFiles = ();

sub walkSetupTotals {
    my $a = shift; my $f = shift; my $p = shift;
    if ($p =~ /(\w+):(\w+):$/) {
	my $tm = $2;
	if (!($finals{Total}->{$tm}->{$f})) {
	    $finals{Total}->{$tm}->{$f} = $a;
	} elsif ($f =~ /Min$/) {
	    $finals{Total}->{$tm}->{$f} = $a
		if (($a > 0.0) && ($a < $finals{Total}->{$tm}->{$f}));
	} elsif ($f =~ /Max$/) {
	    $finals{Total}->{$tm}->{$f} = $a if ($a > $finals{Total}->{$tm}->{$f});
	} else {
	    $finals{Total}->{$tm}->{$f} += $a;}
    }
    elsif ($p =~ /(\w+):$/) {
	$finals{Total}->{$f} += $a;
    }
}

sub setupTotals {
    # Figure out combined timers for "Total" protocol
    # We might do a smarter merge here (look at context and try to match order)
    # As long as the first protocol is a superset, it wont matter
    my @tnames;
    foreach $proto (@protocols) {
	foreach $n (@{$protocolFields{$proto}}) {
	    my $t = $n;
	    $t =~ s/([][{}*+?^.\/])/\\$1/g; # quote regex syntax
	    my $found = 0;
	    foreach $tn (@tnames) { # see if it is in the list already
		next unless ($tn =~ /$t/);
		$found = 1;
		last;
	    }
	    #print "proto $proto: Found $n\n" if ($found > 0);
	    next if ($found > 0);
	    #print "proto $proto: Add $n\n";
	    push @tnames, $n;	# add to list
	}
    }
    #print "'Total' timers @tnames\n";
    $protocolFields{"Total"} = \@tnames;

    # Create "Total" hashes
    $finals{Total} =  ArrayInstance->new();
    foreach $n (@{$protocolFields{"Total"}}) { # all timers
	my $t = $n;	# dont modify original list
	if ($t =~ /^\[(\w+)\]$/) { # Timer case, strip off brackets
	    $finals{Total}->{$1} = ArrayInstance->new();
	    #print "Creating Total timer field $1\n";
	} else {		# scalar
	    $finals{Total}->{$n} = 0;
	    #print "Creating Total scalar field $n\n";
	}
    }

    # Total finals array
    foreach $proto (@protocols) {
	foreach $t (@{$protocolFields{$proto}}) {
	    if ($t =~ /^\[(\w+)\]$/) { # Timer case, strip off brackets
		my $tm = $1;
		foreach $f (@timerFieldsAll) {
		    my $a = $finals{$proto}->{$tm}->{$f};
		    if (!($finals{Total}->{$tm}->{$f})) { # never touched
			$finals{Total}->{$tm}->{$f} = $a;
		    } elsif ($f =~ /Min$/) {
			$finals{Total}->{$tm}->{$f} = $a
			    if (($a > 0.0)
				&& ($a < $finals{Total}->{$tm}->{$f}));
		    } elsif ($f =~ /Max$/) {
			$finals{Total}->{$tm}->{$f} = $a
			    if ($a > $finals{Total}->{$tm}->{$f});
		    } else {
			$finals{Total}->{$tm}->{$f} += $a;
		    }
		}
	    } else {
		$finals{Total}->{$t} += $finals{$proto}->{$t};
	    }
	}
    }    
		    
    # Convert Time2 to standard deviation
    foreach $proto (@protocolsAll) {
	foreach $n (@{$protocolFields{$proto}}) {
	    my $t = $n;	# dont modify original list
	    if ($t =~ /^\[(\w+)\]$/) { $t = $1; } # strip off brackets
	    next unless ($finals{$proto}->{$t}); # proto doesnt have timer
	    next unless ($finals{$proto}->{$t}->{Try});
	    next unless ($finals{$proto}->{$t}->{Time2} > 0);
	    my $ss = $finals{$proto}->{$t}->{Time2};
	    my $tot = $finals{$proto}->{$t}->{Time};
	    my $n = $finals{$proto}->{$t}->{Try};
	    next unless ($n > 0); # skip if this is 0

	    my $var = ($ss - (($tot * $tot) / $n)) / $n;
	    print "$proto->$t var < 0: Time2=$ss Time=$tot n=$n\n"
              if (($var < 0) && ($params{DEBUG}));
	    $finals{$proto}->{$t}->{Time2} = ($var > 0) ? sqrt ($var) : 0.0;
	}
    }

    # Divide total times by trys to get averate time
    foreach $proto (@protocolsAll) {
	foreach $n (@{$protocolFields{$proto}}) {
	    my $t = $n;	# dont modify original list
	    if ($t =~ /^\[(\w+)\]$/) { $t = $1; } # strip off brackets
	    next unless ($finals{$proto}->{$t}); # proto doesnt have timer
	    ($finals{$proto}->{$t}->{Try}) || next;
	    $finals{$proto}->{$t}->{Time} /= $finals{$proto}->{$t}->{Try}
	}
    }
}

# The text version is designed to be machine processable
# commify and kformat are not used
sub genTextReport {
    fileBackup ($resultstxt);	# if processing as we go, backup old file
    # Open a text file to hold the results
    open(RESULTSTXT, ">$resultstxt") || 
	die "Couldn't open $resultstxt: $!";

    # Store results as text
    printf RESULTSTXT "---- MStone Results $params{TSTAMP} ----\n";

    printf RESULTSTXT "\t\t%s\n", $params{TITLE};
    printf RESULTSTXT "\t\t%s\n", $params{COMMENTS};
    printf RESULTSTXT "\n";
    printf RESULTSTXT "Test duration: %d %s.  Rampup: %d %s.  Reported duration %s\n",
    figureTimeNumber ($params{TIME}),
    figureTimeUnits ($params{TIME}, "minutes"),
    figureTimeNumber ($params{RAMPTIME}),
    figureTimeUnits ($params{RAMPTIME}, "seconds"),
    timeFormat ($realTestSecs);
    printf RESULTSTXT "Number of reporting clients: %s of %s\n",
    $reportingClients, $totalProcs;

    foreach $proto (@protocolsAll) {
	# do op counters
	printf RESULTSTXT "\n%-18s  ", $proto;
	foreach $f (@timerFieldsAll) {
	    #($f =~ m/^Time2$/o) && next;
	    printf RESULTSTXT "%13s", expandFieldText($f);
	}
	foreach $n (@{$protocolFields{$proto}}) {
	    my $t = $n;	# dont modify original list
	    unless ($t =~ /^\[(\w+)\]$/) { # scalar case
		#next;		# skip scalars for now
		# do scalar counters. Column should line up with "Try"
		printf RESULTSTXT "\n%-18s  ",
		$proto . ":" . expandTimerText($t, $proto);
		printf RESULTSTXT
		    " %12s", $finals{$proto}->{$t};
		next;
	    } else {		# strip off brackets
		$t = $1;
	    }
	    printf RESULTSTXT "\n%-18s  ",
            $proto . ":" . expandTimerText($t, $proto);
	    foreach $f (@timerFieldsAll) {
		#($f =~ m/^Time2$/o) && next;
		if ($f =~ m/Time/o) {
		    printf RESULTSTXT
			" %12.3f", $finals{$proto}->{$t}->{$f};
		} elsif ($f =~ m/Bytes/o) {
		    printf RESULTSTXT
			" %12.0f", $finals{$proto}->{$t}->{$f};
		} else {
		    printf RESULTSTXT
			" %12s", $finals{$proto}->{$t}->{$f};
		}
	    }
	}

	# do ops/sec
	printf RESULTSTXT "\n\n%-18s  ", $proto;
	foreach $f (@timerFieldsAll) {
	    ($f =~ m/^Time/o) && next;
	    printf RESULTSTXT "%9s/%s", expandFieldText($f),
                ($f =~ m/^Bytes/o) ? "sec" : "min";
	}
	foreach $n (@{$protocolFields{$proto}}) {
	    my $t = $n;	# dont modify original list
	    unless ($t =~ /^\[(\w+)\]$/) { # scalar case
		#next;		# skip scalars for now
		# do scalar counter/min. Column should line up with "Try"
		printf RESULTSTXT "\n%-18s  ",
		$proto . ":" . expandTimerText($t, $proto) . "/m";
		printf RESULTSTXT
		    "%13.3f", $finals{$proto}->{$t} / $realTestSecs;
		next;
	    } else {
		$t = $1;
	    }
	    printf RESULTSTXT "\n%-18s  ",
            $proto . ":" . expandTimerText($t, $proto) . "/m";
	    foreach $f (@timerFieldsAll) {
		($f =~ m/^Time/o) && next;
		if ($f =~ m/Bytes/o) {
		    printf RESULTSTXT
			"%13d",
			$finals{$proto}->{$t}->{$f} / $realTestSecs;
		} else {
		    printf RESULTSTXT
			"%13.3f",
			$finals{$proto}->{$t}->{$f} / ($realTestSecs / 60);
		}
	    }
	}
	printf RESULTSTXT "\n\n";
    }

    if ($params{SYSCONFIG}) {
	print RESULTSTXT "\nSytem config details\n";
	if (($params{SYSCONFIG} =~ m/^\S+$/o)
	    && (open(SCFILE, "<$params{SYSCONFIG}"))) {
	    while (<SCFILE>) {
		(m/^<\S+>\s*$/o) && next; # skip HTML only on them
		s/<\S+>//g;	# trim out obvious HTML commands
		s/<!--.*-->//g;	# trim out HTML comments
		print RESULTSTXT $_;
	    }
	    close(SCFILE);
	} else {
	    my $l = $params{SYSCONFIG};	# filter similar to above
	    $l =~ s/<\S+>//g;	# trim out obvious HTML commands
	    $l =~ s/<!--.*-->//g; # trim out HTML comments
	    $l =~ s/\\\n/\n/g;	# turn quoted newline to plain newline
	    print RESULTSTXT $l;
	}
    }

    close(RESULTSTXT);
}

# Write a line or paragraph with log files.
# There is no output if there aren't any log files.
# Usage:  genLogLine (\*FILE, "prefix", \@files, "postfix\n")
sub genLogLine {
  my $outFile = shift;
  my $prefix = shift;
  my $fileList = shift;
  my $postfix = shift;

  my @logs = ();
  foreach $f (@$fileList) {
    $f =~ s/$resultdir\///o; # strip directory out
    my $n = $f;
    $n =~ s/.txt$//o; # strip extension off
    push @logs, "<A HREF=\"$f\">$n</a>";
  }
  if (@logs) {
    print $outFile $prefix, join(", ", @logs), $postfix;
  }
}

# Write the main part of the HTML page
sub genHTMLReportStart {
    fileBackup ($resultshtml);	# if processing as we go, backup old file
    # Open an html file to hold the results
    open(RESULTSHTML, ">$resultshtml") || 
	die "Couldn't open $resultshtml: $!";

    print RESULTSHTML <<END;
<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 3.2//EN">
<HTML>
<A NAME=TitleSection>
<TITLE>
MStone Results $params{TSTAMP}
</TITLE>
</A>
<HEAD>
</HEAD>
<BODY TEXT="#000000" BGCOLOR="#FFFFFF" LINK="#0000AA" VLINK="#440044" ALINK="#FF0000">
<CENTER>
<HR NOSHADE WIDTH="100%">
<H1>MStone Results $params{TSTAMP}</H1>
<H2>$params{TITLE}</H2>
<I>$params{COMMENTS}</I>
<HR WIDTH="100%">
</CENTER>

END
    printf RESULTSHTML "<BR><B>Test duration:</B> %s,  ",
    timeFormat (figureTimeSeconds ($params{TIME}));
    printf RESULTSHTML "<B>Rampup:</B> %s,  ",
    timeFormat (figureTimeSeconds ($params{RAMPTIME}));
    printf RESULTSHTML "<B>Reported duration:</B> %s.\n",
    timeFormat ($realTestSecs);
    if ($resultprev) {
      my $name = $resultprev;
      $name =~ s/.*\///;
      printf RESULTSHTML "&nbsp; <A HREF=\"../$name/results.html\">Previous run</A>.";
    }
    printf RESULTSHTML " &nbsp; <A HREF=\"../index.html\">Index</A>.\n";

    printf RESULTSHTML "<BR><B>Reporting clients:</B> %s of %s\n",
    commify ($reportingClients), commify ($totalProcs);

    print RESULTSHTML <<END;
<BR>
Test <A HREF="all.wld">complete workload</a> description.  
Filtered <A HREF="work.wld">workload</a> description.
<BR>
Plain <A HREF="results.txt">text version</a> of results.  
Log of <A HREF="stderr">stderr</a> and debugging output.
<BR>

<A NAME=MonitoringSection></A>
END

    {				# list user requested logging
	@preTestFiles = <$resultdir/*-pretest.txt> unless (@preTestFiles);
        genLogLine (\*RESULTSHTML,
                    "Pre test logs: ", \@preTestFiles, "<BR>\n");
	@monitorFiles = <$resultdir/*-monitor.txt> unless (@monitorFiles);
        genLogLine (\*RESULTSHTML,
                    "Monitoring logs: ", \@monitorFiles, "<BR>\n");
	@postTestFiles = <$resultdir/*-posttest.txt> unless (@postTestFiles);
        genLogLine (\*RESULTSHTML,
                    "Post test logs: ", \@postTestFiles, "<BR>\n");
	my @stackFiles = <$resultdir/core-*/stack*>;
        push @stackFiles, "$resultdir/all-core.txt" if (@stackFiles);
        genLogLine (\*RESULTSHTML,
                    "Stack traces: ", \@stackFiles, "<BR>\n");
    }

    #print RESULTSHTML
	#"<CENTER><H2>Results per protocol</H2></CENTER>\n";

    foreach $proto (@protocolsAll) {
	printf RESULTSHTML "<A NAME=%sTable></A>\n", $proto;
	printf RESULTSHTML
	    "<TABLE BORDER=2 CELLSPACING=2 CELLPADDING=2 COLS=%d WIDTH=\"95%%\">",
	    2+$#{@{$protocolFields{$proto}}};
	print RESULTSHTML
	    "<CAPTION>$proto Counters</CAPTION>\n";
	# do op counters
	print RESULTSHTML
	    "<TR><TH>$proto</TH>\n";
	foreach $f (@timerFieldsAll) {
	    #($f =~ m/^Time2$/o) && next;
	    printf RESULTSHTML "<TH>%s</TH> ", expandFieldHtml($f);
	}
	print RESULTSHTML
	    "</TR>\n";
	foreach $n (@{$protocolFields{$proto}}) {
	    my $t = $n;	# dont modify original list
	    unless ($t =~ /^\[(\w+)\]$/) { # scalar case
		next;		# skip scalars for now
		# do scalar counters. Column should line up with "Try"
		printf RESULTSHTML "<TR ALIGN=RIGHT><TH>%s</TH>\n",
		expandTimerHtml($t, $proto, "Timer");
		printf RESULTSHTML
		    "<TD>%s</TD> ",
		    commify ($finals{$proto}->{$t});
		next;
	    } else {
		$t = $1;
	    }
	    printf RESULTSHTML "<TR ALIGN=RIGHT><TH>%s</TH>\n",
            expandTimerHtml($t, $proto, "Timer");
	    foreach $f (@timerFieldsAll) {
		#($f =~ m/^Time2$/o) && next;
		if ($f =~ m/Time/o) {
		    printf RESULTSHTML
			"<TD>%s</TD> ",
			tformat ($finals{$proto}->{$t}->{$f});
		} elsif ($f =~ m/Bytes/o) {
		    printf RESULTSHTML
			"<TD>%s</TD> ",
			kformat ($finals{$proto}->{$t}->{$f});
		} else {
		    printf RESULTSHTML
			"<TD>%s</TD> ",
			commify ($finals{$proto}->{$t}->{$f});
		}
	    }
	    print RESULTSHTML "</TR>\n";
	}

	# do ops/sec
	print RESULTSHTML
	    "<TR><TH>$proto</TH>\n";
	foreach $f (@timerFieldsAll) {
	    ($f =~ m/^Time/o) && next;
	    printf RESULTSHTML "<TH>%s/%s</TH> ", expandFieldText($f),
                ($f =~ m/^Bytes/o) ? "sec" : "min";
	}
	print RESULTSHTML
	    "</TR>\n";
	foreach $n (@{$protocolFields{$proto}}) {
	    my $t = $n;	# dont modify original list
	    unless ($t =~ /^\[(\w+)\]$/) { # scalar case
		next;		# skip scalars for now
		# do scalar counters. Column should line up with "Try"
		printf RESULTSHTML "<TR ALIGN=RIGHT><TH>%s</TH>\n",
                expandTimerHtml($t, $proto, "Rate");
		printf RESULTSHTML
		    "<TD>%.3f</TD> ",
		    $finals{$proto}->{$t} / $realTestSecs;
		next;
	    } else {
		$t = $1;
	    }
	    printf RESULTSHTML "<TR ALIGN=RIGHT><TH>%s</TH>\n",
            expandTimerHtml($t, $proto, "Rate");
	    foreach $f (@timerFieldsAll) {
		($f =~ m/^Time/o) && next;
		if ($f =~ m/Bytes/o) {
		    printf RESULTSHTML
			"<TD>%s</TD> ",
			kformat ($finals{$proto}->{$t}->{$f} / $realTestSecs);
		} else {
		    printf RESULTSHTML
			"<TD>%.3f</TD> ",
			$finals{$proto}->{$t}->{$f} / ($realTestSecs / 60);
		}
	    }
	    print RESULTSHTML "</TR>\n";
	}
	printf RESULTSHTML "</TABLE> <BR>\n\n";
    }

    print RESULTSHTML <<END;

<BR>

<CENTER>
<A NAME=GraphSection></A>
END
}

%genplotGraphs = ();

# Call genplot; and, if a graph is generated, insert the HTML reference to it
sub genHTMLReportGraph {
    my $name = shift;
    my $title = shift;
    my $label = shift;
    my $protos = shift || die "genHTMLReportGraph: '$name' missing protocols";
    my $field = shift;
    my $vars = shift || die "genHTMLReportGraph: '$name' missing vars";

    if ($genplotGraphs{$name}) {
	print "Graph $name has already been generated.\n";
	return;
    }

    $genplotGraphs{$name} = $title;

    # Delineate and tag each graph
    print RESULTSHTML "<A NAME=$name><HR SIZE=4 WIDTH=\"90%\"></A>\n";
    if (genPlot ($name, $title, $label, $protos, $field, $vars) > 0) {
	print RESULTSHTML <<END;
<P><H3>$title</H3>
<IMG SRC=$name.$params{IMAGETYPE} ALT="$label"></P>
END
    } else {
	print RESULTSHTML "<BR>Graph \"$name\" contained no data (@{$vars}).<BR>\n";
    }
	
}

# Write the final parts of the HTML page
sub genHTMLReportEnd {
    print RESULTSHTML <<END;
<!-- INSERT IMAGES HERE - DO NOT DELETE THIS LINE -->
</CENTER>
<A NAME=EndSection></A>
END

    if ($params{SYSCONFIG}) {
	print RESULTSHTML "<HR WIDTH=\"100%\">";
	print RESULTSHTML "<CENTER><H2>Details</H2></CENTER>\n";
	if (($params{SYSCONFIG} =~ m/^\S+$/o)
	    && (open(SCFILE, "<$params{SYSCONFIG}"))) { # see if its a file
	    while (<SCFILE>) {
		print RESULTSHTML $_;
	    }
	    close(SCFILE);
	} else {		# output text directly
	    my $l = $params{SYSCONFIG};
	    $l =~ s/\\\n/\n/g;	# turn quoted newline to plain newline
	    print RESULTSHTML $l;
	}
    }

    if ($params{EXTRAFILES}) {  # show user generated graphs
      print RESULTSHTML "<HR WIDTH=\"100%\">";
      print RESULTSHTML "<CENTER><H3>Extra Files</H3></CENTER>\n";
      foreach my $g (split (/\s+/, $params{EXTRAFILES})) {
        my @flist = glob "$resultdir/$g";
        #print "Extra glob: $resultdir/$g -> @flist\n"; # DEBUG
        next unless (@flist);
        my $text = "<P>$g<BR>\n";       # each glob is nominally a paragraph
        foreach my $f (@flist) {
          $f =~ s/$resultdir\///o;
          my $name = $f;          # get short version of name
          $name =~ s/^.*\///o;
          
          if ($f =~ m/\.(gif|jpg|jpeg|png|tif|tiff)$/) {
            $text .= "<IMG SRC=\"$f\" ALT=\"$name\"><\P>\n<P>";
          } else {
            $text .= "<A HREF=\"$f\">$name</A>\n";
          }
        }
        print RESULTSHTML "$text</P>\n";
      }
    }
    print RESULTSHTML <<END;

<HR NOSHADE WIDTH="100%">

</BODY>
</HTML>
END
    close(RESULTSHTML);
}

# Actually generate the standard stuff
setupTotals();
genTextReport();
@preTestFiles = processScripts("PRETEST");
@monitorFiles = processScripts("MONITOR");
@postTestFiles = processScripts("POSTTEST");

genHTMLReportStart();

my $graphCount = 0;
foreach $section (@workload) {
    next unless ($section->{sectionTitle} =~ /GRAPH/i);
    my $name = $section->{sectionParams};
    $name =~ s/name=\s*//;	# strip off initial bit
    my @varlist = split (/[\s,]+/, $section->{VARIABLES});

    $graphCount++;
    genHTMLReportGraph ($name, $section->{TITLE}, $section->{LABEL}, 
			($section->{FIELD} =~ /Time/o)
			? \@protocols : \@protocolsAll,
			$section->{FIELD}, \@varlist);
}

if ($graphCount <= 0) {		# use built ins

# generate the graphs we want
# NOTE: the first argument (name), must be unique; sets file name
    genHTMLReportGraph ("connects",
			"Number of connections attempted", "Connections/sec",
			\@protocolsAll, "Try", ["conn" ]);
    genHTMLReportGraph ("connections",
			"Total connections", "Connections",
			\@protocolsAll, "", ["connections" ]);
    genHTMLReportGraph ("errors",
			"Number of connection errors", "Errors/sec",
			\@protocolsAll, "Error", ["conn", "banner", "login", "logout" ]);
    genHTMLReportGraph ("retrieves",
			"Number of messages read", "Messages/sec",
			\@protocolsAll, "Try", ["retrieve" ]);
    genHTMLReportGraph ("submits",
			"Number of messages written", "Messages/sec",
			\@protocolsAll, "Try", ["submit" ]);
    genHTMLReportGraph ("commands",
			"Protocol commands", "Commands/sec",
			\@protocolsAll, "Try", ["cmd" ]);
    genHTMLReportGraph ("readBytes",
			"Bytes read", "Bytes/sec",
			\@protocolsAll, "BytesR", ["login", "banner", "cmd", "retrieve", "submit", "logout" ]);
    genHTMLReportGraph ("writeBytes",
			"Bytes written", "Bytes/sec",
			\@protocolsAll, "BytesW", ["login", "banner", "cmd", "retrieve", "submit", "logout" ]);
    genHTMLReportGraph ("msgTime",
			"Message transfer time", "Seconds per message",
			\@protocols, "Time", ["cmd", "submit", "retrieve" ]);
    genHTMLReportGraph ("setupTime",
			"Connection setup time", "Seconds per connection",
			\@protocols, "Time", ["conn", "banner", "login" ]);
    genHTMLReportGraph ("blocks",
			"Number of mstone blocks executed", "Blocks/sec",
			\@protocolsAll, "", ["blocks" ]);
}

if ($params{ADDGRAPHS}) {	# pick up additional graphs
    my @graphs = ();
    readWorkloadFile ($params{ADDGRAPHS}, \@graphs);
    foreach $section (@graphs) {
	next unless ($section->{sectionTitle} =~ /GRAPH/i);
	my $name = $section->{sectionParams};
	$name =~ s/name=\s*//;	# strip off initial bit
	my @varlist = split (/[\s,]+/, $section->{VARIABLES});

	$graphCount++;
	genHTMLReportGraph ($name, $section->{TITLE}, $section->{LABEL}, 
			    ($section->{FIELD} =~ /Time/o)
			    ? \@protocols : \@protocolsAll,
			    $section->{FIELD}, \@varlist);
    }
}


genHTMLReportEnd();

return 1;
