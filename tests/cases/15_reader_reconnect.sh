#!/usr/bin/env bash
set -euo pipefail
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)/test_common.sh"

check_completed_phase()
{
    local first_fd second_fd
    capture_reconnect_baseline
    start_server fifo.txt
    start_observer
    raw_worker 0
    first_fd=$raw_fd
    raw_worker 1
    second_fd=$raw_fd
    start_client librarian
    start_client 2
    raw_fd=$first_fd
    raw_expect 8
    raw_send 9 0 1 0 0 0
    raw_expect 11
    raw_send 12 0 1 0 0 0
    wait_for_line "Reader завершил фазу; Reader 0" observer.log
    raw_close
    wait_for_line "Отключился рабочий клиент Reader 0" server.log
    start_client 0
    wait_for_line "Синхронизация: день 1; состояние 6" reader_0.log
    raw_fd=$second_fd
    raw_expect 8
    raw_close
    wait_for_line "Отключился рабочий клиент Reader 1" server.log
    start_client 1
    finish_reconnect_run
}

test_case()
{
    run_subcase before_operation resume_reader_test 0
    run_subcase after_operation resume_reader_test 1
    run_subcase completed_phase check_completed_phase
}
run_case reader_reconnect
