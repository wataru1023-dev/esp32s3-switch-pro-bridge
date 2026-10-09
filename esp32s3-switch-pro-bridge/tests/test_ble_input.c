#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ble_session_epoch.h"
#include "gamepad_axis_math.h"
#include "switch2_gatt.h"

int64_t esp_timer_get_time(void)
{
    return 1000000;
}

static uint16_t expected_axis(uint16_t raw, uint16_t center)
{
    return gamepad_axis_normalize_12bit(raw, center,
                                       INTERNAL_GAMEPAD_AXIS_CENTER_DEADBAND,
                                       GAMEPAD_AXIS_PRO2_FULL_SCALE_RANGE);
}

static void make_fd2(uint8_t data[60], uint32_t buttons,
                     uint16_t lx, uint16_t ly, uint16_t rx, uint16_t ry)
{
    memset(data, 0, 60);
    for (unsigned i = 0; i < 4; i++) {
        data[4 + i] = (uint8_t)(buttons >> (8u * i));
    }
    gamepad_axis_pack_12bit_pair(data + 10, lx, ly);
    gamepad_axis_pack_12bit_pair(data + 13, rx, ry);
}

static void notify_center(uint16_t raw, unsigned count, uint32_t buttons,
                           switch2_state_t *state)
{
    uint8_t data[60];
    make_fd2(data, buttons, raw, raw, raw, raw);
    for (unsigned i = 0; i < count; i++) {
        assert(switch2_gatt_handle_notify(SWITCH2_NOTIFY_FD2_UUID,
                                           data, 16, state) == ESP_OK);
    }
}

static void test_axes_never_wait_for_center_learning(void)
{
    assert(!switch2_gatt_get_axis_debug(NULL));
    switch2_gatt_reset_axis_calibration();
    switch2_state_t state;
    switch2_state_reset(&state);
    uint8_t data[60];
    make_fd2(data, 0x8, 3648, 448, 3500, 600);
    for (unsigned i = 0; i < 100; i++) {
        assert(switch2_gatt_handle_notify(SWITCH2_NOTIFY_FD2_UUID,
                                           data, 16, &state) == ESP_OK);
        assert(state.lx == 4095 && state.ly == 0);
        assert(state.rx == expected_axis(3500, 2048));
        assert(state.ry == expected_axis(600, 2048));
        assert(switch2_state_get_button(&state, SWITCH2_BUTTON_A));
    }
}

static void test_calibration_interruption_freeze_and_reset(void)
{
    switch2_gatt_reset_axis_calibration();
    switch2_state_t state;
    switch2_state_reset(&state);
    notify_center(2180, 19, 0, &state);
    assert(state.lx == expected_axis(2180, 2048));
    notify_center(2180, 1, 0x8, &state); /* A interrupts training. */
    notify_center(2180, 19, 0, &state);
    assert(state.lx == expected_axis(2180, 2048));
    notify_center(2180, 1, 0, &state);
    assert(state.lx == 2048);

    notify_center(2048, 100, 0, &state);
    assert(state.lx == expected_axis(2048, 2180)); /* Learned center stays fixed. */
    switch2_gatt_reset_axis_calibration();
    notify_center(2180, 1, 0, &state);
    assert(state.lx == expected_axis(2180, 2048));
}

static void test_diagnostic_reads_do_not_train_or_interrupt(void)
{
    switch2_gatt_reset_axis_calibration();
    switch2_state_t state;
    switch2_state_reset(&state);
    uint8_t data[60];
    make_fd2(data, 0, 2180, 2180, 2180, 2180);
    for (unsigned i = 0; i < 100; i++) {
        assert(switch2_gatt_parse_diagnostic(SWITCH2_NOTIFY_FD2_UUID,
                                              data, 16, &state) == ESP_OK);
    }
    assert(state.lx == expected_axis(2180, 2048));
    notify_center(2180, 19, 0, &state);
    make_fd2(data, 0x8, 3600, 3600, 3600, 3600);
    assert(switch2_gatt_parse_diagnostic(SWITCH2_NOTIFY_FD2_UUID,
                                          data, 16, &state) == ESP_OK);
    notify_center(2180, 1, 0, &state);
    assert(state.lx == 2048); /* A diagnostic read did not reset the 19 samples. */
}

static void test_short_reports_and_motion(void)
{
    switch2_gatt_reset_axis_calibration();
    switch2_state_t state;
    switch2_state_reset(&state);
    state.lx = 3010;
    state.ly = 1010;
    state.rx = 3210;
    state.ry = 1210;
    memset(state.motion, 0x5a, sizeof(state.motion));
    state.motion_valid = true;
    uint8_t data[60];
    make_fd2(data, 0x8, 3648, 448, 3648, 448);
    switch2_state_t before = state;
    assert(switch2_gatt_handle_notify(SWITCH2_NOTIFY_FD2_UUID,
                                       data, 7, &state) == ESP_ERR_NOT_FOUND);
    assert(memcmp(&before, &state, sizeof(state)) == 0);
    for (uint16_t len = 8; len < 16; len++) {
        assert(switch2_gatt_handle_notify(SWITCH2_NOTIFY_FD2_UUID,
                                           data, len, &state) == ESP_OK);
        assert(state.lx == 3010 && state.ly == 1010);
        assert(state.rx == 3210 && state.ry == 1210);
        assert(state.motion_valid && state.motion[0] == 0x5a);
        assert(switch2_state_get_button(&state, SWITCH2_BUTTON_A));
    }
    for (unsigned i = 48; i < 60; i++) {
        data[i] = (uint8_t)i;
    }
    assert(switch2_gatt_handle_notify(SWITCH2_NOTIFY_FD2_UUID,
                                       data, 60, &state) == ESP_OK);
    assert(state.lx == 4095 && state.ly == 0);
    for (unsigned i = 0; i < 36; i++) {
        assert(state.motion[i] == 48 + i % 12);
    }

    uint8_t legacy[11] = {0};
    legacy[2] = 0x2;
    gamepad_axis_pack_12bit_pair(legacy + 5, 448, 3648);
    gamepad_axis_pack_12bit_pair(legacy + 8, 3648, 448);
    for (uint16_t len = 5; len < 11; len++) {
        assert(switch2_gatt_handle_notify(SWITCH2_NOTIFY_LEGACY_UUID,
                                           legacy, len, &state) == ESP_OK);
        assert(state.lx == 4095 && state.ly == 0);
    }
    assert(switch2_gatt_handle_notify(SWITCH2_NOTIFY_LEGACY_UUID,
                                       legacy, 11, &state) == ESP_OK);
    assert(state.lx == 0 && state.ly == 4095);
    assert(state.rx == 4095 && state.ry == 0);
}

static void test_invalid_inputs(void)
{
    uint8_t data[60] = {0};
    switch2_state_t state;
    switch2_state_reset(&state);
    assert(switch2_gatt_handle_notify(NULL, data, 16, &state) == ESP_ERR_INVALID_ARG);
    assert(switch2_gatt_handle_notify(SWITCH2_NOTIFY_FD2_UUID,
                                       NULL, 16, &state) == ESP_ERR_INVALID_ARG);
    assert(switch2_gatt_handle_notify(SWITCH2_NOTIFY_FD2_UUID,
                                       data, 16, NULL) == ESP_ERR_INVALID_ARG);
    assert(!switch2_gatt_set_motion_source_offset(52));
    switch2_state_reset(NULL);
    switch2_state_set_button(NULL, SWITCH2_BUTTON_A, true);
    switch2_state_set_button(&state, (switch2_button_t)-1, true);
    assert(state.buttons == 0);
    assert(!switch2_state_get_button(NULL, SWITCH2_BUTTON_A));
    assert(!switch2_state_get_button(&state, (switch2_button_t)-1));
    switch2_state_update_from_fd2_buttons(NULL, 0);
    switch2_state_update_from_legacy_bytes(NULL, 0, 0, 0);
    switch2_state_from_internal(NULL, NULL);
    switch2_state_to_internal(NULL, NULL);
}

static void test_reconnect_epoch_isolation(void)
{
    ble_session_epoch_t epoch = {0};
    ble_session_epoch_invalidate(&epoch);
    ble_session_epoch_activate(&epoch, 7);
    uint32_t old_generation = epoch.generation;
    assert(ble_session_epoch_matches(&epoch, 7, old_generation));
    assert(!ble_session_epoch_matches(&epoch, 8, old_generation));
    ble_session_epoch_invalidate(&epoch);
    assert(!ble_session_epoch_matches(&epoch, 7, old_generation));
    ble_session_epoch_activate(&epoch, 7); /* Controller reused connection handle. */
    assert(!ble_session_epoch_matches(&epoch, 7, old_generation));
    assert(ble_session_epoch_matches(&epoch, 7, epoch.generation));
    epoch.generation = UINT32_MAX;
    ble_session_epoch_invalidate(&epoch);
    assert(epoch.generation == 1 && !epoch.active);
}

int main(void)
{
    test_axes_never_wait_for_center_learning();
    test_calibration_interruption_freeze_and_reset();
    test_diagnostic_reads_do_not_train_or_interrupt();
    test_short_reports_and_motion();
    test_invalid_inputs();
    test_reconnect_epoch_isolation();
    puts("BLE input regression tests passed");
    return 0;
}
