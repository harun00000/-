#!/usr/bin/env bash
set -euo pipefail
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)/test_common.sh"

test_case()
{
    start_server fifo.txt
    start_observer observer_a
    local first_pid=$observer_pid old_slot new_slot
    old_slot=$(sed -n 's/^Зарегистрирован Observer; slot=//p' server.log)
    start_paused_model
    kill -TERM "$first_pid"
    wait_process "$first_pid" failure
    wait_for_line "Observer отключён; slot=$old_slot" server.log
    start_observer observer_d
    new_slot=$(sed -n 's/^Зарегистрирован Observer; slot=//p' server.log | tail -1)
    [[ -n $old_slot && $new_slot == "$old_slot" ]] || fail 'слот Observer не переиспользован'
    ! grep -q 'моделирование начато\|: день начат$' observer_d.log
    kill -CONT "$librarian_pid"
    finish_workers
    grep -q 'День 2: день начат' observer_d.log
    grep -q '; получение книги;' observer_d.log
    [[ $(grep -c 'Observer: нормальное завершение' observer_d.log) == 1 ]]
}
run_case observer_reconnect
