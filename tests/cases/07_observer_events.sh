#!/usr/bin/env bash
set -euo pipefail
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)/test_common.sh"

test_case()
{
    start_server normal_config.txt
    start_observer
    start_workers normal_config.txt
    finish_workers
    check_observer_events
    grep -q "^День 13: моделирование завершено$" observer.log
    ! kill -0 "$observer_pid" 2>/dev/null
    for index in 0 1 2; do grep -q "Reader $index; приход читателя;" observer.log; done
    grep -q 'Observer: нормальное завершение' observer.log
    grep -q 'Моделирование завершено:' librarian.log
    for index in 0 1 2; do grep -q 'Моделирование завершено:' "reader_$index.log"; done
    for pid in "${pids[@]}"; do ! kill -0 "$pid" 2>/dev/null || fail "остался PID $pid"; done
}
run_case observer_events
