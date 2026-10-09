#!/bin/sh
set -eu

test_repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_binary=$(mktemp "${TMPDIR:-/tmp}/switch-config.XXXXXX")
trap 'rm -f "$test_binary"' EXIT HUP INT TERM

cd "$test_repo_dir"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pthread ${TEST_CFLAGS:-} \
    -I tests/config_stubs -I main/config -I main/log \
    tests/test_device_config.c main/config/device_config.c -o "$test_binary"
"$test_binary"
