#!/bin/sh
set -eu

test_repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_binary=$(mktemp "${TMPDIR:-/tmp}/switch-ble-input.XXXXXX")
trap 'rm -f "$test_binary"' EXIT HUP INT TERM

cd "$test_repo_dir"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${TEST_CFLAGS:-} \
    -I tests/usb_stubs -I main/ble -I main/bridge -I main/log \
    tests/test_ble_input.c main/ble/switch2_gatt.c \
    main/bridge/switch2_state.c main/bridge/internal_gamepad_state.c \
    main/bridge/gamepad_axis_math.c -o "$test_binary"
"$test_binary"
