#!/bin/sh
set -eu
test_repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_binary=$(mktemp "${TMPDIR:-/tmp}/switch-usb-vendor.XXXXXX")
trap 'rm -f "$test_binary"' EXIT HUP INT TERM
cd "$test_repo_dir"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${TEST_CFLAGS:-} \
    -I tests/usb_stubs -I main/usb -I main/ble -I main/control \
    -I main/config -I main/bridge -I main/log \
    tests/test_usb_vendor_safety.c main/usb/usb_switch2_vendor.c \
    -o "$test_binary"
"$test_binary"
