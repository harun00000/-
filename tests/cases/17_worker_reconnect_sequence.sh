#!/usr/bin/env bash
set -euo pipefail
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)/test_common.sh"

check_sequence()
{
    local simultaneous=$1 lib_fd first_fd second_fd
    capture_reconnect_baseline
    start_server fifo.txt
    start_observer
    raw_worker -1
    lib_fd=$raw_fd
    raw_worker 1
    first_fd=$raw_fd
    raw_worker 2
    second_fd=$raw_fd
    start_client 0
    raw_fd=$lib_fd
    raw_expect 6
    raw_send 7 -1 1 0 0 0
    raw_expect 11
    raw_send 12 -1 1 0 0 0
    raw_fd=$first_fd
    raw_expect 8
    raw_close
    wait_for_line 'Отключился рабочий клиент Reader 1' server.log
    if ((simultaneous)); then
        raw_fd=$second_fd
        raw_expect 8
        raw_close
        wait_for_line 'Отключился рабочий клиент Reader 2' server.log
    fi
    start_client 1
    wait_for_line 'Reader завершил фазу; Reader 1' observer.log
    ! grep -q '^День 2: день начат$' observer.log
    if ((!simultaneous)); then
        raw_fd=$second_fd
        raw_expect 8
        raw_close
        wait_for_line 'Отключился рабочий клиент Reader 2' server.log
    fi
    start_client 2
    raw_fd=$lib_fd
    raw_expect 6
    [[ ${raw_frame[2]} == 2 ]]
    raw_close
    wait_for_line 'Отключился рабочий клиент Librarian' server.log
    kill -0 "$observer_pid"
    start_client librarian
    finish_reconnect_run
    grep -q 'Синхронизация: день 2;' librarian.log
}

test_case()
{
    run_subcase sequence check_sequence 0
    run_subcase multiple_missing check_sequence 1
}
run_case worker_reconnect_sequence
