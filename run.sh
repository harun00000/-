#!/usr/bin/env bash
set -e

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cmake -S "$project_dir" -B "$project_dir/build"
cmake --build "$project_dir/build"
"$project_dir/build/library_sim"
