#!/usr/bin/perl --
# Show pre-post deltas in ifconfig data
# Usage:  perl clean_ifconfig.pl infile outfile duration frequency width height
# duration and frequency are in seconds
# width and height are in pixels.

use strict;
use warnings;

my $usage = "Usage: $0 infile outfile [duration frequency width height]";
die "$usage\n" unless ($#ARGV >= 1);
my $infile = shift;
my $outfile = shift;
# We ignore duration, frequency, width, and height

# Usage: parseBlock FILE
# Returns: Hash with fields => values
# Each field is prefixed with either RX_ or TX_
sub parseBlock {
  my $file = shift;

  my %all_fields = ();               # hash name => hash ref
  while (<$file>) {
    chomp;
    unless ($_) {
      return \%all_fields;
    }
    # RX packets:397701 errors:0 dropped:0 overruns:0 frame:30
    if ($_ =~ m/^\s+(RX|TX) (packets:\d+.*)/o) {
      my $dir = "$1_";
      my @fields = split /\s+/, $2;
      #print "dir: '$dir', fields: @fields\n"; # DEBUG
      foreach my $f (@fields) {
        my @nv = split /:/, $f;
        $all_fields{"$dir$nv[0]"} = 0.0 + $nv[1];
      }
      next;
    }
    #          RX bytes:286608204 (273.3 MiB)  TX bytes:310780054 (296.3 MiB)
    if ($_ =~ m/^\s+RX bytes:(\d+).*\s+TX bytes:(\d+)/o) {
      $all_fields{"RX_bytes"} = $1;
      $all_fields{"TX_bytes"} = $2;
      next;
    }
    #print "Skip: $_\n";         # DEBUG
  }
}

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

# Usage: printDelta FILE \%old_data \%new_data prefix
sub printDelta {
  my $file = shift;
  my $old = shift;
  my $new = shift;
  my $prefix = shift || '  ';

  for my $key (sort keys %$new) {
    my $delta = $new->{$key};
    if (defined($old->{$key})) {
      $delta -= $old->{$key};
      # ifconfig counters are only 32bit.  Correct for counter wrap.
      # We can't tell if the counters wrapped more than once! + N * 2^32
      if ($delta < 0) {
        $delta += 4*1024*1024*1024;
      }
      printf $file "%s$key: \t%s \t(%.0f - %.0f)\n",
      $prefix, kformat($delta), $new->{$key}, $old->{$key};
    } else {
      printf $file "%s$key: \t%s\n", $prefix, kformat($new->{$key});
    }
  }
}
    
open OUTFILE, ">$outfile" || die "Error opening $outfile for output\n";
open INFILE, "<$infile" || die "Error opening $infile for reading\n";

# HACK: we have to find the matching pretest file, but only have one name.
# First look for -preNAME.txt, then -pre*.txt
my $preglob = $infile;
$preglob =~ s/-log-(\w+).txt/*-pre$1.txt/;
my $name = $1;
my @prefiles = glob $preglob;
unless (@prefiles) {
  $preglob =~ s/-pre$name.txt/-pre*.txt/;
  @prefiles = glob $preglob;
}
#print STDERR "in: $infile pre: @prefiles post: $outfile\n"; # DEBUG

# hash of interfaces => %fields
my %prefields = ();
my %postfields = ();

if (open PREFILE, "<$prefiles[0]") {
  while (<PREFILE>) {
    chomp;
    if ($_ =~ /^(eth\d+)\s+Link/o) {
      #print "Saw pre $1\n";      # DEBUG
      $prefields{$1} = parseBlock(\*PREFILE);
      #print "Got pre $1 %$prefields\n"; # DEBUG
      next;
    }
  }
} else {
  print STDERR "Could not find pretest file using pattern $preglob\n";
}

while (<INFILE>) {
  chomp;

  # pass through the header written by mstone
  if ($_ =~ m/^=.*=$/o) {
    print OUTFILE "$_\n";
    next;
  }
  if ($_ =~ /^lo\s+Link/o) {    # ignore lo
    #print "Ignoring lo\n";      # DEBUG
    next;
  }
  if ($_ =~ /^(eth\d+)\s+Link/o) {
    #print "Saw post $1\n";      # DEBUG
    $postfields{$1} = parseBlock(\*INFILE);
    #print "Got post $1 %$postfields\n"; # DEBUG
    next;
  }
}

# ASSUMES: that there is a matching pretest and posttest for all interfaces.
for my $key (sort keys %postfields) {
  print OUTFILE "Counter deltas for $key:\n";
  printDelta(\*OUTFILE, $prefields{$key}, $postfields{$key});
  print OUTFILE "\n";
}
