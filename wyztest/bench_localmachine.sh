#!/bin/bash

source shared.sh

USAGE="Usage: $0 [tests_prefix]"
SERVER_IP="18.18.1.5"
MAIN_SERVER_IP="18.18.1.2"
LPID=1
TEST_NAME="bench_hashtable_timeseries"

while (( "$#" )); do
	case "$1" in
		-h|--help) echo "$USAGE" >&2 ; exit 0 ;;
		*) tests_prefix=$1 ; shift ;;
	esac
done

function prepare {
    kill_iokerneld
    kill_controller
    sleep 5
    source setup.sh >/dev/null 2>&1
    sudo sync; sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"
}

function run_main_server {
    sudo stdbuf -o0 sh -c "ulimit -c unlimited; $1 -m -l $LPID -i $MAIN_SERVER_IP"
}

function run_server {
    sudo stdbuf -o0 sh -c "ulimit -c unlimited; $1 -l $LPID -i $SERVER_IP"
}

function run_test {
    BIN="$SHARED_SCRIPT_DIR/bin/$1"

    # run_controller 1>/dev/null 2>&1 &
    run_controller 1>$SHARED_SCRIPT_DIR/wyztest/logs/controller.log 2>&1 &
    disown -r
    sleep 3

    # run_server $BIN 1>/dev/null 2>&1 &
    run_server $BIN 1>$SHARED_SCRIPT_DIR/wyztest/logs/server.log 2>&1 &
    disown -r
    sleep 3

    # run_main_server $BIN 2>/dev/null | grep -q "Passed"
    run_main_server $BIN 1>$SHARED_SCRIPT_DIR/wyztest/logs/mainserver.log 2>&1
    ret=$?

    kill_process bench_
    kill_controller
    sleep 5

    sudo mv core core.$1 1>/dev/null 2>&1

    return $ret
}

function cleanup {
    kill_iokerneld
}

function force_cleanup {
    echo -e "\nPlease wait for proper cleanups..."
    kill_process test_
    kill_controller
    cleanup
    exit 1
}

function main_func {
    prepare
    trap force_cleanup INT

    echo "Running test $TEST_NAME..."
    rerun_iokerneld
    run_test $TEST_NAME

    cleanup

    exit 0
}

main_func
