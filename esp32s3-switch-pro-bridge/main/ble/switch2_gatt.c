#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "app_log.h"
#include "gamepad_axis_math.h"
#include "switch2_gatt.h"

static const char *TAG = "switch2_gatt";

#define CENTER_12BIT 2048
#define AXIS_DEADZONE INTERNAL_GAMEPAD_AXIS_CENTER_DEADBAND
#define AXIS_CALIBRATION_SAMPLES 20
#define AXIS_CENTER_LEARN_MAX_DELTA 256
#define FD2_FULL_REPORT_MIN_LEN 60
#define FD2_FULL_MOTION_OFFSET 48
#define MOTION_NOTIFY_MAX_OFFSET 51

typedef struct {
    bool calibrated;
    uint32_t sample_count;
    uint32_t sum_lx;
    uint32_t sum_ly;
    uint32_t sum_rx;
    uint32_t sum_ry;
    uint16_t center_lx;
    uint16_t center_ly;
    uint16_t center_rx;
    uint16_t center_ry;
} axis_calibration_t;

static portMUX_TYPE s_parser_lock = portMUX_INITIALIZER_UNLOCKED;
static axis_calibration_t s_fd2_axis = {
    .center_lx = CENTER_12BIT,
    .center_ly = CENTER_12BIT,
    .center_rx = CENTER_12BIT,
    .center_ry = CENTER_12BIT,
};
static axis_calibration_t s_legacy_axis = {
    .center_lx = CENTER_12BIT,
    .center_ly = CENTER_12BIT,
    .center_rx = CENTER_12BIT,
    .center_ry = CENTER_12BIT,
};
static uint8_t s_motion_source_offset = FD2_FULL_MOTION_OFFSET;
static bool s_motion_full_only = true;
static bool s_axis_debug_enabled;
static uint32_t s_axis_debug_every = 32;
static uint32_t s_axis_debug_seen;

static void axis_calibration_reset(axis_calibration_t *cal)
{
    if (!cal) {
        return;
    }
    memset(cal, 0, sizeof(*cal));
    cal->center_lx = CENTER_12BIT;
    cal->center_ly = CENTER_12BIT;
    cal->center_rx = CENTER_12BIT;
    cal->center_ry = CENTER_12BIT;
}

void switch2_gatt_reset_axis_calibration(void)
{
    portENTER_CRITICAL(&s_parser_lock);
    axis_calibration_reset(&s_fd2_axis);
    axis_calibration_reset(&s_legacy_axis);
    portEXIT_CRITICAL(&s_parser_lock);
    APP_LOGI(TAG, "axis calibration reset");
}

void switch2_gatt_set_axis_debug(bool enabled, uint32_t every)
{
    portENTER_CRITICAL(&s_parser_lock);
    s_axis_debug_enabled = enabled;
    s_axis_debug_every = every == 0 ? 32 : every;
    s_axis_debug_seen = 0;
    uint32_t interval = s_axis_debug_every;
    portEXIT_CRITICAL(&s_parser_lock);
    APP_LOGI(TAG, "axis debug %s every=%lu",
             enabled ? "enabled" : "disabled",
             (unsigned long)interval);
}

bool switch2_gatt_get_axis_debug(uint32_t *out_every)
{
    portENTER_CRITICAL(&s_parser_lock);
    if (out_every) {
        *out_every = s_axis_debug_every;
    }
    bool enabled = s_axis_debug_enabled;
    portEXIT_CRITICAL(&s_parser_lock);
    return enabled;
}

bool switch2_gatt_set_motion_source_offset(uint8_t offset)
{
    if (offset > MOTION_NOTIFY_MAX_OFFSET) {
        return false;
    }
    portENTER_CRITICAL(&s_parser_lock);
    s_motion_source_offset = offset;
    portEXIT_CRITICAL(&s_parser_lock);
    return true;
}

uint8_t switch2_gatt_get_motion_source_offset(void)
{
    portENTER_CRITICAL(&s_parser_lock);
    uint8_t offset = s_motion_source_offset;
    portEXIT_CRITICAL(&s_parser_lock);
    return offset;
}

void switch2_gatt_set_motion_full_only(bool enabled)
{
    portENTER_CRITICAL(&s_parser_lock);
    s_motion_full_only = enabled;
    portEXIT_CRITICAL(&s_parser_lock);
}

bool switch2_gatt_get_motion_full_only(void)
{
    portENTER_CRITICAL(&s_parser_lock);
    bool enabled = s_motion_full_only;
    portEXIT_CRITICAL(&s_parser_lock);
    return enabled;
}

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t clamp12(int32_t value)
{
    if (value < 0) {
        return 0;
    }
    if (value > 4095) {
        return 4095;
    }
    return (uint16_t)value;
}

static uint16_t unpack12_x(const uint8_t *data, int offset)
{
    return clamp12((int32_t)data[offset] | (((int32_t)data[offset + 1] & 0x0f) << 8));
}

static uint16_t unpack12_y(const uint8_t *data, int offset)
{
    return clamp12((((int32_t)data[offset + 1] >> 4) & 0x0f) | ((int32_t)data[offset + 2] << 4));
}

static bool axis_near_factory_center(uint16_t value)
{
    int32_t delta = (int32_t)value - CENTER_12BIT;
    if (delta < 0) {
        delta = -delta;
    }
    return delta <= AXIS_CENTER_LEARN_MAX_DELTA;
}

static bool axes_look_centered(uint16_t lx,
                               uint16_t ly,
                               uint16_t rx,
                               uint16_t ry)
{
    return axis_near_factory_center(lx) &&
           axis_near_factory_center(ly) &&
           axis_near_factory_center(rx) &&
           axis_near_factory_center(ry);
}

static void apply_axes(axis_calibration_t *cal,
                       const char *source,
                       switch2_state_t *state,
                       uint16_t lx,
                       uint16_t ly,
                       uint16_t rx,
                       uint16_t ry,
                       bool learn_center)
{
    bool learned_now = false;
    bool log_axes = false;
    uint32_t sample = 0;
    portENTER_CRITICAL(&s_parser_lock);
    if (learn_center && !cal->calibrated &&
        state->buttons == 0 &&
        axes_look_centered(lx, ly, rx, ry)) {
        cal->sum_lx += lx;
        cal->sum_ly += ly;
        cal->sum_rx += rx;
        cal->sum_ry += ry;
        cal->sample_count++;
        if (cal->sample_count >= AXIS_CALIBRATION_SAMPLES) {
            cal->center_lx = (uint16_t)(cal->sum_lx / cal->sample_count);
            cal->center_ly = (uint16_t)(cal->sum_ly / cal->sample_count);
            cal->center_rx = (uint16_t)(cal->sum_rx / cal->sample_count);
            cal->center_ry = (uint16_t)(cal->sum_ry / cal->sample_count);
            cal->calibrated = true;
            learned_now = true;
        }
    } else if (learn_center && !cal->calibrated) {
        cal->sample_count = 0;
        cal->sum_lx = 0;
        cal->sum_ly = 0;
        cal->sum_rx = 0;
        cal->sum_ry = 0;
    }

    /*
     * Do not suppress stick input while the optional center-learning phase is
     * still running.  The old code forced all four axes to 2048 here until it
     * had observed 20 consecutive centered frames.  If the controller was
     * being held, or a button was pressed during connection, that condition
     * could never be met, leaving buttons alive but all sticks permanently
     * centered.  Use the factory center as a safe fallback and replace it
     * with the learned center once calibration completes.
     */
    uint16_t center_lx = cal->calibrated ? cal->center_lx : CENTER_12BIT;
    uint16_t center_ly = cal->calibrated ? cal->center_ly : CENTER_12BIT;
    uint16_t center_rx = cal->calibrated ? cal->center_rx : CENTER_12BIT;
    uint16_t center_ry = cal->calibrated ? cal->center_ry : CENTER_12BIT;

    if (learn_center && s_axis_debug_enabled) {
        sample = ++s_axis_debug_seen;
        uint32_t every = s_axis_debug_every == 0 ? 32 : s_axis_debug_every;
        log_axes = every <= 1 || (sample % every) == 0;
    }
    portEXIT_CRITICAL(&s_parser_lock);

    if (learned_now) {
        APP_LOGI(TAG, "auto center %s lx=%u ly=%u rx=%u ry=%u",
                 source, (unsigned)center_lx, (unsigned)center_ly,
                 (unsigned)center_rx, (unsigned)center_ry);
    }

    state->lx = gamepad_axis_normalize_12bit(
        lx, center_lx, AXIS_DEADZONE,
        GAMEPAD_AXIS_PRO2_FULL_SCALE_RANGE);
    state->ly = gamepad_axis_normalize_12bit(
        ly, center_ly, AXIS_DEADZONE,
        GAMEPAD_AXIS_PRO2_FULL_SCALE_RANGE);
    state->rx = gamepad_axis_normalize_12bit(
        rx, center_rx, AXIS_DEADZONE,
        GAMEPAD_AXIS_PRO2_FULL_SCALE_RANGE);
    state->ry = gamepad_axis_normalize_12bit(
        ry, center_ry, AXIS_DEADZONE,
        GAMEPAD_AXIS_PRO2_FULL_SCALE_RANGE);

    if (log_axes) {
            APP_LOGI(TAG,
                     "AXIS_DEBUG src=%s sample=%lu raw=[%u,%u,%u,%u] center=[%u,%u,%u,%u] out=[%u,%u,%u,%u] buttons=0x%08lx",
                     source,
                     (unsigned long)sample,
                     (unsigned)lx,
                     (unsigned)ly,
                     (unsigned)rx,
                     (unsigned)ry,
                     (unsigned)center_lx,
                     (unsigned)center_ly,
                     (unsigned)center_rx,
                     (unsigned)center_ry,
                     (unsigned)state->lx,
                     (unsigned)state->ly,
                     (unsigned)state->rx,
                     (unsigned)state->ry,
                     (unsigned long)state->buttons);
    }
}

static void apply_motion_if_available(switch2_state_t *state, const uint8_t *data, uint16_t len)
{
    portENTER_CRITICAL(&s_parser_lock);
    uint8_t offset = s_motion_source_offset;
    bool full_only = s_motion_full_only;
    portEXIT_CRITICAL(&s_parser_lock);
    if ((uint16_t)offset + SWITCH2_MOTION_SAMPLE_SIZE > len) {
        return;
    }

    /*
     * Pro2 7FD2 full BLE reports carry one raw IMU sample at bytes 48..59:
     * accel XYZ followed by gyro XYZ. The USB 0x05 report uses the same
     * 12-byte layout shifted by one byte because report[0] is the report ID.
     */
    if (full_only && len < FD2_FULL_REPORT_MIN_LEN) {
        return;
    }

    switch2_state_set_motion_sample(state, data + offset, SWITCH2_MOTION_SAMPLE_SIZE);
}

static esp_err_t parse_report(const char *uuid, const uint8_t *data, uint16_t len,
                              switch2_state_t *out_state, bool learn_center)
{
    if (!uuid || !data || !out_state) {
        return ESP_ERR_INVALID_ARG;
    }

    APP_LOGD(TAG, "notify uuid=%s len=%u", uuid, (unsigned)len);

    if (strcmp(uuid, SWITCH2_NOTIFY_FD2_UUID) == 0 && len >= 8) {
        switch2_state_update_from_fd2_buttons(out_state, read_le32(&data[4]));
        if (len >= 16) {
            apply_axes(&s_fd2_axis,
                       "fd2",
                       out_state,
                       unpack12_x(data, 10),
                       unpack12_y(data, 10),
                       unpack12_x(data, 13),
                       unpack12_y(data, 13),
                       learn_center);
        }
        apply_motion_if_available(out_state, data, len);
        return ESP_OK;
    }

    if (strcmp(uuid, SWITCH2_NOTIFY_LEGACY_UUID) == 0 && len >= 5) {
        switch2_state_update_from_legacy_bytes(out_state, data[2], data[3], data[4]);
        if (len >= 11) {
            apply_axes(&s_legacy_axis,
                       "legacy",
                       out_state,
                       unpack12_x(data, 5),
                       unpack12_y(data, 5),
                       unpack12_x(data, 8),
                       unpack12_y(data, 8),
                       learn_center);
        }
        return ESP_OK;
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t switch2_gatt_handle_notify(const char *uuid, const uint8_t *data,
                                    uint16_t len, switch2_state_t *out_state)
{
    return parse_report(uuid, data, len, out_state, true);
}

esp_err_t switch2_gatt_parse_diagnostic(const char *uuid, const uint8_t *data,
                                       uint16_t len, switch2_state_t *out_state)
{
    /* Reads can be cached: observe the current centers without training them. */
    return parse_report(uuid, data, len, out_state, false);
}

esp_err_t switch2_gatt_send_rumble_stub(const uint8_t *data, uint16_t len)
{
    (void)data;
    (void)len;
    APP_LOGI(TAG, "legacy rumble stub is unused; Pro2 rumble is handled by the cc48 HD stream path");
    return ESP_ERR_NOT_SUPPORTED;
}
