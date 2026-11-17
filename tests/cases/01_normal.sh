#!/usr/bin/env bash
set -euo pipefail
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)/test_common.sh"

test_case()
{
    full_run normal_config.txt
    grep -q 'День 13: серверная обработка завершена' server.log
    grep -q 'Невыполнен' server.log
}

run_case normal
