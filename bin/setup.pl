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
# Copyright (C) 1999-2000 Netscape Communications Corporation. All
# Rights Reserved.
# 
# Contributor(s):	Dan Christian <robodan@netscape.com>
#			Marcel DePaolis <marcel@netcape.com>
#			Jim Salter <jsalter@netscape.com>
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

# usage: setup.pl setup|cleanup|checktime|timesync [workload]
# message files are expected to be in ./data/ and end with ".msg" or ".dat"

print "Netscape Mailstone.\nCopyright (c) 1997-2000 Netscape Communications Corp.\n";

$mode = shift;			# mode must be first

# this parses the command line and sets many defaults
do 'args.pl'|| die $@;
sub warn_system;
sub die_system;

parseArgs();			# parse command line

setConfigDefaults();		# setup RSH and RCP

$cpcmd = "cp -f";		# copy files... dir
$rmcmd = "rm -f";		# remove files...

die "Must specify workload file" unless (@workload);

# Add or change client machines
# Usage: configClients "filename"
sub configClients {
    my $filename = shift;

    print "\n    You can enter multiple machines like this: host1,host2\n";
    my @d = <bin/*/bin>;
    if (@d) {
	my @d2;
	foreach (@d2 = @d) { s/^bin\/// }
	foreach (@d = @d2) { s/\/bin$// }
	print "    These OS versions are available:\n@d\n";
    }

    foreach $section (@workload) {
	next unless ($section->{sectionTitle} =~ /CLIENT/i);
	my $slist = $section->{sectionParams};
	$slist =~ s/HOSTS=\s*//; # strip off initial bit
	my $arch = "default OS";
	$arch = $section->{ARCH} if ($section->{ARCH});
	print "\nWhat is the name of the client(s) for $arch [$slist]: ";
	my $ans = <STDIN>; chomp $ans;
	if ($ans) {
	    $ans =~ s/\s//g;	# strip any whitespace
	    fileReplaceText ($filename, "<CLIENT", $slist, $ans);
	}
    }
    while (1) {
	print "\nWhat additional client(s) [none]: ";
	my $ans = <STDIN>; chomp $ans;
	last unless ($ans);	# done
	last if ($ans =~ /^none$/i);
	$ans =~ s/\s//g;	# strip any whitespace
	my $block = "\n<CLIENT HOSTS=$ans>\n";

	print "What OS type [default]: ";
	my $ans = <STDIN>; chomp $ans;
	$block .= "  Arch\t$ans\n" if ($ans && !($ans =~ /^default$/i));

	$block .= "</CLIENT>\n";
	fileInsertAfter ($filename, "^</CLIENT>", $block);
    }
}

# Create a user ldif file
sub configUserLdif {
    my $filename = shift;

    my $name = "conf/$defaultSection->{SERVER}.ldif";
    print "\nWhat file to you want to create [$name]? ";
    $ans = <STDIN>; chomp $ans;
    $name = $ans if ($ans);

    my $mode = "users";
    print "\nDo you want to create a broadcast account [y]? ";
    $ans = <STDIN>;
    $mode .= " broadcast" unless ($ans =~ /^n/i);

    my $basedn = $defaultSection->{SERVER};	# pick a default
    $basedn =~ s/^.*?\.//;	# strip off before first dot
    $basedn = "o=$basedn";

    print "\nWhat is LDAP base DN [$basedn]? ";
    $ans = <STDIN>; chomp $ans;
    $basedn = $ans if ($ans);

    my $args = $params{MAKEUSERSARGS};

    print "\n    Common additional makeusers arguments:\n";
    print "\t-s storeName -x storeCount \tMultiple store partitions\n";
    print "\t[-3|-4]                    \tConfigure for NSMS 3.x or 4.x\n";
    print "Any extra arguments to makeusers [$args]? ";
    $ans = <STDIN>; chomp $ans;
    $args = $ans if ($ans);

    my $perlbin = "/usr/bin/perl";

    $params{DEBUG} &&
	print "$perlbin -Ibin -- bin/makeusers.pl $mode -w $filename -b '$basedn' -o $name $args\n";

    print "\nGenerating $name (this can take a while)\n";
    warn_system "$perlbin -Ibin -- bin/makeusers.pl $mode -w $filename -b '$basedn' -o $name $args";
    print "LDIF generation complete.  See $name\n";
    print "\tSee the manual or INSTALL to create users using the LDIF file.\n";
}

# This uses a match pattern plus text to text replacements.  
# Could make all changes and then write out new workload
# 	You would have to be carefull about sections with multi-line support.
sub configMailWorkload {
    my $filename = shift;
    my $ans;

    print "\nWhat is the name of the mail host [$defaultSection->{SERVER}]: ";
    $ans = <STDIN>; chomp $ans;
    if ($ans) {
	fileReplaceText ($filename,
			 "(SERVER|SMTPMAILFROM|ADDRESSFORMAT)", 
			 $defaultSection->{SERVER}, $ans);
	$defaultSection->{SERVER} = $ans; # needed for ldif generation
    }
    print "\nWhat is the user name pattern [$defaultSection->{LOGINFORMAT}]: ";
    $ans = <STDIN>; chomp $ans;
    if ($ans) {
	fileReplaceText ($filename,
			 "(LOGINFORMAT|ADDRESSFORMAT)",
			 $defaultSection->{LOGINFORMAT}, $ans);
	$ans =~ s/%ld/0/;	# create smtpMailFrom user
	my $olduser = $defaultSection->{SMTPMAILFROM};
	$olduser =~ s/@.*$//;	# strip off after @
	fileReplaceText ($filename,
			 "SMTPMAILFROM",
			 $olduser, $ans);
    }

    print "\nWhat is the password pattern [$defaultSection->{PASSWDFORMAT}]: ";
    $ans = <STDIN>; chomp $ans;
    fileReplaceText ($filename, "PASSWDFORMAT", 
		     $defaultSection->{PASSWDFORMAT}, $ans);

    $defaultSection->{NUMLOGINS} = 100 unless ($defaultSection->{NUMLOGINS});
    print "\nHow many users [$defaultSection->{NUMLOGINS}]: ";
    $ans = <STDIN>; chomp $ans;
    fileReplaceText ($filename, "(NUMADDRESSES|NUMLOGINS)",
		     $defaultSection->{NUMLOGINS}, $ans);

    $defaultSection->{FIRSTLOGIN} = 0 unless ($defaultSection->{FIRSTLOGIN});
    print "\nWhat is the first user number [$defaultSection->{FIRSTLOGIN}]: ";
    $ans = <STDIN>; chomp $ans;
    fileReplaceText ($filename, "(FIRSTADDRESS|FIRSTLOGIN)",
		     $defaultSection->{FIRSTLOGIN}, $ans);

    print "\nDo you want to view the new $filename [y]? ";
    $ans = <STDIN>;
    unless ($ans =~ /^n/i) {
	print "Here is the new $filename:\n\n";
	fileShow ($filename);
	print "\n";
    }
}

# This uses a match pattern plus text to text replacements.  
# Could make all changes and then write out new workload
# 	You would have to be carefull about sections with multi-line support.
sub configRCSWorkload {
    my $filename = shift;
    my $ans;

    print "\nWhat is the URL of the revision control host [$defaultSection->{REPOURL}]: ";
    $ans = <STDIN>; chomp $ans;
    fileReplaceText ($filename, "(repoUrl)", 
                     $defaultSection->{REPOURL}, $ans) if ($ans);

    print "\nWhat is the repository top directory [$defaultSection->{TOPDIR}]: ";
    $ans = <STDIN>; chomp $ans;
    fileReplaceText ($filename, "(topDir)", 
                     $defaultSection->{TOPDIR}, $ans) if ($ans);

    print "\nWhat is the user name pattern [$defaultSection->{LOGINFORMAT}]: ";
    $ans = <STDIN>; chomp $ans;
    fileReplaceText ($filename, "(LOGINFORMAT)",
                     $defaultSection->{LOGINFORMAT}, $ans) if ($ans);

    print "\nWhat is the password pattern [$defaultSection->{PASSWDFORMAT}]: ";
    $ans = <STDIN>; chomp $ans;
    fileReplaceText ($filename, "PASSWDFORMAT", 
		     $defaultSection->{PASSWDFORMAT}, $ans) if ($ans);

    $defaultSection->{NUMLOGINS} = 100 unless ($defaultSection->{NUMLOGINS});
    print "\nHow many users [$defaultSection->{NUMLOGINS}]: ";
    $ans = <STDIN>; chomp $ans;
    fileReplaceText ($filename, "(NUMADDRESSES|NUMLOGINS)",
		     $defaultSection->{NUMLOGINS}, $ans) if ($ans);

    $defaultSection->{FIRSTLOGIN} = 0 unless ($defaultSection->{FIRSTLOGIN});
    print "\nWhat is the first user number [$defaultSection->{FIRSTLOGIN}]: ";
    $ans = <STDIN>; chomp $ans;
    fileReplaceText ($filename, "(FIRSTADDRESS|FIRSTLOGIN)",
		     $defaultSection->{FIRSTLOGIN}, $ans) if ($ans);

    print "\nDo you want to view the new $filename [y]? ";
    $ans = <STDIN>;
    unless ($ans =~ /^n/i) {
	print "Here is the new $filename:\n\n";
	fileShow ($filename);
	print "\n";
    }
}

sub configWorkload {
    my $ans;

    print "\nConfigure test clients [n]? ";
    $ans = <STDIN>;
    if ($ans =~ /^y/i) {
        configClients($params{WORKLOAD});
    }

    print "\nConfigure SMTP/POP/IMAP protocols [n]? ";
    $ans = <STDIN>;
    if ($ans =~ /^y/i) {
	configMailWorkload($params{WORKLOAD});
    }

    print "\nConfigure SVN support [n]? ";
    $ans = <STDIN>;
    if ($ans =~ /^y/i) {
	configRCSWorkload($params{WORKLOAD});
    }

    print "\nGenerate an account LDIF file for Netscape Directory Server [n]? ";
    $ans = <STDIN>;
    if ($ans =~ /^y/i) {
	configUserLdif($params{WORKLOAD});
    }

}

# See if license file has been displayed
if (($mode ne "cleanup") && (! -f ".license" )) {
    fileShow ("LICENSE");
    print "\nDo you agree to the terms of the license? (yes/no) ";
    my $ans = <STDIN>;
    print "\n";
    unless ($ans =~ /^yes$/i) {
  	print "License not agreed to.\n";
  	exit 0;
    }
    my ($sec, $min, $hour, $mday, $mon, $year) = localtime;
    open (LIC, ">.license");
    printf LIC "%04d$mon$mday$hour$min\n", $year+1900;
    close (LIC);
}

if ($mode eq "config") {	# re-run config
    configWorkload ();

    print "\nMake any additional changes to $params{WORKLOAD} and then re-run 'setup'\n";
    exit 0;
} elsif ($mode ne "cleanup") {	# check if configured
    my $clicnt = 0; # see if clients are configured
    foreach $section (@workload) { # count client sections
        next unless ($section->{sectionTitle} =~ /CLIENT/i);
        $clicnt++;
    }
    if ($clicnt == 0) {
	print "Clients have not been configured.\n";
        print "  Run ./config or edit conf/general.wld and then try again.\n";
	exit 0;
    }
}

if ($mode eq "timesync") {
    my ($sec, $min, $hour, $mday, $mon, $year) = localtime;
    $mon += 1;			# adjust from 0 based to std
    $systime = sprintf ("%02d%02d%02d%02d%04d.%02d",
		     $mon, $mday, $hour, $min, 1900+$year, $sec);
} elsif ($mode eq "checktime") {
    mkdir ("$resultbase", 0775);
    mkdir ("$tmpbase", 0775);
    foreach $section (@workload) {
	next unless ($section->{sectionTitle} =~ /CLIENT/i);
	my $slist = $section->{sectionParams};
	$slist =~ s/HOSTS=\s*//; # strip off initial bit
	foreach $cli (split /[\s,]/, $slist) {
	    open MAKEIT, ">$tmpbase/$cli.tim";
	    close MAKEIT;
	}
    }
} elsif (($mode eq "setup") || ($mode eq "cleanup")) {
    @msgs = <data/*.{msg,dat}>;
    foreach (@files = @msgs) { s/data\/// }
    print "Found these message files:\n@files\n\n";
}

# iterate over every client in the testbed, complete the cmd and rsh
foreach $section (@workload) {
    next unless ($section->{sectionTitle} =~ /CLIENT/i);
    my $slist = $section->{sectionParams};
    $slist =~ s/HOSTS=\s*//; # strip off initial bit
    foreach $cli (split /[\s,]/, $slist) {
	my $rsh = ($section->{RSH}) ? $section->{RSH} : $params{RSH};
	my $rcp = ($section->{RCP}) ? $section->{RCP} : $params{RCP};
	my $tempdir;
	if ($section->{TEMPDIR}) {
	    $tempdir = $section->{TEMPDIR};
	} elsif ($params{TEMPDIR}) {
	    $tempdir = $params{TEMPDIR};
	}

	my $cliarch = $section->{ARCH};

	# presumed architecture for bin/mailclient on localhost:
	my $local_arch = `bin/nsarch`;
	chomp $local_arch;

	# Try to determine arch if it hasn't been explicitly set.
	if (!$cliarch) {
	    if ($cli =~ /localhost/) {
		$cliarch = `bin/nsarch`;
		chomp $cliarch;
	    } else {
		$cliarch = `$rsh $cli sh < bin/nsarch`;
		chomp $cliarch;
	    }
	}

	# most time critical first
	if ($mode eq "timesync") {
	    next if ($cli =~ /^localhost$/i); # dont reset our own time
	    # run all these in parallel to minimize skew
	    forkproc ($rsh, $cli, "date $systime");
	}

	elsif ($mode eq "checktime") {
	    # run all these in parallel to minimize skew
	    forkproc ($rsh, $cli, "date", "/dev/null", "$tmpbase/$cli.tim");
	}

	elsif ($mode eq "setup") {
	    my $clipath = '';	# do nothing by default

	    # Look for architecture-specific binary.
	    if ($cliarch) {
		# fallback to just os-name if we can't find an exact match.
		my $approx = $cliarch;
		$approx =~ s/^(\D+).*/$1/;
		my $approx_bin = <"build/${approx}*/mailclient">;

		if (-x "build/$cliarch/mailclient") {
		    # exact match.
		    $clipath = "build/$cliarch/mailclient";
		} elsif (-x $approx_bin) {
		    # approximate match
		    $clipath = $approx_bin;
		} elsif ($local_arch =~ /^$approx/) {
		    # same arch as localhost
		    $clipath = "bin/mailclient";
		} else {
		    print STDERR
			"Requested OS $cliarch for $cli not found.  ",
			"Not copying binary.\n";
		}
	    } else {
		# arch not found
		print STDERR "Cannot determine architecture for $cli.  ",
		"Not copying binary.\n";
	    }
	    # See if we have anything to copy:
	    if ("$clipath @files" !~ /\S/) {
		print STDERR "Nothing to copy to $cli.  Skipping.\n";
		next;
	    }
	    my $rdir = ($tempdir) ?  "$tempdir/" : ".";
	    # chmod so that the remote files can be easily cleaned up
	    my $rcmd = "chmod g+w @files mailclient; uname -a";
	    $rcmd = "cd $tempdir; $rcmd" if ($tempdir);
	    if ($cli =~ /^localhost$/i) {
		die "TEMPDIR must be set for 'localhost'\n"
		    unless ($tempdir);
		print "Copying $clipath and message files to $rdir\n";
		die_system ("$cpcmd @msgs $clipath $rdir");
		die_system ($rcmd);
	    } else {
		print "$rcp $clipath @msgs $cli:$rdir\n" if ($params{DEBUG});
		print "Copying $clipath and message files to $cli:$rdir\n";
		warn_system (split (/\s+/, $rcp), $clipath,
                             @msgs, "$cli:$rdir");
		print "rcmd='$rcmd'\n" if ($params{DEBUG});
		die_system (split (/\s+/, $rsh), $cli, $rcmd);
	    }
	    print "\n";
	}

	elsif ($mode eq "cleanup") {
	    if ($params{DEBUG}) {	# get debug files
		print "Cleaning up debug files on $cli\n";
		my $rcmd = $rmcmd;
		$rmcmd .= " mstone-debug.[0-9]*";
		$rcmd = "cd $tempdir; $rcmd" if ($tempdir);
		if ($cli =~ /^localhost$/i) {
		    die "TEMPDIR must be set for 'localhost'\n"
			unless ($tempdir);
		    warn_system ($rcmd);
		} else {
		    warn_system (split (/\s+/, $rsh), $cli, $rcmd);
		}
	    } else {
		print "Cleaning $cli\n";
		my $rcmd = $rmcmd;
		$rcmd .= " mailclient @files";
		$rcmd = "cd $tempdir; $rcmd" if ($tempdir);
		if ($cli =~ /^localhost$/i) {
		    die "TEMPDIR must be set for 'localhost'\n"
			unless ($tempdir);
		    warn_system ($rcmd);
		} else {
		    warn_system (split (/\s+/, $rsh), $cli, $rcmd);
		}
	    }
	}

	else {
	    die "Couldn't recognize mode $mode!\n";
	}
    }
}

# wait for children to finish
if (($mode eq "timesync") || ($mode eq "checktime")) {
    $pid = wait();
    while ($pid != -1) {
	$pid = wait();
    }
}

# Print the results of the time checks
if ($mode eq "checktime") {
    print "Time from each client:\n";
    foreach $section (@workload) {
	next unless ($section->{sectionTitle} =~ /CLIENT/i);
	my $slist = $section->{sectionParams};
	$slist =~ s/HOSTS=\s*//; # strip off initial bit
	foreach $cli (split /[\s,]/, $slist) {
	    open TIMEFILE, "$tmpbase/$cli.tim"
		|| warn "Counldn't open $tmpbase/$cli.tim\n";
	    printf "%32s: ", $cli;
	    while (<TIMEFILE>) { print; last;} # single line (2 on NT)
	    close(TIMEFILE);
	    unlink "$tmpbase/$cli.tim";
	}
    }
}
