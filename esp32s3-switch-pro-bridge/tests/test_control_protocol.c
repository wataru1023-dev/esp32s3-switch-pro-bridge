#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "control_protocol.h"
#include "device_config.h"
#include "report_rate_stats.h"
#include "switch2_state.h"

static esp_err_t s_operation_result = ESP_OK;
static unsigned s_mutations;
static uint32_t s_axis_interval, s_raw_interval;
static char s_target[40];
static uint16_t s_rate = 66;
static bool s_auto = true, s_live_valid;

const char *esp_err_to_name(esp_err_t err)
{ return err == ESP_ERR_INVALID_ARG ? "ESP_ERR_INVALID_ARG" : "ESP_ERR_INVALID_STATE"; }
void esp_restart(void) { s_mutations++; }
void app_log_set_debug(bool enabled) { (void)enabled; s_mutations++; }
void switch2_gatt_set_axis_debug(bool enabled, uint32_t every)
{ (void)enabled; s_axis_interval = every; s_mutations++; }
void ble_central_set_imu_debug(bool enabled, uint32_t every)
{ (void)enabled; s_raw_interval = every; s_mutations++; }
esp_err_t ble_central_start_scan(void) { s_mutations++; return s_operation_result; }
esp_err_t ble_central_reconnect_saved_or_scan(void) { s_mutations++; return s_operation_result; }
esp_err_t ble_central_connect(const char *target)
{ assert(target); s_mutations++; return s_operation_result; }
void ble_central_disconnect(void) { s_mutations++; }
void ble_central_start_auto_reconnect(void) { s_mutations++; }
const char *ble_central_state_string(void) { return "idle"; }
const char *usb_hid_device_state_string(void) { return "mounted"; }
void usb_switch2_vendor_start_hd_rumble_self_test(void) { s_mutations++; }
void usb_switch2_vendor_stop_hd_rumble(void) { s_mutations++; }
bool usb_switch2_vendor_hd_rumble_active(void) { return false; }
bool device_config_get_ble_autoconnect(void) { return s_auto; }
void device_config_copy_ble_target(char *out, size_t capacity)
{ snprintf(out, capacity, "%s", s_target); }
uint16_t device_config_get_report_rate_hz(void) { return s_rate; }
const char *device_config_get_version(void) { return "5.9.14"; }
void device_config_set_bridge_running(bool enabled) { (void)enabled; s_mutations++; }
esp_err_t device_config_save_report_rate_hz(uint16_t rate)
{ s_mutations++; if (s_operation_result == ESP_OK) s_rate = rate; return s_operation_result; }
esp_err_t device_config_save_ble_autoconnect(bool enabled)
{ s_mutations++; if (s_operation_result == ESP_OK) s_auto = enabled; return s_operation_result; }
esp_err_t device_config_save_ble_target(const char *target)
{
    s_mutations++;
    if (strlen(target) >= sizeof(s_target)) return ESP_ERR_INVALID_ARG;
    if (s_operation_result == ESP_OK) snprintf(s_target, sizeof(s_target), "%s", target);
    return s_operation_result;
}
bool switch2_state_get_live(switch2_state_t *out, uint32_t *updates, int64_t *age)
{ (void)out; *updates = 20; *age = INT64_MAX; return s_live_valid; }
void switch2_state_get_live_stats(switch2_live_stats_t *out)
{ memset(out, 0, sizeof(*out)); out->actual_millihz = 66500; }
void report_rate_stats_get(report_rate_stats_snapshot_t *out)
{ memset(out, 0, sizeof(*out)); out->actual_millihz = 65123; }

static char s_reply[3072];
static void command(const char *text, bool ok)
{
    assert(control_protocol_handle_line(text, s_reply, sizeof(s_reply)) == ESP_OK);
    assert(strncmp(s_reply, ok ? "{\"ok\":true" : "{\"ok\":false", ok ? 10 : 11) == 0);
}

static void test_numeric_and_length_validation(void)
{
    const char *invalid[] = {"axis debug on -1", "axis debug on 0", "axis debug on 4294967296",
                            "axis debug on 99999999999999999999999999", "raw debug on -2",
                            "raw debug on +2", "rate 99999999999999999999999999", "rate 66xyz"};
    unsigned before = s_mutations;
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) command(invalid[i], false);
    assert(s_mutations == before);
    command("axis debug on 4294967295", true);
    assert(s_axis_interval == UINT32_MAX);
    command("raw debug on 10", true);
    assert(s_raw_interval == 10);
    char long_command[150];
    memcpy(long_command, "rate 66", 7);
    memset(long_command + 7, ' ', sizeof(long_command) - 9);
    long_command[sizeof(long_command) - 2] = 'x';
    long_command[sizeof(long_command) - 1] = 0;
    before = s_mutations;
    command(long_command, false); /* No executing a truncated 'rate 66' prefix. */
    assert(s_mutations == before);
}

static void test_json_escaping_and_status_units(void)
{
    command("bad\"\\\tname", false);
    assert(strstr(s_reply, "bad\\\"\\\\\\u0009name"));
    command("ble target pad\"\\\tname", true);
    assert(strstr(s_reply, "pad\\\"\\\\\\u0009name"));
    command("status", true);
    assert(strstr(s_reply, "\"report_actual_hz\":65.123"));
    assert(strstr(s_reply, "\"ble_input_hz\":66.500"));
    assert(strstr(s_reply, "\"live_age_ms\":-1"));
    assert(strstr(s_reply, "pad\\\"\\\\\\u0009name"));
}

static void test_failed_operations(void)
{
    s_operation_result = ESP_ERR_INVALID_STATE;
    const char *failed[] = {"rate 200", "ble target abc", "ble auto off", "ble auto on",
                           "ble forget", "ble scan", "ble connect", "ble connect abc"};
    for (size_t i = 0; i < sizeof(failed) / sizeof(failed[0]); ++i) {
        command(failed[i], false);
        assert(strstr(s_reply, "ESP_ERR_INVALID_STATE"));
    }
    assert(s_rate == 66 && s_auto);
    s_operation_result = ESP_OK;
}

static void test_reply_buffer_and_null_inputs(void)
{
    char small[8];
    assert(control_protocol_handle_line("status", small, sizeof(small)) == ESP_ERR_INVALID_SIZE);
    assert(small[0] == 0); /* Never ship truncated malformed JSON as success. */
    assert(control_protocol_handle_line(NULL, s_reply, sizeof(s_reply)) == ESP_ERR_INVALID_ARG);
    assert(control_protocol_handle_line("status", NULL, 8) == ESP_ERR_INVALID_ARG);
    assert(control_protocol_handle_line("status", s_reply, 0) == ESP_ERR_INVALID_ARG);
}

int main(void)
{
    test_numeric_and_length_validation();
    test_json_escaping_and_status_units();
    test_failed_operations();
    test_reply_buffer_and_null_inputs();
    puts("Control protocol safety regression tests passed");
    return 0;
}
