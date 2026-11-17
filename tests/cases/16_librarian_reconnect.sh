#!/usr/bin/env bash
set -euo pipefail
source "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)/test_common.sh"

test_case()
{
    run_subcase before_operation resume_librarian_test 0
    run_subcase after_operation resume_librarian_test 1
}
run_case librarian_reconnect
