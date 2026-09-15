#pragma once

#include <stdint.h>
#include "hid_report.h"
#include "internal_gamepad_state.h"

void report_mapper_internal_to_switch_legacy_report(const internal_gamepad_state_t *state,
                                                    uint8_t report[SWITCH_LEGACY_REPORT_SIZE]);
