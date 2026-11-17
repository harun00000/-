#!/usr/bin/env bash
set -euo pipefail
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)/test_common.sh"

test_case()
{
    start_server fifo.txt
    start_three_observers
    start_paused_model
    wait_for_line 'день начат' "$case_dir/observer_b.log"
    kill -TERM "${observer_pids[1]}"
    wait_process "${observer_pids[1]}" failure
    wait_for_line 'Observer отключён' server.log
    kill -0 "$server_pid"
    kill -CONT "$librarian_pid"
    finish_workers
    check_complete_observer observer_a.log 9
    check_complete_observer observer_c.log 9
    diff observer_a.log observer_c.log
}
run_case observer_disconnect_others_continue
