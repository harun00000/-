#!/usr/bin/env bash
set -euo pipefail
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)/test_common.sh"

test_case()
{
    full_run normal_config.txt
    grep -q 'День 1: начало' server.log
    grep -q 'День 13: серверная обработка завершена' server.log
    ! grep -q 'Зарегистрирован Observer' server.log
}
run_case observer_not_required
