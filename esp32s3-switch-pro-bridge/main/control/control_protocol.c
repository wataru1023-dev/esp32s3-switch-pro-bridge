#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "esp_err.h"
#include "esp_system.h"
#include "ble_central.h"
#include "device_config.h"
#include "report_rate_stats.h"
#include "switch2_state.h"
#include "usb_hid_device.h"
#include "usb_switch2_vendor.h"
#include "control_protocol.h"

static int json_ok(char *reply, int reply_len, const char *cmd, const char *extra)
{
    return snprintf(reply, (size_t)reply_len, "{\"ok\":true,\"cmd\":\"%s\",%s}", cmd, extra);
}

static int json_error(char *reply, int reply_len, const char *cmd, const char *error)
{
    return snprintf(reply, (size_t)reply_len, "{\"ok\":false,\"cmd\":\"%s\",\"error\":\"%s\"}", cmd, error);
}

static bool copy_trimmed_arg(const char *src, char *dst, size_t dst_len)
{
    if (!src || !dst || dst_len == 0) {
        return false;
    }
    while (*src && isspace((unsigned char)*src)) {
        src++;
    }
    size_t n = 0;
    while (*src && n + 1 < dst_len) {
        dst[n++] = *src++;
    }
    dst[n] = 0;
    while (n > 0 && isspace((unsigned char)dst[n - 1])) {
        dst[--n] = 0;
    }
    return n > 0;
}

void control_protocol_init(void)
{
}

static void handle_status(char *reply, int reply_len)
{
    uint32_t live_updates = 0;
    int64_t live_age_us = 0;
    bool live_valid = switch2_state_get_live(NULL, &live_updates, &live_age_us);
    switch2_live_stats_t live_stats;
    switch2_state_get_live_stats(&live_stats);
    report_rate_stats_snapshot_t report_stats;
    report_rate_stats_get(&report_stats);

    char extra[1024];
    snprintf(extra,
             sizeof(extra),
             "\"usb\":\"%s\",\"ble\":\"%s\",\"ble_auto\":\"%s\",\"ble_target\":\"%s\","
             "\"rate_hz\":%u,\"report_sent\":%lu,\"report_failed\":%lu,\"report_actual_hz\":%lu,"
             "\"live\":\"%s\",\"live_updates\":%lu,\"live_age_ms\":%lld,\"ble_input_hz\":%lu,"
             "\"rumble\":\"%s\",\"version\":\"%s\"",
             usb_hid_device_state_string(),
             ble_central_state_string(),
             device_config_get_ble_autoconnect() ? "on" : "off",
             device_config_get_ble_target()[0] ? device_config_get_ble_target() : "<scan>",
             (unsigned)device_config_get_report_rate_hz(),
             (unsigned long)report_stats.sent_total,
             (unsigned long)report_stats.failed_total,
             (unsigned long)report_stats.actual_millihz,
             live_valid ? "yes" : "no",
             (unsigned long)live_updates,
             (long long)(live_age_us / 1000),
             (unsigned long)live_stats.actual_millihz,
             usb_switch2_vendor_hd_rumble_active() ? "on" : "off",
             device_config_get_version());
    json_ok(reply, reply_len, "status", extra);
}

esp_err_t control_protocol_handle_line(const char *line, char *reply, int reply_len)
{
    if (!line || !reply || reply_len <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    reply[0] = 0;

    char cmd[128];
    if (!copy_trimmed_arg(line, cmd, sizeof(cmd))) {
        return ESP_OK;
    }

    if (strcmp(cmd, "help") == 0) {
        json_ok(reply, reply_len, "help",
                "\"commands\":\"status,ble scan,ble connect <addr|name>,ble reconnect,ble disconnect,ble forget,ble target <addr>,ble auto on|off,rate <hz>,start,stop,rumble,rumble stop,reboot\"");
    } else if (strcmp(cmd, "status") == 0) {
        handle_status(reply, reply_len);
    } else if (strcmp(cmd, "ble scan") == 0) {
        ble_central_start_scan();
        json_ok(reply, reply_len, "ble scan", "\"ble\":\"scanning\"");
    } else if (strcmp(cmd, "ble connect") == 0 || strcmp(cmd, "ble reconnect") == 0) {
        ble_central_reconnect_saved_or_scan();
        json_ok(reply, reply_len, "ble connect", "\"ble\":\"connecting\"");
    } else if (strncmp(cmd, "ble connect ", 12) == 0) {
        char target[96];
        if (!copy_trimmed_arg(cmd + 12, target, sizeof(target))) {
            json_error(reply, reply_len, "ble connect", "missing target");
        } else {
            ble_central_connect(target);
            json_ok(reply, reply_len, "ble connect", "\"ble\":\"connecting\"");
        }
    } else if (strcmp(cmd, "ble disconnect") == 0) {
        ble_central_disconnect();
        json_ok(reply, reply_len, "ble disconnect", "\"ble\":\"idle\"");
    } else if (strcmp(cmd, "ble forget") == 0 || strcmp(cmd, "ble target clear") == 0) {
        ble_central_disconnect();
        device_config_save_ble_target("");
        json_ok(reply, reply_len, "ble forget", "\"ble_target\":\"\"");
    } else if (strncmp(cmd, "ble target ", 11) == 0) {
        char target[96];
        if (!copy_trimmed_arg(cmd + 11, target, sizeof(target))) {
            json_error(reply, reply_len, "ble target", "missing target");
        } else {
            device_config_save_ble_target(target);
            char extra[128];
            snprintf(extra, sizeof(extra), "\"ble_target\":\"%s\"", device_config_get_ble_target());
            json_ok(reply, reply_len, "ble target", extra);
        }
    } else if (strcmp(cmd, "ble auto on") == 0 || strcmp(cmd, "ble autoconnect on") == 0) {
        device_config_save_ble_autoconnect(true);
        ble_central_start_auto_reconnect();
        json_ok(reply, reply_len, "ble auto", "\"ble_auto\":\"on\"");
    } else if (strcmp(cmd, "ble auto off") == 0 || strcmp(cmd, "ble autoconnect off") == 0) {
        device_config_save_ble_autoconnect(false);
        json_ok(reply, reply_len, "ble auto", "\"ble_auto\":\"off\"");
    } else if (strncmp(cmd, "rate ", 5) == 0) {
        char *end = NULL;
        long rate = strtol(cmd + 5, &end, 10);
        if (end == cmd + 5 || *end || rate < 20 || rate > 1000) {
            json_error(reply, reply_len, "rate", "invalid rate (20..1000)");
        } else {
            device_config_save_report_rate_hz((uint16_t)rate);
            char extra[64];
            snprintf(extra, sizeof(extra), "\"rate_hz\":%u", (unsigned)device_config_get_report_rate_hz());
            json_ok(reply, reply_len, "rate", extra);
        }
    } else if (strcmp(cmd, "start") == 0) {
        device_config_set_bridge_running(true);
        json_ok(reply, reply_len, "start", "\"hid\":\"running\"");
    } else if (strcmp(cmd, "stop") == 0) {
        device_config_set_bridge_running(false);
        json_ok(reply, reply_len, "stop", "\"hid\":\"stopped\"");
    } else if (strcmp(cmd, "rumble") == 0) {
        usb_switch2_vendor_start_hd_rumble_self_test();
        json_ok(reply, reply_len, "rumble", "\"rumble\":\"self-test\"");
    } else if (strcmp(cmd, "rumble stop") == 0) {
        usb_switch2_vendor_stop_hd_rumble();
        json_ok(reply, reply_len, "rumble stop", "\"rumble\":\"stopped\"");
    } else if (strcmp(cmd, "reboot") == 0) {
        json_ok(reply, reply_len, "reboot", "\"reboot\":true");
        esp_restart();
    } else {
        json_error(reply, reply_len, cmd, "unknown command");
    }

    return ESP_OK;
}
