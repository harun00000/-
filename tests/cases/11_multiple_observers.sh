#!/usr/bin/env bash
set -euo pipefail
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)/test_common.sh"

test_case()
{
    start_server fifo.txt
    start_three_observers
    start_workers fifo.txt
    finish_workers
    for label in observer_a observer_b observer_c; do
        check_complete_observer "$label.log" 9
    done
    diff observer_a.log observer_b.log
    diff observer_a.log observer_c.log
}
run_case multiple_observers
