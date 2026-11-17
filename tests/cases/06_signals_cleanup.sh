#!/usr/bin/env bash
set -euo pipefail
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)/test_common.sh"

check_signals()
{
    for number in INT TERM; do
        mkdir "$case_dir/$number"
        local outer_dir=$case_dir
        case_dir="$outer_dir/$number"
        start_long_run
        kill -"$number" "$server_pid"
        for pid in "${pids[@]}"; do wait_process "$pid" success; done
        grep -q 'Получен сигнал' "$case_dir/server.log"
        case_dir=$outer_dir
        pids=()
        statuses=()
    done
}

check_cleanup()
{
    full_run one_reader_one_book.txt
    for pid in "${pids[@]}"; do ! kill -0 "$pid" 2>/dev/null || fail "остался PID $pid"; done
    pids=()
    statuses=()
    mkdir interrupted
    case_dir="$case_dir/interrupted"
    start_long_run
    kill -TERM "$server_pid"
    for pid in "${pids[@]}"; do wait_process "$pid" success; done
    cleanup_case
    for pid in "${pids[@]}"; do ! kill -0 "$pid" 2>/dev/null || fail "остался PID $pid"; done
}

test_case()
{
    run_subcase signals check_signals
    run_subcase cleanup check_cleanup
}

run_case signals_cleanup
