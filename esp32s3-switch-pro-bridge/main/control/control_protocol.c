#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_err.h"
#include "esp_system.h"
#include "app_log.h"
#include "ble_central.h"
#include "device_config.h"
#include "report_rate_stats.h"
#include "switch2_state.h"
#include "switch2_gatt.h"
#include "usb_hid_device.h"
#include "usb_switch2_vendor.h"
#include "control_protocol.h"

static bool json_escape(const char *src, char *dst, size_t capacity)
{
    size_t used = 0;
    while (*src) {
        unsigned char ch = (unsigned char)*src++;
        size_t needed = ch < 0x20 ? 6 : (ch == '"' || ch == '\\') ? 2 : 1;
        if (needed >= capacity - used) {
            dst[0] = 0;
            return false;
        }
        if (ch < 0x20) {
            snprintf(dst + used, capacity - used, "\\u%04x", (unsigned)ch);
        } else {
            if (needed == 2) dst[used++] = '\\';
            dst[used] = (char)ch;
        }
        used += ch < 0x20 ? 6 : 1;
    }
    dst[used] = 0;
    return true;
}

static void json_ok(char *reply, int reply_len, const char *cmd, const char *extra)
{
    int count = snprintf(reply, (size_t)reply_len, "{\"ok\":true,\"cmd\":\"%s\",%s}", cmd, extra);
    if (count < 0 || count >= reply_len) reply[0] = 0;
}

static void json_error(char *reply, int reply_len, const char *cmd, const char *error)
{
    /* Escape directly into the caller's response buffer. Avoid large stack
     * scratch arrays in the 4 KiB TinyUSB task executing feature commands. */
    int count = snprintf(reply, (size_t)reply_len, "{\"ok\":false,\"cmd\":\"");
    if (count < 0 || count >= reply_len) goto insufficient;
    size_t used = (size_t)count;
    if (!json_escape(cmd, reply + used, (size_t)reply_len - used)) goto insufficient;
    used += strlen(reply + used);
    count = snprintf(reply + used, (size_t)reply_len - used, "\",\"error\":\"");
    if (count < 0 || (size_t)count >= (size_t)reply_len - used) goto insufficient;
    used += (size_t)count;
    if (!json_escape(error, reply + used, (size_t)reply_len - used)) goto insufficient;
    used += strlen(reply + used);
    if ((size_t)reply_len - used < 3) goto insufficient;
    memcpy(reply + used, "\"}", 3);
    return;
insufficient:
    reply[0] = 0;
}

static esp_err_t response_result(const char *reply)
{
    return reply[0] ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

/* Zero means empty, one means copied, minus one means too long. Never execute
 * the prefix of a command whose trailing characters were silently dropped. */
static int copy_trimmed_arg(const char *src, char *dst, size_t dst_len)
{
    while (*src && isspace((unsigned char)*src)) {
        src++;
    }
    size_t n = strlen(src);
    while (n > 0 && isspace((unsigned char)src[n - 1])) {
        --n;
    }
    dst[0] = 0;
    if (n >= dst_len) return -1;
    memcpy(dst, src, n);
    dst[n] = 0;
    return n > 0 ? 1 : 0;
}

static bool parse_interval(const char *src, uint32_t *out)
{
    while (isspace((unsigned char)*src)) src++;
    if (!isdigit((unsigned char)*src)) return false;
    errno = 0;
    char *end;
    unsigned long parsed = strtoul(src, &end, 10);
    if (errno == ERANGE || end == src || *end || parsed == 0 || parsed > UINT32_MAX) {
        return false;
    }
    *out = (uint32_t)parsed;
    return true;
}

static bool command_succeeded(esp_err_t err, char *reply, int reply_len, const char *cmd)
{
    if (err == ESP_OK) return true;
    json_error(reply, reply_len, cmd, esp_err_to_name(err));
    return false;
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
    char target[40], escaped_target[6 * sizeof(target)];
    device_config_copy_ble_target(target, sizeof(target));
    json_escape(target[0] ? target : "<scan>", escaped_target, sizeof(escaped_target));

    char extra[1024];
    snprintf(extra,
             sizeof(extra),
             "\"usb\":\"%s\",\"ble\":\"%s\",\"ble_auto\":\"%s\",\"ble_target\":\"%s\","
             "\"rate_hz\":%u,\"report_sent\":%lu,\"report_failed\":%lu,\"report_actual_hz\":%lu.%03lu,"
             "\"live\":\"%s\",\"live_updates\":%lu,\"live_age_ms\":%lld,\"ble_input_hz\":%lu.%03lu,"
             "\"rumble\":\"%s\",\"version\":\"%s\"",
             usb_hid_device_state_string(),
             ble_central_state_string(),
             device_config_get_ble_autoconnect() ? "on" : "off",
             escaped_target,
             (unsigned)device_config_get_report_rate_hz(),
             (unsigned long)report_stats.sent_total,
             (unsigned long)report_stats.failed_total,
             (unsigned long)(report_stats.actual_millihz / 1000),
             (unsigned long)(report_stats.actual_millihz % 1000),
             live_valid ? "yes" : "no",
             (unsigned long)live_updates,
             live_valid ? (long long)(live_age_us / 1000) : -1LL,
             (unsigned long)(live_stats.actual_millihz / 1000),
             (unsigned long)(live_stats.actual_millihz % 1000),
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
    int copied = copy_trimmed_arg(line, cmd, sizeof(cmd));
    if (copied < 0) {
        json_error(reply, reply_len, "command", "command too long");
        return response_result(reply);
    }
    if (!copied) {
        return ESP_OK;
    }

    if (strcmp(cmd, "help") == 0) {
        json_ok(reply, reply_len, "help",
                "\"commands\":\"status,debug on|off,axis debug on|off [every],raw debug on|off [every],ble scan,ble connect <addr|name>,ble reconnect,ble disconnect,ble forget,ble target <addr>,ble auto on|off,rate <hz>,start,stop,rumble,rumble stop,reboot\"");
    } else if (strcmp(cmd, "status") == 0) {
        handle_status(reply, reply_len);
    } else if (strcmp(cmd, "debug on") == 0) {
        app_log_set_debug(true);
        json_ok(reply, reply_len, "debug", "\"debug\":\"on\"");
    } else if (strcmp(cmd, "debug off") == 0) {
        app_log_set_debug(false);
        json_ok(reply, reply_len, "debug", "\"debug\":\"off\"");
    } else if (strcmp(cmd, "axis debug off") == 0) {
        switch2_gatt_set_axis_debug(false, 32);
        json_ok(reply, reply_len, "axis debug", "\"axis_debug\":\"off\"");
    } else if (strncmp(cmd, "axis debug on", 13) == 0 &&
               (cmd[13] == 0 || isspace((unsigned char)cmd[13]))) {
        uint32_t every = 32;
        if (cmd[13] != 0) {
            if (!parse_interval(cmd + 14, &every)) {
                json_error(reply, reply_len, "axis debug", "invalid interval");
                return response_result(reply);
            }
        }
        switch2_gatt_set_axis_debug(true, every);
        char extra[64];
        snprintf(extra, sizeof(extra), "\"axis_debug\":\"on\",\"every\":%lu", (unsigned long)every);
        json_ok(reply, reply_len, "axis debug", extra);
    } else if (strcmp(cmd, "raw debug off") == 0) {
        ble_central_set_imu_debug(false, 32);
        json_ok(reply, reply_len, "raw debug", "\"raw_debug\":\"off\"");
    } else if (strncmp(cmd, "raw debug on", 12) == 0 &&
               (cmd[12] == 0 || isspace((unsigned char)cmd[12]))) {
        uint32_t every = 32;
        if (cmd[12] != 0) {
            if (!parse_interval(cmd + 13, &every)) {
                json_error(reply, reply_len, "raw debug", "invalid interval");
                return response_result(reply);
            }
        }
        ble_central_set_imu_debug(true, every);
        char extra[64];
        snprintf(extra, sizeof(extra), "\"raw_debug\":\"on\",\"every\":%lu", (unsigned long)every);
        json_ok(reply, reply_len, "raw debug", extra);
    } else if (strcmp(cmd, "ble scan") == 0) {
        if (command_succeeded(ble_central_start_scan(), reply, reply_len, "ble scan")) {
            json_ok(reply, reply_len, "ble scan", "\"ble\":\"scanning\"");
        }
    } else if (strcmp(cmd, "ble connect") == 0 || strcmp(cmd, "ble reconnect") == 0) {
        if (command_succeeded(ble_central_reconnect_saved_or_scan(), reply, reply_len, "ble connect")) {
            json_ok(reply, reply_len, "ble connect", "\"ble\":\"connecting\"");
        }
    } else if (strncmp(cmd, "ble connect ", 12) == 0) {
        char target[96];
        int target_copied = copy_trimmed_arg(cmd + 12, target, sizeof(target));
        if (target_copied <= 0) {
            json_error(reply, reply_len, "ble connect", target_copied < 0 ? "target too long" : "missing target");
        } else if (command_succeeded(ble_central_connect(target), reply, reply_len, "ble connect")) {
            json_ok(reply, reply_len, "ble connect", "\"ble\":\"connecting\"");
        }
    } else if (strcmp(cmd, "ble disconnect") == 0) {
        ble_central_disconnect();
        json_ok(reply, reply_len, "ble disconnect", "\"ble\":\"idle\"");
    } else if (strcmp(cmd, "ble forget") == 0 || strcmp(cmd, "ble target clear") == 0) {
        ble_central_disconnect();
        if (command_succeeded(device_config_save_ble_target(""), reply, reply_len, "ble forget")) {
            json_ok(reply, reply_len, "ble forget", "\"ble_target\":\"\"");
        }
    } else if (strncmp(cmd, "ble target ", 11) == 0) {
        char target[96];
        int target_copied = copy_trimmed_arg(cmd + 11, target, sizeof(target));
        if (target_copied <= 0) {
            json_error(reply, reply_len, "ble target", target_copied < 0 ? "target too long" : "missing target");
        } else if (command_succeeded(device_config_save_ble_target(target), reply, reply_len, "ble target")) {
            char saved[40], escaped[6 * sizeof(saved)], extra[6 * sizeof(saved) + 32];
            device_config_copy_ble_target(saved, sizeof(saved));
            json_escape(saved, escaped, sizeof(escaped));
            snprintf(extra, sizeof(extra), "\"ble_target\":\"%s\"", escaped);
            json_ok(reply, reply_len, "ble target", extra);
        }
    } else if (strcmp(cmd, "ble auto on") == 0 || strcmp(cmd, "ble autoconnect on") == 0) {
        if (command_succeeded(device_config_save_ble_autoconnect(true), reply, reply_len, "ble auto")) {
            ble_central_start_auto_reconnect();
            json_ok(reply, reply_len, "ble auto", "\"ble_auto\":\"on\"");
        }
    } else if (strcmp(cmd, "ble auto off") == 0 || strcmp(cmd, "ble autoconnect off") == 0) {
        if (command_succeeded(device_config_save_ble_autoconnect(false), reply, reply_len, "ble auto")) {
            json_ok(reply, reply_len, "ble auto", "\"ble_auto\":\"off\"");
        }
    } else if (strncmp(cmd, "rate ", 5) == 0) {
        char *end = NULL;
        errno = 0;
        long rate = strtol(cmd + 5, &end, 10);
        if (errno == ERANGE || end == cmd + 5 || *end || rate < 20 || rate > 1000) {
            json_error(reply, reply_len, "rate", "invalid rate (20..1000)");
        } else if (command_succeeded(device_config_save_report_rate_hz((uint16_t)rate), reply, reply_len, "rate")) {
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

    return response_result(reply);
}
