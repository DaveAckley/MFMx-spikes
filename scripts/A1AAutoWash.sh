#!/usr/bin/env bash

cleanup() {
    # restore terminal
    stty sane 2>/dev/null || true
    exit 0
}

SCRIPT=${BASH_SOURCE[0]}
SCRIPTDIR=$(dirname $SCRIPT)

# The file path to watch for
WATCH_FILE="/tmp/MFMx-spikes-BUILT.new"
BASEDIR=$(realpath $SCRIPTDIR/..)
PROG="nu1011.py"
echo BASEDIR $BASEDIR
PROGRAM="$BASEDIR/srcs/host/python/EWD/$PROG"
echo looking for $PROGRAM
if ! [ -f $PROGRAM ] ; then
   echo "cannot find $PROGRAM"
   exit 1  
fi
echo "FOUND $PROGRAM"

# The program to run (can be a curses/TUI program)
PROGRAMARGS=($PROGRAM "5" "2")

get_disk_usage_pct() {
    local mount="${1:-/}"   # default to root
    df "$mount" | awk 'NR==2 {gsub(/%/,""); print $5}'
}

while true; do
    stty sane 2>/dev/null || true
    clear
    echo "WATCHING FOR $WATCH_FILE !!"
    while [[ ! -e "$WATCH_FILE" ]]; do
        trap cleanup INT TERM EXIT

        # Brief sleep to avoid a tight CPU-spinning loop
        usg=$(df -h / | tail -1)
        echo -e -n "\r                                                                                    \r$(date) $usg"
        USAGE=$(get_disk_usage_pct)
        if [ "$USAGE" -ge "85" ]; then
            echo -n "⚠️ "
        fi
        if [ "$USAGE" -ge "90" ]; then
            echo -n "⚠️ "
        fi
        if [ "$USAGE" -ge "93" ]; then
            echo -n "⚠️ "
        fi
        if [ "$USAGE" -ge "95" ]; then
            echo -n "⚠️ "
        fi
        if [ "$USAGE" -ge "96" ]; then
            echo "🛑 DISK TOO FULL"
            exit 1
        fi
        sleep 2
    done

    echo " -- FOUND FILE $WATCH_FILE!! -- "
    # Remove the trigger file before launching the program
    rm -f -- "$WATCH_FILE"

    # Run the program in the foreground.
    # We use exec with a subshell so that any exit (including signals)
    # brings control back to the loop.
    echo "RUNNING ${PROGRAM[@]}"
    ("${PROGRAM[@]}" 1>>/tmp/autowash.out)
done
