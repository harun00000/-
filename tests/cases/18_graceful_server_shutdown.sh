#!/usr/bin/env bash
set -euo pipefail
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)/test_common.sh"

check_shutdown()
{
    local signal=$1 stopped=$2 pid stopped_pid=0 started finished_count=0
    start_server signals.txt
    start_observer observer_a
    start_observer observer_b
    if ((stopped)); then
        stopped_pid=$observer_pid
        kill -STOP "$stopped_pid"
    fi
    start_workers signals.txt
    wait_for_line 'День 3: начало' server.log
    # дополнительное зарегистрированное соединение проверяет final на уровне кадров
    exec {raw_fd}<>/dev/tcp/127.0.0.1/"$port"
    raw_send 14 -3 0 0 0 0
    raw_expect 3
    started=$SECONDS
    kill -"$signal" "$server_pid"
    while raw_read; do
        if [[ ${raw_frame[0]} == 13 ]]; then
            finished_count=$((finished_count + 1))
            [[ ${raw_frame[4]} == 1 ]]
        fi
    done
    raw_close
    [[ $finished_count == 1 ]] || fail 'final message отсутствует или дублируется'
    wait_process "$server_pid" success
    ((SECONDS - started <= 5)) || fail 'shutdown не ограничен по времени'
    for pid in "${pids[@]}"; do
        [[ $pid == "$stopped_pid" ]] || wait_process "$pid" success
    done
    if ((stopped)); then
        kill -CONT "$stopped_pid"
        local code=0
        wait "$stopped_pid" || code=$?
        statuses[$stopped_pid]=$code
    fi
    [[ $(grep -c 'Observer: сервер завершает работу' observer_a.log) == 1 ]]
    for index in 0 1; do
        [[ $(grep -c 'Сервер завершает работу:' "reader_$index.log") == 1 ]]
        ! grep -q 'сервер отключился до NET_FINISHED' "reader_$index.log"
    done
    [[ $(grep -c 'Сервер завершает работу:' librarian.log) == 1 ]]
    for pid in "${pids[@]}"; do ! kill -0 "$pid" 2>/dev/null || fail "остался PID $pid"; done
}

check_unexpected_failure()
{
    start_server signals.txt
    start_observer observer_a
    start_observer observer_b
    start_workers signals.txt
    wait_for_line "День 3: начало" server.log
    kill -KILL "$server_pid"
    for pid in "${pids[@]}"; do wait_process "$pid" failure; done
    grep -Eq "EOF|recv|send|сервер отключился" reader_0.log
    ! grep -q "Сервер завершает работу:" reader_0.log
}

test_case()
{
    run_subcase sigint check_shutdown INT 0
    run_subcase sigterm check_shutdown TERM 0
    run_subcase stopped_observer check_shutdown TERM 1
    run_subcase unexpected_failure check_unexpected_failure
}
run_case graceful_server_shutdown
