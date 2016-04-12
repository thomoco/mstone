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

# Look at the test parameters and create a top level index
# You can create an index from past runs like this (backup first!):
# cp -b results/index.html results/index-save.html
# perl -Ibin bin/makeindex.pl
# Or just get specific files using a pattern:
# for w in results/2*.*/all.wld ; do perl -Ibin bin/makeindex.pl -w $w ; done

sub writeindexentry {
  my $entry = "";

  $entry .= "<TR><TD><BR><A HREF=\"$params{TSTAMP}/results.html\">$params{TSTAMP}</A></TD>";
  $entry .= "<TD>$params{TESTNAME}</TD>\n";
  $entry .= "<TD>$params{TITLE}</TD>\n";
  $entry .= "<TD>$params{TIME}</TD>\n";
  $entry .= "<TD>$params{CLIENTCOUNT}</TD>\n";
  $entry .= "<TD><A HREF=\"$params{TSTAMP}/all.wld\">workload</A></TD>\n";
  $entry .= "<TD><A HREF=\"$params{TSTAMP}/stderr\">stderr</A></TD></TR>\n";

  if (-r "$resultbase/index.html") {
    fileInsertAfter ("$resultbase/index.html",
		     "^<!-- INSERT TAGS HERE",
		     $entry);
  } else {			# create index from scratch
    system ("cd $resultbase; ln -s ../doc .");
    open(INDEXNEW, ">$resultbase/index.new") || 
	die "Couldn't open $resultbase/index.new: $!";

    print INDEXNEW <<END;
<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 3.2//EN">
<HTML>
<TITLE>
MStone Results
</TITLE>
<HEAD>
</HEAD>
<BODY TEXT="#000000" BGCOLOR="#FFFFFF" LINK="#0000AA" VLINK="#770033" ALINK="#FF0000">
<A HREF=$mstoneURL>MStone documentation</A><BR>
<TABLE BORDER=2>
<CAPTION> MStone Results Index </CAPTION>
<TR>
<TH>Run</TH> <TH>Testname</TH> <TH> Title </TH>
    <TH>Duration</TH> <TH>Clients</TH> <TH>Details</TH> <TH>Error log</TH> 
</TR>
<!-- INSERT TAGS HERE - DO NOT DELETE THIS LINE -->
END

    print INDEXNEW $entry;	# put in this entry

    print INDEXNEW <<END;
</TABLE>
</BODY>
</HTML>
END
    close (INDEXNEW);
    fileBackup ("$resultbase/index.html");
    rename ("$resultbase/index.new", "$resultbase/index.html");
    print "Created $resultbase/index.html\n";
  }

  return 1;
}

if ($resultbase) {              # config already read: we're a sub-process
  writeindexentry();
} else {                        # stand alone script
  do 'args.pl'|| die $@;
  parseArgs();		# parse command line

  if ($params{WORKLOAD}) {      # a workload was specified
    writeindexentry();
  } elsif (-r "$resultbase/index.html") { # safety check for user intent.
    print "No workload was specified and $resultbase/index.html exists.\n";
    print "Move aside $resultbase/index.html if you want to re-generate it.\n";
    0;
  } else {                      # generate the entire index
    # Add all existing entries from the saved workloads
    @resall = <$resultbase/2*.*/all.wld>;
    foreach $filefull (@resall) {
      $params{WORKLOAD} = "";
      $params{TESTNAME} = "";
      $params{DEBUG} = 0;
      @ARGV = ( "-w", $filefull);
      parseArgs();
      writeindexentry();
    }
  }
}
