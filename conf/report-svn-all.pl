#!/usr/bin/perl --
# Generate a wiki report/status from mstone runs
# This report lists all SVN operations (makes for a big table!)
# Dan Christian.  June 2007

# List of timers to report (see results/SOME.RUN/results.txt for values)
# Each timer can specify the fields[J,K,L] or default to timeFields
@main::timers = ('get_head', 'checkout[4,7]', 'add', 'del', 'mkdir', 'modify',
                 'list', 'diff', 'log', 'blame', 'stat',
                 'update', 'commit[4,7]');
# Which fields to select (see reportutil.pl:@colNames for order)
@main::timeFields = (4);

# # List of timer rates to report (see results/SOME.RUN/results.txt for values)
# @main::rates = ('commit/m', 'total/m');
# # Which fields to select (see reportutil.pl:@colNames for order)
# @main::rateFields = (0);

# # Add network bandwidth fields
# @main::netTimers = ('RX_bytes:\s+\S+\s+\((\d+) - (\d+)\)',
#                     'TX_bytes:\s+\S+\s+\((\d+) - (\d+)\)');
# @main::netFields = (0);

# What web address to use
$main::www = "http://www/~MyDir/mstone_svn";

# Customs processing callback
# Called once with the just the header list
# Called for each test with data
# Called like this: custom_procesing(\@results, \%timerFields, \%rateFields)
# Return results list reference
sub custom_results {
  my $results = shift;
  my $timeFields = shift;
  my $checkoutField = 2;
  my $commitField = 14;

  # Convert StdDev to 95% percentile time for checkout and commit
  # mean+1*StdDev is 68percentile; mean+2*StdDev is 95percentile;
  if ($timeFields) {            # results mode
    #my $rateFields = shift;
    @$results[$checkoutField] = @$results[($checkoutField-1)]
      + 2 * @$results[$checkoutField]; # checkout
    @$results[$commitField] = @$results[($commitField-1)]
      + 2 * @$results[$commitField]; # commit
  } else {                      # header mode
    @$results[$checkoutField] =~ s/:TStd/:Time95%/;
    @$results[$commitField] =~ s/:TStd/:Time95%/;
  }

  return $results;
}
$main::customResultsCallback = \&custom_results;

# Our report template handles everything else
do 'conf/reportwiki.pl' || die "$@\n";
