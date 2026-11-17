#!/usr/bin/env bash
set -euo pipefail
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)/test_common.sh"

test_case()
{
    start_server fifo.txt
    start_observer
    start_paused_model
    wait_for_line 'день начат' "$case_dir/observer.log"
    kill -TERM "$observer_pid"
    wait_process "$observer_pid" failure
    wait_for_line 'Observer отключён' server.log
    kill -0 "$server_pid"
    kill -CONT "$librarian_pid"
    finish_workers
    grep -q 'День 9: серверная обработка завершена' server.log
}
run_case observer_disconnect
