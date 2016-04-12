#!/bin/bash
# run a series of mstone load tests
# Dan Christian.  June 2007

time=20				# default time in minutes
report_prog="conf/report-svn-all.pl" # The appropriate report/status program

testname="test"                 # Default name base
source  conf/testutil.sh	# Get variables/functions to run/report tests
testname="$testname-svn_load" 	# Name to use for generated report

for load in 5 10 20 50 #100
do
    [[ -n "$ALL_RUNS" ]] && server_sleep
    run_mstone \
        conf/svn-ss.wld \
        -b "Load $load" -t "$time"m -r $(($load*5)) -l $load "$@"
    [[ -n "$quick" && "$load" -ge 10 ]] && break
done

generate_report     # return status from generate_report becomes our status
