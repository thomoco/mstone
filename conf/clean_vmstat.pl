#!/usr/bin/perl --
# Get the interesting parts of vmstat data
# Usage:  perl clean_vmstat.pl infile outfile duration frequency width height
# duration and frequency are in seconds
# width and height are in pixels.

# Print the time (from test start), runnable processes (load), idle%,
#   and block IO.
# The goal is to catch conditions where the client saturates.
# A marker line is inserted every minute to help with readibility.

# This is what you get for vmstat(procps-3.2.6) on Linux:
# r  b   swpd   free   buff  cache   si   so    bi    bo   in    cs us sy id wa
# 3  0 138520 721080 617192 369260    0    0     9    16   15     9  2  0 97  1

use strict;
use warnings;

my $usage = "Usage: $0 infile outfile duration frequency [width height]";
die "$usage\n" unless ($#ARGV >= 3);
my $infile = shift;
my $outfile = shift;
my $duration = shift;
my $frequency = shift;
# We ignore width and height for now

# Just get running processes and idle percentage
open OUTFILE, ">$outfile" || die "Error opening $outfile for output\n";
open INFILE, "<$infile" || die "Error opening $infile for reading\n";

my $time = 0;
my $marker_period = 60;         # insert a marker this often (seconds).
my $marker = $marker_period;

# pass through the header written by mstone
while (<INFILE>) {
  if  ($_ =~ m/^=.*=\s+$/o) {
    print OUTFILE "$_";
  } else {
    last;
  }
}

print OUTFILE "Time  runable  idle% blocks_in blocks_out\n";   # label results

while (<INFILE>) {
  chomp;
  $_ =~ s/^\s*//o;              # strip white space (if any)
  next unless ($_ =~ /^\d/o);   # ignore non-data lines

  my @fields = split /\s+/;
  print OUTFILE "$time \t$fields[0] \t$fields[14] \t$fields[8] \t$fields[9]\n";

  $time += $frequency;          # guestimate of time
  if ($time >= $marker) {
    print OUTFILE "\n";
    $marker += $marker_period;
  }
}
