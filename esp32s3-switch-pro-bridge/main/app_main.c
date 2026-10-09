#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "app_log.h"
#include "ble_central.h"
#include "control_protocol.h"
#include "device_config.h"
#include "hid_report.h"
#include "internal_gamepad_state.h"
#include "report_mapper.h"
#include "report_rate_stats.h"
#include "switch2_state.h"
#include "switch2_gatt.h"
#include "usb_hid_device.h"

static const char *TAG = "app";

#define BLE_LIVE_STALE_US 1000000LL
#define CONTROL_LINE_MAX 192

static uint32_t s_usb_axis_debug_seen;

static uint32_t report_delay_ms(void)
{
    uint16_t rate_hz = device_config_get_report_rate_hz();
    uint32_t delay_ms = (1000u + rate_hz - 1u) / rate_hz;
    return delay_ms == 0 ? 1 : delay_ms;
}

static esp_err_t send_usb_state_report(const switch2_state_t *state)
{
    internal_gamepad_state_t internal;
    switch2_state_to_internal(state, &internal);

    uint8_t report[SWITCH_LEGACY_REPORT_SIZE];
    report_mapper_internal_to_switch_legacy_report(&internal, report);

    uint32_t every = 32;
    if (switch2_gatt_get_axis_debug(&every)) {
        s_usb_axis_debug_seen++;
        if (every <= 1 || (s_usb_axis_debug_seen % every) == 0) {
            APP_LOGI(TAG,
                     "USB_AXIS_DEBUG sample=%lu state=[%u,%u,%u,%u] report=[%02x %02x %02x %02x %02x %02x]",
                     (unsigned long)s_usb_axis_debug_seen,
                     (unsigned)state->lx,
                     (unsigned)state->ly,
                     (unsigned)state->rx,
                     (unsigned)state->ry,
                     report[6], report[7], report[8], report[9], report[10], report[11]);
        }
    }
    return usb_hid_device_send_switch_pro_report(report);
}

static void control_task(void *arg)
{
    (void)arg;
    char line[CONTROL_LINE_MAX];
    uint8_t rx[64];
    size_t line_len = 0;
    bool overflow = false;
    static char reply[16384];

    while (true) {
        int rx_len = read(STDIN_FILENO, rx, sizeof(rx));
        if (rx_len <= 0) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        for (int i = 0; i < rx_len; i++) {
            uint8_t ch = rx[i];
            if (ch == '\r' || ch == '\n') {
                if (overflow) {
                    APP_LOGW(TAG, "serial control line too long or contains NUL; discarded");
                    printf("{\"ok\":false,\"cmd\":\"serial\",\"error\":\"invalid command line\"}\n");
                    overflow = false;
                    line_len = 0;
                    continue;
                }
                if (line_len > 0) {
                    line[line_len] = 0;
                    control_protocol_handle_line(line, reply, sizeof(reply));
                    if (reply[0]) {
                        printf("%s\n", reply);
                        fflush(stdout);
                    }
                    line_len = 0;
                }
                continue;
            }
            if (overflow) {
                continue;
            }
            if (ch == 0 || line_len + 1 >= sizeof(line)) {
                overflow = true;
                line_len = 0;
                continue;
            }
            line[line_len++] = (char)ch;
        }
    }
}

static void hid_report_task(void *arg)
{
    (void)arg;
    int64_t next_log_us = 0;
    uint32_t loop_count = 0;
    uint32_t not_ready_count = 0;

    while (true) {
        int64_t now_us = esp_timer_get_time();
        uint32_t delay_ms = report_delay_ms();
        switch2_state_t state;
        uint32_t live_updates = 0;
        int64_t live_age_us = 0;

        loop_count++;
        bool bridge_running = device_config_bridge_running();
        bool live_valid = switch2_state_get_live(&state, &live_updates, &live_age_us);
        bool live_fresh = live_valid && live_age_us <= BLE_LIVE_STALE_US;
        if (!bridge_running || !live_fresh) {
            switch2_state_reset(&state);
        }

        /* One task owns IN submission; queued host replies always go first. */
        bool servicing_reply = usb_hid_device_service_replies();
        bool usb_ready = usb_hid_device_ready();
        if (!servicing_reply && usb_ready) {
            (void)send_usb_state_report(&state);
        } else if (!usb_ready) {
            not_ready_count++;
        }

        /* Print even without enumeration or endpoint readiness. Completion
         * counters confirm host USB transfers, rather than just attempted sends. */
        if (now_us >= next_log_us &&
            (app_log_debug_enabled() || switch2_gatt_get_axis_debug(NULL))) {
            usb_hid_diagnostics_t usb;
            usb_hid_device_get_diagnostics(&usb);
            APP_LOGI(TAG,
                     "USB_DIAG_V2 mounted=%u suspended=%u ready=%u enabled=%u bridge=%u live=%u age_ms=%lld loops=%lu not_ready=%lu "
                     "axes=[%u,%u,%u,%u] tx=%lu done=%lu fail=%lu blocked=%lu "
                     "reply=[q:%lu tx:%lu done:%lu fail:%lu pending:%u full:%lu] host_out=%lu last_out=0x%02x "
                     "last_done=0x%02x usb_axes=[%u,%u,%u,%u]",
                     (unsigned)usb.mounted, (unsigned)usb.suspended,
                     (unsigned)usb.endpoint_ready, (unsigned)usb.input_enabled,
                     (unsigned)bridge_running, (unsigned)live_fresh,
                     live_valid ? (long long)(live_age_us / 1000) : -1LL,
                     (unsigned long)loop_count, (unsigned long)not_ready_count,
                     (unsigned)state.lx, (unsigned)state.ly, (unsigned)state.rx, (unsigned)state.ry,
                     (unsigned long)usb.input_submitted, (unsigned long)usb.input_completed,
                     (unsigned long)usb.input_failed, (unsigned long)usb.input_blocked,
                     (unsigned long)usb.replies_queued, (unsigned long)usb.replies_submitted,
                     (unsigned long)usb.replies_completed, (unsigned long)usb.replies_failed,
                     (unsigned)usb.reply_pending,
                     (unsigned long)usb.reply_queue_full,
                     (unsigned long)usb_hid_device_out_count(),
                     usb_hid_device_last_out_effective_report_id(), usb.last_completed_report_id,
                     (unsigned)usb.last_completed_axes[0], (unsigned)usb.last_completed_axes[1],
                     (unsigned)usb.last_completed_axes[2], (unsigned)usb.last_completed_axes[3]);
        }
        if (now_us >= next_log_us) {
            next_log_us = now_us + 1000000LL;
        }

        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

void app_main(void)
{
    app_log_init();
    APP_LOGI(TAG, "ESP32-S3 Switch Pro bridge firmware starting");
    APP_LOGI(TAG, "firmware=%s built=%s %s",
             device_config_get_version(), __DATE__, __TIME__);

    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_err);

    device_config_init();
    switch2_state_init();
    report_rate_stats_init();

    ESP_ERROR_CHECK(usb_hid_device_init());
    control_protocol_init();
    ble_central_init();

    if (xTaskCreate(control_task, "control_task", 6144, NULL, 6, NULL) != pdPASS ||
        xTaskCreate(hid_report_task, "hid_report_task", 4096, NULL, 5, NULL) != pdPASS) {
        APP_LOGE(TAG, "failed to create bridge tasks");
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
}
