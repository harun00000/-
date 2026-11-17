#!/usr/bin/env bash
set -euo pipefail
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)/test_common.sh"

check_observer_connect()
{
    start_server fifo.txt
    start_observer
    sleep 0.1
    kill -0 "$observer_pid"
    kill -0 "$server_pid"
    grep -q 'Зарегистрирован Observer' server.log
    ! grep -q 'День 1:' server.log
}

check_observer_second_accepted()
{
    start_server fifo.txt
    start_observer
    start_process second_observer "$build_dir/library_observer_client" --host 127.0.0.1 --port "$port"
    local second_pid=$last_pid
    wait_for_line 'Observer подключён' "$case_dir/second_observer.log"
    kill -0 "$second_pid"
    kill -0 "$observer_pid"
    kill -0 "$server_pid"
    start_workers fifo.txt
    finish_workers
    grep -q 'Observer: нормальное завершение' observer.log
    grep -q 'Observer: нормальное завершение' second_observer.log
    diff observer.log second_observer.log
}

check_observer_before_clients()
{
    start_server fifo.txt
    start_observer
    ! grep -q 'Зарегистрирован Reader\|Зарегистрирован Librarian' server.log
    ! grep -q 'моделирование начато' observer.log
    start_workers fifo.txt
    finish_workers
    grep -q '^День 0: моделирование начато$' observer.log
    grep -q '^День 1: день начат$' observer.log
}

check_observer_during_model()
{
    start_server signals.txt
    start_workers signals.txt
    wait_for_line 'День 3: начало' server.log
    kill -STOP "$librarian_pid"
    start_observer
    ! grep -q 'моделирование начато' observer.log
    kill -CONT "$librarian_pid"
    wait_for_line 'день завершён' "$case_dir/observer.log"
    wait_for_line 'Reader завершил фазу' "$case_dir/observer.log"
    # длинный запуск завершаем штатным пользовательским прерыванием
    kill -TERM "$server_pid"
    for pid in "${pids[@]}"; do wait_process "$pid" success; done
    grep -q 'Получен сигнал' server.log
}

check_observer_no_state_change()
{
    full_run fifo.txt
    stable_result > baseline.txt
    local baseline="$case_dir/baseline.txt"
    pids=()
    statuses=()
    mkdir with_observer
    case_dir="$case_dir/with_observer"
    cd "$case_dir"
    start_server fifo.txt
    start_observer
    start_workers fifo.txt
    finish_workers
    stable_result > observed.txt
    diff "$baseline" observed.txt
    grep -q 'Observer: нормальное завершение' observer.log
}

test_case()
{
    run_subcase observer_connect check_observer_connect
    run_subcase observer_second_accepted check_observer_second_accepted
    run_subcase observer_before_clients check_observer_before_clients
    run_subcase observer_during_model check_observer_during_model
    run_subcase observer_no_state_change check_observer_no_state_change
}

run_case observer_integration
