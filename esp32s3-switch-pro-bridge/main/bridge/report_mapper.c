#include "report_mapper.h"

#include "gamepad_axis_math.h"

static uint8_t s_switch_legacy_input_seq;

void report_mapper_internal_to_switch_legacy_report(const internal_gamepad_state_t *state,
                                                    uint8_t report[SWITCH_LEGACY_REPORT_SIZE])
{
    hid_report_make_switch_legacy_neutral(report);
    if (!state) {
        return;
    }

    internal_gamepad_state_t snapped = *state;
    internal_gamepad_state_apply_center_snap(&snapped);
    state = &snapped;

    report[1] = s_switch_legacy_input_seq++;

    if (internal_gamepad_state_get_button(state, INTERNAL_GAMEPAD_BUTTON_WEST)) report[3] |= 0x01;
    if (internal_gamepad_state_get_button(state, INTERNAL_GAMEPAD_BUTTON_NORTH)) report[3] |= 0x02;
    if (internal_gamepad_state_get_button(state, INTERNAL_GAMEPAD_BUTTON_SOUTH)) report[3] |= 0x04;
    if (internal_gamepad_state_get_button(state, INTERNAL_GAMEPAD_BUTTON_EAST)) report[3] |= 0x08;
    if (internal_gamepad_state_get_button(state, INTERNAL_GAMEPAD_BUTTON_R1)) report[3] |= 0x40;
    if (internal_gamepad_state_get_button(state, INTERNAL_GAMEPAD_BUTTON_R2)) report[3] |= 0x80;

    if (internal_gamepad_state_get_button(state, INTERNAL_GAMEPAD_BUTTON_BACK)) report[4] |= 0x01;
    if (internal_gamepad_state_get_button(state, INTERNAL_GAMEPAD_BUTTON_START)) report[4] |= 0x02;
    if (internal_gamepad_state_get_button(state, INTERNAL_GAMEPAD_BUTTON_RSTICK)) report[4] |= 0x04;
    if (internal_gamepad_state_get_button(state, INTERNAL_GAMEPAD_BUTTON_LSTICK)) report[4] |= 0x08;
    if (internal_gamepad_state_get_button(state, INTERNAL_GAMEPAD_BUTTON_HOME)) report[4] |= 0x10;
    if (internal_gamepad_state_get_button(state, INTERNAL_GAMEPAD_BUTTON_CAPTURE)) report[4] |= 0x20;

    if (internal_gamepad_state_get_button(state, INTERNAL_GAMEPAD_BUTTON_DPAD_DOWN)) report[5] |= 0x01;
    if (internal_gamepad_state_get_button(state, INTERNAL_GAMEPAD_BUTTON_DPAD_UP)) report[5] |= 0x02;
    if (internal_gamepad_state_get_button(state, INTERNAL_GAMEPAD_BUTTON_DPAD_RIGHT)) report[5] |= 0x04;
    if (internal_gamepad_state_get_button(state, INTERNAL_GAMEPAD_BUTTON_DPAD_LEFT)) report[5] |= 0x08;
    if (internal_gamepad_state_get_button(state, INTERNAL_GAMEPAD_BUTTON_L1)) report[5] |= 0x40;
    if (internal_gamepad_state_get_button(state, INTERNAL_GAMEPAD_BUTTON_L2)) report[5] |= 0x80;

    gamepad_axis_pack_12bit_pair(report + 6, state->lx, state->ly);
    gamepad_axis_pack_12bit_pair(report + 9, state->rx, state->ry);
}
