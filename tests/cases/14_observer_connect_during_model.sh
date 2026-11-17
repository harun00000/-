#!/usr/bin/env bash
set -euo pipefail
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)/test_common.sh"

test_case()
{
    start_server fifo.txt
    start_paused_model
    ! grep -q 'Зарегистрирован Observer' server.log
    start_observer observer_a
    start_observer observer_b
    for label in observer_a observer_b; do
        grep -q 'текущий день 1;' "$label.log"
        ! grep -q 'моделирование начато\|: день начат$\|Librarian начал фазу' "$label.log"
    done
    kill -CONT "$librarian_pid"
    finish_workers
    for label in observer_a observer_b; do
        grep -q '^День 2: день начат$' "$label.log"
        grep -q '; получение книги;' "$label.log"
        grep -q 'Observer: нормальное завершение' "$label.log"
    done
    diff observer_a.log observer_b.log
    [[ $(grep -c '^День 1: начало серверной фазы' server.log) == 1 ]]
}
run_case observer_connect_during_model
