#!/bin/sh
set -eu

test_repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_binary=$(mktemp "${TMPDIR:-/tmp}/switch-protocol.XXXXXX")
trap 'rm -f "$test_binary"' EXIT HUP INT TERM
cd "$test_repo_dir"

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic ${TEST_CFLAGS:-} \
    -I main/usb main/usb/switch_legacy_protocol.c tests/test_switch_legacy_protocol.c \
    -o "$test_binary"
"$test_binary"

# Each runner compiles the actual firmware sources with host platform stubs.
sh tests/run_usb_hid_transport.sh
for test_runner in tests/run_device_config.sh tests/run_ble_input.sh \
                   tests/run_usb_vendor.sh tests/run_control_protocol.sh; do
    sh "$test_runner"
done
