#!/usr/bin/env bash
set -euo pipefail

http_file="tests/http_server.fyra"
sqlite_file="tests/sqlite_clone.fyra"
sqlite_test="tests/test_sqlite_clone.cpp"

# HTTP server must be capability/extern based and syscall-free.
rg -q '^extern net\.socket' "$http_file"
rg -q '^extern net\.bind' "$http_file"
rg -q '^extern net\.listen' "$http_file"
rg -q '^extern net\.accept' "$http_file"
rg -q '^extern io\.write' "$http_file"
rg -q '^extern io\.close' "$http_file"
! rg -q '\bsyscall\b' "$http_file"

# sqlite clone IR must remain syscall-free.
! rg -q '\bsyscall\b' "$sqlite_file"

# sqlite clone test still asserts expected exit semantics (59).
rg -q 'Expected 59' "$sqlite_test"
rg -q 'exitCode == 59' "$sqlite_test"

echo "extern http/sqlite smoke checks passed"
