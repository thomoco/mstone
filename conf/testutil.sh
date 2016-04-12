# source this into a bash (or ksh) script
# Utilities to run tests under mstone
# Dan Christian.  June 2007

# localdir must match localDir in the workload for core collection to work.
[[ -z "$localdir" ]] && localdir="/tmp/mstonercs" # default localdir

# Print (optional) error message and exit
# Usage: die [[msg] exit_status]
die() {
    [[ -n "$1" ]] && echo "$1"
    [[ -n "$2" ]] && exit $2 || exit 1
}

# show usage and exit
usage() {
    echo -e "Usage: $0 [-l load] [-r tries] [-t time] [options...]$args_extra"
    echo -e "    -s           Run setup before starting test";
    echo -e "    -q           Request quick version of test (for debugging)";
    echo -e "    -l load      How many clients to use";
    echo -e "    -t time      How long to run (minutes)";
    echo -e "    -r tries     How many time to try a test (due to failures)";
    echo -e "    -b name      What to name the report NAME-date.html";
    echo -e "    -c dir       Where to copy final test results";
    echo -e "    -m 'm1 m2'   List of machines to check for core files";
    echo -e "    -e prog      Program to use to generate report$usage_extra";
    exit 1
}

# Parse arguments (and remove from command line)
arg=''
for a ; do
  if [[ -n "$arg" ]] ; then
      case "$arg" in
          -b) testname="$a"; arg=''; shift;;
          -c) copydir="$a"; arg=''; shift;;
          -e) report_prog="$a"; arg=''; shift;;
          -l) load="$a"; arg=''; shift;;
          -m) machines="$a"; arg=''; shift;;
          -r) tries="$a"; arg=''; shift;;
          -t) time="$a"; arg=''; shift;;
      esac
  else
      case $a in
          -b|-c|-e|-l|-m|-r|-t) arg="$a"; shift;;
          -h) usage;;
          -s) ./setup; shift;;
          -q) quick="yes"; shift;;
          *) break;;            # unknown arguments are left in $@
      esac
  fi
done
[[ -z "$arg" ]] || die "Missing argument to $arg"

if [[ -n "$quick" ]] ; then     # for quick, use low defaults
  [[ -n "$tries" ]] || tries=1
  [[ -n "$time" ]] || time=3
  [[ "$time" -gt 10 ]] && time=10 # the test sets a default time, reduce it
fi

# Number of times to try a test run
[[ -n "$tries" ]] || tries=2
[[ "$tries" -ge 1 ]] || die "Tries must be at least 1"
  

# Test time in minutes
[[ -n "$time" ]] || time=5

# Test load
[[ -n "$load" ]] || load=1

# default test name for report file name: $testname-$starttime.txt
[[ -n "$testname" ]] || testname="$0"
# Whitespace causes problems later.  Convert to underscore.
testname= `echo "$testname" | sed -e 's/[ 	]/_/g'`
  
# command to generate report
# Called: $report_prog DIR...
# stdout is redirected to the final report
# stderr is displayed on console
# return status indicates if the runs were OK
[[ -n "$report_prog" ]] || report_prog="check_stderr"

# list of all runs (space separated)
ALL_RUNS=""

# timestamp for the labeling the report
starttime=`date '+%Y%m%d.%H%M'`

#egrep pattern of lines to ignore in stderr
stderr_ignore='/usr/bin.*/xauth|Handling commit error:|cd /var/tmp;'

# We need patterns that don't match files to expand to nothing
shopt -s nullglob               # this sets it for bash
#setopt NULL_GLOB                # this sets it for zsh

# If generating a report, make it world readable
[[ -n "$copydir" ]] && umask 022

# Create a report dir to write reports and graphs into
setup_report_dir() {
    export MSTONE_REPORT_DIR="results/$starttime-$testname"
    mkdir -p "$MSTONE_REPORT_DIR"
}

# sleep to let server load drop.
server_sleep() {
    echo -n "Waiting for server load to drop (Ctrl-C now to abort run)... "
    [[ "$time" -gt 5 ]] && sleep 60 || sleep 15
    echo " Continuing"
}

# check stderr for problems
# This is the default if no report_prog was specified
# Usage: check_stderr out_dir
# Returns: 0 if everything OK.
check_stderr() {
    out_dir="$1"
    status=0
    stderr="$out_dir/stderr"
    if egrep -v "$stderr_ignore" "$stderr" | egrep -qi "error" ; then
        echo "==== Errors found:" 1>&2
        egrep -v "$stderr_ignore" "$stderr" | egrep -i "error" | tail
        status=1
    fi
    if egrep -v "$stderr_ignore" "$stderr" | egrep -qi "failed" ; then
        echo "==== Failures detected:" 1>&2
        egrep -v "$stderr_ignore" "$stderr" | egrep -i "failed" | tail
        status=1
    fi
    if egrep -v "$stderr_ignore" "$stderr" | egrep -qi " fault|Aborted" ; then
        echo "==== Faults found:" 1>&2
        egrep -v "$stderr_ignore" "$stderr" | egrep -i " fault|Aborted" | tail
        status=1
    fi
    if ! tail "$stderr" | grep -q "Done." ; then
        echo "Run did non terminate properly ('Done.' not found)." 1>&2
        status=2
    fi
    return $status
}

# Remove any sensitive info from the stored results
scrub_results() {
    local scrub_dir="$1"; shift
    for f in all.wld work.wld ; do      # strip account and password entries
      [[ -f "$scrub_dir/$f" ]] || continue
      egrep -iv '^[ 	]*(passwdFormat|loginFormat)' "$scrub_dir/$f" \
        > "$scrub_dir/$f"~
      cmp -s "$scrub_dir/$f"~ "$scrub_dir/$f" && rm "$scrub_dir/$f"~\
        || mv "$scrub_dir/$f"~ "$scrub_dir/$f" 
    done
    for f in stderr ; do      # strip account and password entries
      [[ -f "$scrub_dir/$f" ]] || continue
      egrep -iv 'clean line.*(passwdFormat|loginFormat)' "$scrub_dir/$f" \
        > "$scrub_dir/$f"~
      cmp -s "$scrub_dir/$f"~ "$scrub_dir/$f" && rm "$scrub_dir/$f"~\
        || mv "$scrub_dir/$f"~ "$scrub_dir/$f" 
    done
}
    
# Run one mstone test, monitor it, and check results for sanity
# Usage: run_mstone mstone_args...
# If check_program is empty, use out built in check
# Returns 0 for a successful run
# Exports ALL_RUNS with the directories created
run_mstone() {
    NEW_RUNS=""                         # Runs created by this invocation
    if [[ -n "$report_prog" &&  "$report_prog" != "check_stderr" ]] ; then
        [[ -n "$MSTONE_REPORT_DIR" ]] || setup_report_dir
    fi
    trycnt=$tries
    while ((trycnt-- > 0))         # re-try on error
    do
      trap '' SIGALRM           # ignore spurious alarm signals from mstone
      echo -e "\nRunning: ./mstone $@"
      purge_cores               # also waits for any background gzip
      startpat=`date '+%Y%m%d'` # slight chance of failure around midnight
      ./mstone "$@" &
      mpid=$!
      trap "kill -INT $mpid && echo Signalled $mpid && wait $mpid && quick=shutdown" INT TERM
      sleep 45                  # long enough for pretest to finish
      out_dir=`ls -d tmp/$startpat.* | tail -1 | sed -e 's!tmp/!!'`
      export NEW_RUNS="$NEW_RUNS $out_dir"
      export ALL_RUNS="$ALL_RUNS $out_dir"

      wait $mpid
      mstatus=$?
      trap - INT TERM
      # save cores with run results (and update test report)
      (cd results/$out_dir && collect_cores \
          && get_stacks && make_all_stacks "$out_dir: ./mstone $*") \
        && ./process results/$out_dir

      echo "================"
      [[ $mstatus -ne 0 ]] && echo "mstone status indicates problems"
      
      scrub_results "results/$out_dir"
      $report_prog "results/$out_dir" > /dev/null
      rstatus=$?
      if [[ $rstatus -eq 0 && $mstatus -eq 0 ]] ; then
        echo -e "Good run: $out_dir\n"
        return 0
      fi
      echo "Problems detected in $out_dir"
      [[ "$quick" = "shutdown" ]] && return 1  # Ctrl-C received
      if ((trycnt > 0)) ; then
          echo -e "---- Repeating test ----"
          server_sleep
      else
          echo -e "No more retries.  Continuing to next test case."
      fi
    done
    return 1
}

# nuke any old cores on local or remote machines
# We also invoke mailclient to pull it+libraries into cache
purge_cores() {
    for h in $machines ; do
        if [[ "$h" = localhost ]] ; then
            [[ -n "`echo -n {/var/tmp,$localdir}/core*`" ]] \
                && rm -f /{var/tmp,tmp/mstonercs}/core* &
            /var/tmp/mailclient -h 2> /dev/null
        else
            ssh -x $h "/var/tmp/mailclient -h; rm -f {/var/tmp,$localdir}/core*" 2> /dev/null &
        fi
    done
    echo -n "Waiting for clean up workers... "
    wait
    echo " Done"
}

# Collect any cores on local or remote machines
# Makes a subdirectorie in current directory for each machine.
collect_cores() {
    [[ -n "$machines" ]] || return 1

    for h in $machines ; do
        destdir="core-$h"
        mkdir $destdir 2> /dev/null
        if [[ "$h" = localhost ]] ; then
            [[ -n "`echo -n {/var/tmp,$localdir}/core*`" ]] \
                && cp {/var/tmp,$localdir}/core* $destdir/ &
        else
            scp -qp $h:{/var/tmp,$localdir}/core\* $destdir/ 2> /dev/null &
        fi
    done
    echo -n "Checking for cores... "
    wait
    echo " Done"

    for h in $machines ; do     # purge empty directories
        destdir="core-$h"
        rmdir $destdir 2> /dev/null   # which fails if directory isn't empty
    done
    
    corelist=`echo -n core-*/core* | sed -e s/core-//g`
    [[ -n "$corelist" ]] && echo "Retrieved cores: $corelist"
    # returns true if there are core files
}

# Generate stack traces from core files
# Must be run from directory above core files
# BUG: cores on linux-2.6.18 are different than 2.6.22
get_stacks() {
    [[ -n "core-*/core*" ]] || return 1
    echo -e "bt\nquit\n" > backtrace.gdb
    for f in core-*/core* ; do
      # Strip out addresses to make file compares easier
      stackfile=`echo $f | sed -e 's!/core!/stack!'`      
      stackfile="$stackfile.txt"
      # BUG: user's .gdbrc might affect results
      gdb /var/tmp/mailclient $f < backtrace.gdb 2>> gdb-error.log \
          | sed -ne '/^Core was generated/p
/^Program terminated/p
/^.gdb/,/^.gdb/s/0x[0-9a-f]\{3,16\}/0x???/gp' > $stackfile
    done
    gzip core-*/core* &         # compress core files
    rm -f backtrace.gdb
    [[ -n "`echo -n core-*/stack*`" ]] # return true if there are stacks
}

# Make a combined listing of all stack traces
# Must be run from directory above core files
make_all_stacks() {
    [[ -n "`echo -n core-*/stack*`" ]] || return 1
    # Filters out identical stack traces
    [[ -f all-core.txt ]] && mv -f all-core.txt all-core.txt~
    [[ -n "$*" ]] && echo -e "$*\n====" > all-core.txt
    last=""                     # last stack printed
    for f in `ls -S core-*/stack*` ; do # sort by stack size
        corefile=`echo $f | sed -e 's!/stack!/core!'`
        corefile="${corefile%.txt}"
        ls -l $corefile >> all-core.txt

        tail +2 "$f" > "$f"~    # skip first line (args) for the comparison
        if [[ -n "$last" ]] ; then
            if cmp "$last"~ "$f"~ > /dev/null; then
                echo -e "Stack of $f == $last\n" >> all-core.txt
                rm -f "$f"~
                continue            # skip identical stack
            else
                rm -f "$last"~
            fi
        fi
        last="$f"

        echo -e "========" >> all-core.txt
        cat $f >> all-core.txt
        echo -e "==================\n\n" >> all-core.txt
    done
    [[ -n "$last" ]] && rm -f "$last"~ # also sets the proper status to return
}

# Generate the final report and status feedback.
# This can be called from a trap.
# Usage: generate_report [report_name [runs...]]
generate_report() {
    trap - EXIT                 # don't run more than once

    echo -e "\n\n================================"
    echo "Generating report of all runs"

    rep_name="report"
    runs="$ALL_RUNS"
    if [[ -n "$1" ]] ; then
        rep_name="$1"
        shift
        if [[ -n "$1" ]] ; then
            runs="$@"
        fi
    fi
    if [[ -z "$runs" ]] ; then
        echo "No runs to generate report from!"
        return 99
    fi

    # get list of all runs (as a shell expansion)
    runlist="`echo $runs | sed -e 's/ /,/g'`"
    if [[ $runlist == *,* ]] ; then
        file_list="`eval echo results/{$runlist}`" # expanded list
        shell_files="results/{$runlist}"           # human/pastable version
    else         # shell doesnt like curly braces around just one item
        file_list="results/$runlist"
        shell_files="$file_list"
    fi

    echo -n "Waiting for background tasks... "
    wait
    echo " Done"
    
    if [[ -n "$report_prog" &&  "$report_prog" != "check_stderr" ]] ; then
        # Generate report from all runs
        [[ -n "$MSTONE_REPORT_DIR" ]] || setup_report_dir
        copyargs="-d $MSTONE_REPORT_DIR -w $rep_name.wiki -t $rep_name.html"
        [[ -n "$copydir" ]] && copyargs="$copyargs -c $copydir"
        $report_prog $copyargs $file_list
        status=$?
        echo -e "Generated $MSTONE_REPORT_DIR/$rep_name.* from:\n  $report_prog $copyargs $shell_files"
    else
        status=0
        for r in $runs ; do
            check_stderr results/$r || ((status++))
        done
        echo "Created these runs: $shell_files"
    fi
    [[ $status -eq 0 ]] && echo "All runs OK" || echo "Failures detected"
    return $status
}
# on fault, display results to far
trap "generate_report" EXIT
