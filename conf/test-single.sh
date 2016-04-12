#!/bin/bash
# run a single mstone test with monitoring and error checking
# This is the very simplest example of the test-*.sh scripts
# Dan Christian.  June 2007

testname="test-single"
usage_extra="
All remaining arguments are passed directly to mstone.  The test
control arguments (show above) must all come before the mstone
arguments.

Example:
  conf/test-single.sh -q -r 1 -t 5 -b 'example' svn-ss.wld
"
source  conf/testutil.sh

workload="$1"
shift
run_mstone \
    "$workload" -b "$testname" -t "$time"m  -r 60 -l $load "$@"

generate_report     # return status from generate_report becomes our status
