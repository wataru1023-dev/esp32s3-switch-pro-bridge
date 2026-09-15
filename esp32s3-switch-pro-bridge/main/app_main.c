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
#include "usb_hid_device.h"

static const char *TAG = "app";

#define BLE_LIVE_STALE_US 1000000LL
#define CONTROL_LINE_MAX 192

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
                    APP_LOGW(TAG, "serial control line too long; discarded");
                    printf("{\"ok\":false,\"cmd\":\"serial\",\"error\":\"command line too long\"}\n");
                    overflow = false;
                    line_len = 0;
                    continue;
                }
                if (line_len > 0) {
                    line[line_len] = 0;
                    control_protocol_handle_line(line, reply, sizeof(reply));
                    line_len = 0;
                }
                continue;
            }
            if (overflow) {
                continue;
            }
            if (line_len + 1 >= sizeof(line)) {
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

    while (true) {
        int64_t now_us = esp_timer_get_time();
        uint32_t delay_ms = report_delay_ms();
        switch2_state_t state;
        uint32_t live_updates = 0;
        int64_t live_age_us = 0;

        if (!device_config_bridge_running()) {
            switch2_state_reset(&state);
            if (usb_hid_device_ready()) {
                (void)send_usb_state_report(&state);
            }
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
            continue;
        }

        bool live_valid = switch2_state_get_live(&state, &live_updates, &live_age_us);
        if (!(live_valid && live_age_us <= BLE_LIVE_STALE_US)) {
            switch2_state_reset(&state);
        }

        if (usb_hid_device_ready()) {
            esp_err_t err = send_usb_state_report(&state);
            if (err == ESP_OK && now_us >= next_log_us) {
                APP_LOGI(TAG, "report loop rate_hz=%u live_updates=%lu live_age_ms=%lld",
                         (unsigned)device_config_get_report_rate_hz(),
                         (unsigned long)live_updates,
                         (long long)(live_age_us / 1000));
                next_log_us = now_us + 1000000LL;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

void app_main(void)
{
    app_log_init();
    APP_LOGI(TAG, "ESP32-S3 Switch Pro bridge firmware starting");

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

    xTaskCreate(control_task, "control_task", 6144, NULL, 6, NULL);
    xTaskCreate(hid_report_task, "hid_report_task", 4096, NULL, 5, NULL);
}
