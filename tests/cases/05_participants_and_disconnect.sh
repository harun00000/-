#!/usr/bin/env bash
set -euo pipefail
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)/test_common.sh"

check_duplicate_client()
{
    start_server normal_config.txt -d 1
    start_client librarian
    start_client 0
    wait_for_line 'Зарегистрирован Librarian' server.log
    wait_for_line 'Зарегистрирован Reader 0' server.log
    start_client librarian duplicate_librarian
    wait_process "$last_pid" failure
    start_client 0 duplicate_reader
    wait_process "$last_pid" failure
    grep -q 'Ошибка регистрации' duplicate_librarian.log
    grep -q 'Ошибка регистрации' duplicate_reader.log
    kill -0 "$server_pid"
    start_client 1
    start_client 2
    for pid in "${pids[@]}"; do
        [[ -n ${statuses[$pid]+set} ]] || wait_process "$pid" success
    done
    check_model 3
}

check_wait_all_participants()
{
    start_server normal_config.txt -d 1
    start_client 0
    start_client 1
    wait_for_line 'Зарегистрирован Reader 1' server.log
    sleep 0.1
    ! grep -q 'День 1:' server.log
    kill -0 "$server_pid"
    start_client librarian
    wait_for_line 'Зарегистрирован Librarian' server.log
    sleep 0.1
    ! grep -q 'День 1:' server.log
    start_client 2
    wait_all_success
    check_model 3
}

check_disconnect_client()
{
    start_server fifo.txt
    start_paused_model
    local lost_pid=${pids[2]}
    kill -KILL "$lost_pid"
    wait_process "$lost_pid" failure
    wait_for_line "Отключился рабочий клиент Reader 0" server.log
    kill -0 "$server_pid"
    start_client 0
    kill -CONT "$librarian_pid"
    finish_workers
    grep -q "День 9: серверная обработка завершена" server.log
}

test_case()
{
    run_subcase duplicate_registration check_duplicate_client
    run_subcase waits_for_all check_wait_all_participants
    run_subcase working_disconnect check_disconnect_client
}

run_case participants_and_disconnect
