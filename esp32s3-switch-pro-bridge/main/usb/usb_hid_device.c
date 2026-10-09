#include <string.h>
#include <stdio.h>
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "tinyusb.h"
#include "tusb.h"
#include "app_log.h"
#include "ble_central.h"
#include "control_protocol.h"
#include "report_rate_stats.h"
#include "usb_descriptors.h"
#include "usb_hid_device.h"
#include "usb_switch2_vendor.h"
#include "switch_legacy_protocol.h"

static const char *TAG = "usb";
static bool s_mounted;
static bool s_suspended;
static uint32_t s_hid_out_count;
static uint8_t s_hid_out_last_report_id;
static uint8_t s_hid_out_last_effective_report_id;
static uint8_t s_hid_out_last_type;
static uint16_t s_hid_out_last_len;
static uint8_t s_hid_out_last_first_byte;
static uint32_t s_hid_get_count;
static uint8_t s_hid_get_last_report_id;
static uint8_t s_hid_get_last_type;
static uint16_t s_hid_get_last_req_len;
static uint16_t s_hid_get_last_resp_len;
static char s_feature_reply[3072];
static uint16_t s_feature_reply_len;
static uint16_t s_feature_reply_offset;

#define HID_INSTANCE_GENERIC 0
#define HID_INSTANCE_NINTENDO 0
#define HID_INSTANCE_DUAL_A 0
#define HID_INSTANCE_DUAL_B 1
#define MANAGER_FEATURE_SET_MAGIC "Y7HID1"
#define MANAGER_FEATURE_REPLY_MAGIC "Y7HRS1"
#define MANAGER_FEATURE_MAGIC_LEN 6
#define MANAGER_FEATURE_REPLY_HEADER_LEN 11
#define SWITCH_LEGACY_DEVICE_TYPE_PRO 0x03
#define SWITCH_LEGACY_SUBCMD_REQUEST_DEVICE_INFO 0x02
#define SWITCH_LEGACY_SUBCMD_SET_INPUT_REPORT_MODE 0x03
#define SWITCH_LEGACY_SUBCMD_SPI_FLASH_READ 0x10
#define SWITCH_LEGACY_PROP_STATUS 0x01
#define SWITCH_LEGACY_PROP_HANDSHAKE 0x02
#define SWITCH_LEGACY_PROP_HIGH_SPEED 0x03
#define SWITCH_LEGACY_PROP_FORCE_USB 0x04
#define SWITCH_LEGACY_PROP_CLEAR_USB 0x05
#define SWITCH_LEGACY_PROP_RESET_MCU 0x06
#define SWITCH_LEGACY_REPLY_QUEUE_DEPTH 16
#define SWITCH_LEGACY_SPI_MAX_READ (SWITCH_LEGACY_REPORT_SIZE - 20)

static const uint8_t s_switch_legacy_mac[2][6] = {
    { 0x98, 0xb6, 0xe9, 0x00, 0x00, 0xa1 },
    { 0x98, 0xb6, 0xe9, 0x00, 0x00, 0xb2 },
};
static uint8_t s_switch_legacy_reply_seq[2];
static uint8_t s_switch_legacy_force_usb_count[2];
static bool s_switch_legacy_input_enabled[2];

typedef struct {
    uint32_t session;
    uint8_t instance;
    uint8_t report[SWITCH_LEGACY_REPORT_SIZE];
} switch_legacy_queued_reply_t;

static QueueHandle_t s_reply_queue;
static SemaphoreHandle_t s_state_mutex;
static uint32_t s_session;
/* All USB IN submissions share one owner; callbacks finish its in-flight copy. */
static switch_legacy_queued_reply_t s_pending_reply;
static bool s_pending_reply_valid;
static bool s_inflight;
static bool s_inflight_reply;
static uint8_t s_inflight_report[SWITCH_LEGACY_REPORT_SIZE];
static portMUX_TYPE s_diagnostics_lock = portMUX_INITIALIZER_UNLOCKED;
static usb_hid_diagnostics_t s_diagnostics;

/* Caller holds s_state_mutex. Application in-flight state protects TinyUSB's
 * shared EP buffer until the completion callback has consumed our snapshot. */
static bool usb_hid_device_instance_ready_locked(uint8_t instance)
{
    return s_mounted && !s_suspended && !s_inflight && tud_hid_n_ready(instance);
}

static uint8_t switch_legacy_slot_from_instance(uint8_t instance)
{
    return instance == HID_INSTANCE_DUAL_B ? 1 : 0;
}

static void switch_legacy_reset_session_locked(void)
{
    s_session++;
    s_pending_reply_valid = false;
    s_inflight = false;
    if (s_reply_queue) {
        xQueueReset(s_reply_queue);
    }
    memset(s_switch_legacy_reply_seq, 0, sizeof(s_switch_legacy_reply_seq));
    memset(s_switch_legacy_force_usb_count, 0, sizeof(s_switch_legacy_force_usb_count));
    memset(s_switch_legacy_input_enabled, 1, sizeof(s_switch_legacy_input_enabled));
}

static void switch_legacy_write_le32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xff);
    dst[1] = (uint8_t)((value >> 8) & 0xff);
    dst[2] = (uint8_t)((value >> 16) & 0xff);
    dst[3] = (uint8_t)((value >> 24) & 0xff);
}

static uint32_t switch_legacy_read_le32(const uint8_t *src)
{
    return (uint32_t)src[0] |
           ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2] << 16) |
           ((uint32_t)src[3] << 24);
}

static void switch_legacy_fill_spi_read(uint32_t address, uint8_t requested_len, uint8_t *dst, size_t capacity)
{
    if (!dst || capacity < 5) {
        return;
    }
    memset(dst, 0, capacity);
    switch_legacy_write_le32(dst, address);
    dst[4] = requested_len;

    switch_legacy_spi_read(address, dst + 5, requested_len, capacity - 5);
}

static bool switch_legacy_send_report_locked(uint8_t instance,
                                             const uint8_t report[SWITCH_LEGACY_REPORT_SIZE],
                                             bool reply)
{
    if (!usb_hid_device_instance_ready_locked(instance)) {
        return false;
    }
    memcpy(s_inflight_report, report, sizeof(s_inflight_report));
    s_inflight_reply = reply;
    s_inflight = true;
    bool submitted = tud_hid_n_report(instance, report[0], report + 1, SWITCH_LEGACY_REPORT_SIZE - 1);
    if (!submitted) {
        s_inflight = false;
    }
    return submitted;
}

static bool switch_legacy_queue_reply(uint8_t instance, const uint8_t report[SWITCH_LEGACY_REPORT_SIZE])
{
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    switch_legacy_queued_reply_t reply = {.session = s_session, .instance = instance};
    memcpy(reply.report, report, sizeof(reply.report));
    bool queued = xQueueSend(s_reply_queue, &reply, 0) == pdTRUE;
    xSemaphoreGive(s_state_mutex);
    portENTER_CRITICAL(&s_diagnostics_lock);
    if (queued) {
        s_diagnostics.replies_queued++;
    } else {
        s_diagnostics.reply_queue_full++;
    }
    portEXIT_CRITICAL(&s_diagnostics_lock);
    if (!queued) {
        APP_LOGE(TAG, "Switch reply queue full; report=0x%02x", report[0]);
    }
    return queued;
}

static void switch_legacy_make_subcommand_reply(uint8_t instance,
                                                uint8_t subcommand,
                                                const uint8_t *subcommand_data,
                                                uint16_t subcommand_len,
                                                uint8_t report[SWITCH_LEGACY_REPORT_SIZE])
{
    uint8_t slot = switch_legacy_slot_from_instance(instance);
    hid_report_make_switch_legacy_neutral(report);
    report[0] = SWITCH_LEGACY_REPORT_ID_SUBCOMMAND_REPLY;
    report[1] = s_switch_legacy_reply_seq[slot]++;
    report[13] = switch_legacy_subcommand_ack(subcommand);
    report[14] = subcommand;

    if (subcommand == SWITCH_LEGACY_SUBCMD_SPI_FLASH_READ &&
        (!subcommand_data || subcommand_len < 5 ||
         subcommand_data[4] > SWITCH_LEGACY_SPI_MAX_READ)) {
        /* A report has room for 44 data bytes. Never advertise bytes that
         * cannot fit, or ACK a read request lacking its address/length. */
        report[13] = 0x00;
        return;
    }

    if (subcommand == SWITCH_LEGACY_SUBCMD_REQUEST_DEVICE_INFO) {
        report[15] = 0x04;
        report[16] = 0x33;
        report[17] = SWITCH_LEGACY_DEVICE_TYPE_PRO;
        report[18] = 0x02;
        memcpy(report + 19, s_switch_legacy_mac[slot], 6);
        report[26] = 0x01;
    } else if (subcommand == SWITCH_LEGACY_SUBCMD_SPI_FLASH_READ &&
               subcommand_data && subcommand_len >= 5) {
        uint32_t address = switch_legacy_read_le32(subcommand_data);
        uint8_t requested_len = subcommand_data[4];
        switch_legacy_fill_spi_read(address, requested_len, report + 15, SWITCH_LEGACY_REPORT_SIZE - 15);
        APP_LOGI(TAG, "Switch SPI read addr=0x%08lx len=%u ack=0x%02x",
                 (unsigned long)address, (unsigned)requested_len, report[13]);
    } else if (subcommand_data && subcommand_len > 0) {
        uint16_t copy_len = subcommand_len;
        if (copy_len > SWITCH_LEGACY_REPORT_SIZE - 15) {
            copy_len = SWITCH_LEGACY_REPORT_SIZE - 15;
        }
        memcpy(report + 15, subcommand_data, copy_len);
    }
}

static void switch_legacy_send_subcommand_reply(uint8_t instance,
                                                uint8_t subcommand,
                                                const uint8_t *subcommand_data,
                                                uint16_t subcommand_len)
{
    if (subcommand == SWITCH_LEGACY_SUBCMD_SET_INPUT_REPORT_MODE) {
        uint8_t slot = switch_legacy_slot_from_instance(instance);
        xSemaphoreTake(s_state_mutex, portMAX_DELAY);
        s_switch_legacy_input_enabled[slot] = true;
        xSemaphoreGive(s_state_mutex);
        APP_LOGI(TAG, "Switch legacy input enabled via set-input-report-mode instance=%u slot=%u",
                 (unsigned)instance,
                 (unsigned)slot);
    }

    uint8_t report[SWITCH_LEGACY_REPORT_SIZE];
    switch_legacy_make_subcommand_reply(instance, subcommand, subcommand_data, subcommand_len, report);
    bool ok = switch_legacy_queue_reply(instance, report);
    APP_LOGI(TAG, "Switch legacy subcommand reply instance=%u cmd=0x%02x queued=%s",
             (unsigned)instance,
             subcommand,
             ok ? "yes" : "no");
}

static void switch_legacy_send_proprietary_reply(uint8_t instance, uint8_t command)
{
    uint8_t slot = switch_legacy_slot_from_instance(instance);
    uint8_t report[SWITCH_LEGACY_REPORT_SIZE] = {SWITCH_LEGACY_REPORT_ID_COMMAND_ACK};
    uint8_t *payload = report + 1;
    payload[0] = command;

    if (command == SWITCH_LEGACY_PROP_STATUS) {
        payload[2] = SWITCH_LEGACY_DEVICE_TYPE_PRO;
        for (uint8_t i = 0; i < 6; i++) {
            payload[3 + i] = s_switch_legacy_mac[slot][5 - i];
        }
    }

    bool ok = switch_legacy_queue_reply(instance, report);
    APP_LOGI(TAG, "Switch legacy proprietary reply instance=%u cmd=0x%02x queued=%s",
             (unsigned)instance,
             command,
             ok ? "yes" : "no");
}

static void switch_legacy_handle_output(uint8_t instance, const uint8_t full_report[SWITCH_LEGACY_REPORT_SIZE],
                                        uint16_t full_len)
{
    if (instance != HID_INSTANCE_DUAL_A && instance != HID_INSTANCE_DUAL_B) {
        return;
    }
    if (!full_report || full_len == 0) {
        return;
    }

    uint8_t report_id = full_report[0];
    if (report_id == SWITCH_LEGACY_REPORT_ID_PROPRIETARY && full_len >= 2) {
        uint8_t command = full_report[1];
        uint8_t slot = switch_legacy_slot_from_instance(instance);
        xSemaphoreTake(s_state_mutex, portMAX_DELAY);
        if (command == SWITCH_LEGACY_PROP_STATUS) {
            s_switch_legacy_force_usb_count[slot] = 0;
            s_switch_legacy_input_enabled[slot] = false;
        }
        if (command == SWITCH_LEGACY_PROP_FORCE_USB ||
            command == SWITCH_LEGACY_PROP_CLEAR_USB) {
            if (command == SWITCH_LEGACY_PROP_FORCE_USB) {
                if (s_switch_legacy_force_usb_count[slot] < 255) {
                    s_switch_legacy_force_usb_count[slot]++;
                }
                if (s_switch_legacy_force_usb_count[slot] >= 2 && !s_switch_legacy_input_enabled[slot]) {
                    s_switch_legacy_input_enabled[slot] = true;
                }
            }
            xSemaphoreGive(s_state_mutex);
            APP_LOGI(TAG, "Switch legacy proprietary no-reply instance=%u cmd=0x%02x",
                     (unsigned)instance,
                     command);
            return;
        }
        xSemaphoreGive(s_state_mutex);
        switch_legacy_send_proprietary_reply(instance, command);
        return;
    }

    if (report_id == SWITCH_LEGACY_REPORT_ID_RUMBLE_SUBCOMMAND && full_len >= 11) {
        usb_switch2_vendor_switch_pro_rumble(full_report + 2, 8);
        uint8_t subcommand = full_report[10];
        const uint8_t *subcommand_data = full_report + 11;
        uint16_t subcommand_len = full_len > 11 ? (uint16_t)(full_len - 11) : 0;
        switch_legacy_send_subcommand_reply(instance, subcommand, subcommand_data, subcommand_len);
        return;
    }

    if (report_id == SWITCH_LEGACY_REPORT_ID_RUMBLE) {
        usb_switch2_vendor_switch_pro_rumble(full_report + 2,
                                             full_len > 2 ? (uint16_t)(full_len - 2) : 0);
    }
}

esp_err_t usb_hid_device_init(void)
{
    s_state_mutex = xSemaphoreCreateMutex();
    if (!s_state_mutex) {
        return ESP_ERR_NO_MEM;
    }
    s_reply_queue = xQueueCreate(SWITCH_LEGACY_REPLY_QUEUE_DEPTH, sizeof(switch_legacy_queued_reply_t));
    if (!s_reply_queue) {
        return ESP_ERR_NO_MEM;
    }
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    switch_legacy_reset_session_locked();
    xSemaphoreGive(s_state_mutex);
    APP_LOGI(TAG, "initializing TinyUSB HID (Switch Pro)");
    APP_LOGI(TAG, "USB identity VID=0x%04x PID=0x%04x product=%s",
             usb_descriptors_current_vid(),
             usb_descriptors_current_pid(),
             usb_descriptors_current_product());
    usb_switch2_vendor_init();

    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = usb_descriptors_current_device(),
        .string_descriptor = usb_descriptors_current_strings(),
        .string_descriptor_count = usb_descriptors_current_string_count(),
        .external_phy = false,
        .configuration_descriptor = usb_descriptors_current_configuration(),
    };
    return tinyusb_driver_install(&tusb_cfg);
}

bool usb_hid_device_ready(void)
{
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    bool ready = usb_hid_device_instance_ready_locked(HID_INSTANCE_GENERIC);
    xSemaphoreGive(s_state_mutex);
    return ready;
}

bool usb_hid_device_service_replies(void)
{
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    if (s_pending_reply_valid && s_pending_reply.session != s_session) {
        s_pending_reply_valid = false;
    }
    while (!s_pending_reply_valid && xQueueReceive(s_reply_queue, &s_pending_reply, 0) == pdTRUE) {
        s_pending_reply_valid = s_pending_reply.session == s_session;
    }
    if (!s_pending_reply_valid) {
        bool inflight = s_inflight;
        xSemaphoreGive(s_state_mutex);
        return inflight;
    }
    if (switch_legacy_send_report_locked(s_pending_reply.instance, s_pending_reply.report, true)) {
        portENTER_CRITICAL(&s_diagnostics_lock);
        s_diagnostics.replies_submitted++;
        portEXIT_CRITICAL(&s_diagnostics_lock);
        /* Keep pending until successful completion, so async failures retry. */
    }
    xSemaphoreGive(s_state_mutex);
    return true;
}

void usb_hid_device_get_diagnostics(usb_hid_diagnostics_t *out)
{
    if (!out) {
        return;
    }
    portENTER_CRITICAL(&s_diagnostics_lock);
    *out = s_diagnostics;
    portEXIT_CRITICAL(&s_diagnostics_lock);
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    out->mounted = s_mounted;
    out->suspended = s_suspended;
    out->endpoint_ready = usb_hid_device_instance_ready_locked(HID_INSTANCE_GENERIC);
    out->input_enabled = s_switch_legacy_input_enabled[0];
    out->reply_pending = (uint16_t)uxQueueMessagesWaiting(s_reply_queue) +
                         (s_pending_reply_valid && s_pending_reply.session == s_session ? 1 : 0);
    xSemaphoreGive(s_state_mutex);
}

const char *usb_hid_device_state_string(void)
{
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    const char *state = !s_mounted ? "not_mounted" : s_suspended ? "suspended" : "mounted";
    xSemaphoreGive(s_state_mutex);
    return state;
}

uint32_t usb_hid_device_out_count(void)
{
    return s_hid_out_count;
}

uint8_t usb_hid_device_last_out_report_id(void)
{
    return s_hid_out_last_report_id;
}

uint8_t usb_hid_device_last_out_effective_report_id(void)
{
    return s_hid_out_last_effective_report_id;
}

uint8_t usb_hid_device_last_out_type(void)
{
    return s_hid_out_last_type;
}

uint16_t usb_hid_device_last_out_len(void)
{
    return s_hid_out_last_len;
}

uint8_t usb_hid_device_last_out_first_byte(void)
{
    return s_hid_out_last_first_byte;
}

uint32_t usb_hid_device_get_count(void)
{
    return s_hid_get_count;
}

uint8_t usb_hid_device_last_get_report_id(void)
{
    return s_hid_get_last_report_id;
}

uint8_t usb_hid_device_last_get_type(void)
{
    return s_hid_get_last_type;
}

uint16_t usb_hid_device_last_get_req_len(void)
{
    return s_hid_get_last_req_len;
}

uint16_t usb_hid_device_last_get_resp_len(void)
{
    return s_hid_get_last_resp_len;
}

esp_err_t usb_hid_device_send_switch_pro_report(const uint8_t report[SWITCH_LEGACY_REPORT_SIZE])
{
    if (!report || report[0] != SWITCH_LEGACY_REPORT_ID_FULL_STATE) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    /* Gated input must never be reported as a successful USB submission. */
    if (!s_switch_legacy_input_enabled[0]) {
        portENTER_CRITICAL(&s_diagnostics_lock);
        s_diagnostics.input_blocked++;
        portEXIT_CRITICAL(&s_diagnostics_lock);
        xSemaphoreGive(s_state_mutex);
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (s_pending_reply_valid || uxQueueMessagesWaiting(s_reply_queue) > 0) {
        xSemaphoreGive(s_state_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    if (!usb_hid_device_instance_ready_locked(HID_INSTANCE_NINTENDO)) {
        xSemaphoreGive(s_state_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    bool ok = switch_legacy_send_report_locked(HID_INSTANCE_NINTENDO, report, false);
    portENTER_CRITICAL(&s_diagnostics_lock);
    if (ok) {
        s_diagnostics.input_submitted++;
    } else {
        s_diagnostics.input_failed++;
    }
    portEXIT_CRITICAL(&s_diagnostics_lock);
    xSemaphoreGive(s_state_mutex);
    if (!ok) {
        report_rate_stats_record(false);
    }
    return ok ? ESP_OK : ESP_FAIL;
}

void tud_hid_report_complete_cb(uint8_t instance, const uint8_t *report, uint16_t len)
{
    (void)report;
    (void)len;
    if (instance != HID_INSTANCE_NINTENDO) {
        return;
    }
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    if (!s_inflight) {
        xSemaphoreGive(s_state_mutex);
        return;
    }
    bool input = !s_inflight_reply;
    portENTER_CRITICAL(&s_diagnostics_lock);
    s_diagnostics.last_completed_report_id = s_inflight_report[0];
    if (input) {
        s_diagnostics.input_completed++;
        for (uint8_t stick = 0; stick < 2; stick++) {
            const uint8_t *axes = s_inflight_report + 6 + stick * 3;
            s_diagnostics.last_completed_axes[stick * 2] = axes[0] | ((axes[1] & 0x0f) << 8);
            s_diagnostics.last_completed_axes[stick * 2 + 1] = (axes[1] >> 4) | (axes[2] << 4);
        }
    } else {
        s_diagnostics.replies_completed++;
    }
    portEXIT_CRITICAL(&s_diagnostics_lock);
    if (s_inflight_reply) {
        s_pending_reply_valid = false;
    }
    s_inflight = false;
    xSemaphoreGive(s_state_mutex);
    if (input) {
        report_rate_stats_record(true);
    }
}

void tud_hid_report_failed_cb(uint8_t instance, hid_report_type_t report_type,
                              const uint8_t *report, uint16_t xferred_bytes)
{
    (void)report;
    (void)xferred_bytes;
    if (instance == HID_INSTANCE_NINTENDO && report_type == HID_REPORT_TYPE_INPUT) {
        xSemaphoreTake(s_state_mutex, portMAX_DELAY);
        bool input = s_inflight && !s_inflight_reply;
        bool reply = s_inflight && s_inflight_reply;
        /* Replies remain pending for the input task to retry. */
        s_inflight = false;
        portENTER_CRITICAL(&s_diagnostics_lock);
        if (input) {
            s_diagnostics.input_failed++;
        } else if (reply) {
            s_diagnostics.replies_failed++;
        }
        portEXIT_CRITICAL(&s_diagnostics_lock);
        xSemaphoreGive(s_state_mutex);
        if (input) {
            report_rate_stats_record(false);
        }
    }
}

static bool manager_feature_set_command(uint8_t const *payload, uint16_t payload_size)
{
    return payload && payload_size > MANAGER_FEATURE_MAGIC_LEN &&
           memcmp(payload, MANAGER_FEATURE_SET_MAGIC, MANAGER_FEATURE_MAGIC_LEN) == 0;
}

static void manager_feature_handle_set(uint8_t const *payload, uint16_t payload_size)
{
    char command[128];
    size_t command_len = payload_size > MANAGER_FEATURE_MAGIC_LEN ?
        payload_size - MANAGER_FEATURE_MAGIC_LEN : 0;

    while (command_len > 0 &&
           (payload[MANAGER_FEATURE_MAGIC_LEN + command_len - 1] == 0 ||
            payload[MANAGER_FEATURE_MAGIC_LEN + command_len - 1] == '\r' ||
            payload[MANAGER_FEATURE_MAGIC_LEN + command_len - 1] == '\n')) {
        command_len--;
    }
    if (command_len >= sizeof(command) ||
        memchr(payload + MANAGER_FEATURE_MAGIC_LEN, 0, command_len)) {
        snprintf(s_feature_reply, sizeof(s_feature_reply),
                 "{\"ok\":false,\"cmd\":\"command\",\"error\":\"invalid command framing\"}");
        s_feature_reply_len = (uint16_t)strlen(s_feature_reply);
        s_feature_reply_offset = 0;
        return;
    }

    memcpy(command, payload + MANAGER_FEATURE_MAGIC_LEN, command_len);
    command[command_len] = 0;

    control_protocol_handle_line(command, s_feature_reply, sizeof(s_feature_reply));
    s_feature_reply_len = (uint16_t)strlen(s_feature_reply);
    s_feature_reply_offset = 0;
    APP_LOGI(TAG, "manager HID feature command handled cmd=%s reply_len=%u",
             command,
             (unsigned)s_feature_reply_len);
}

static uint16_t manager_feature_get_chunk(uint8_t *buffer, uint16_t reqlen)
{
    if (!buffer || reqlen == 0) {
        return 0;
    }

    memset(buffer, 0, reqlen);
    if (reqlen < MANAGER_FEATURE_REPLY_HEADER_LEN) {
        return reqlen;
    }

    uint16_t offset = s_feature_reply_offset;
    uint16_t remaining = offset < s_feature_reply_len ? (uint16_t)(s_feature_reply_len - offset) : 0;
    uint16_t chunk = (uint16_t)(reqlen - MANAGER_FEATURE_REPLY_HEADER_LEN);
    if (chunk > remaining) {
        chunk = remaining;
    }

    memcpy(buffer, MANAGER_FEATURE_REPLY_MAGIC, MANAGER_FEATURE_MAGIC_LEN);
    buffer[6] = (uint8_t)(s_feature_reply_len & 0xff);
    buffer[7] = (uint8_t)((s_feature_reply_len >> 8) & 0xff);
    buffer[8] = (uint8_t)(offset & 0xff);
    buffer[9] = (uint8_t)((offset >> 8) & 0xff);
    buffer[10] = (uint8_t)chunk;
    if (chunk > 0) {
        memcpy(buffer + MANAGER_FEATURE_REPLY_HEADER_LEN, s_feature_reply + offset, chunk);
        s_feature_reply_offset = (uint16_t)(offset + chunk);
    }

    return reqlen;
}

void tud_mount_cb(void)
{
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    s_mounted = false;
    s_suspended = false;
    switch_legacy_reset_session_locked();
    s_feature_reply_len = 0;
    s_feature_reply_offset = 0;
    s_mounted = true;
    xSemaphoreGive(s_state_mutex);
    usb_switch2_vendor_reset_hid_guard();
    APP_LOGI(TAG, "USB SET_CONFIGURATION complete; mounted");
}

void tud_umount_cb(void)
{
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    s_mounted = false;
    s_suspended = false;
    switch_legacy_reset_session_locked();
    s_feature_reply_len = 0;
    s_feature_reply_offset = 0;
    xSemaphoreGive(s_state_mutex);
    usb_switch2_vendor_reset_hid_guard();
    APP_LOGI(TAG, "USB unmounted");
}

void tud_suspend_cb(bool remote_wakeup_en)
{
    (void)remote_wakeup_en;
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    s_suspended = true;
    xSemaphoreGive(s_state_mutex);
    APP_LOGI(TAG, "USB suspended");
}

void tud_resume_cb(void)
{
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    s_suspended = false;
    xSemaphoreGive(s_state_mutex);
    APP_LOGI(TAG, "USB resumed");
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen)
{
    (void)instance;

    s_hid_get_count++;
    s_hid_get_last_report_id = report_id;
    s_hid_get_last_type = (uint8_t)report_type;
    s_hid_get_last_req_len = reqlen;

    if (!buffer || reqlen == 0) {
        s_hid_get_last_resp_len = 0;
        return 0;
    }

    uint16_t resp_len = 0;
    if (report_type == HID_REPORT_TYPE_FEATURE &&
        report_id == MANAGER_FEATURE_REPORT_ID) {
        resp_len = manager_feature_get_chunk(buffer, reqlen);
    } else if (report_type == HID_REPORT_TYPE_INPUT &&
               (report_id == 0 ||
                report_id == SWITCH_LEGACY_REPORT_ID_FULL_STATE ||
                report_id == SWITCH_LEGACY_REPORT_ID_FULL_STATE_MCU ||
                report_id == SWITCH_LEGACY_REPORT_ID_SIMPLE_STATE ||
                report_id == SWITCH_LEGACY_REPORT_ID_SUBCOMMAND_REPLY ||
                report_id == SWITCH_LEGACY_REPORT_ID_COMMAND_ACK)) {
        uint8_t report[SWITCH_LEGACY_REPORT_SIZE];
        hid_report_make_switch_legacy_neutral(report);
        if (report_id == SWITCH_LEGACY_REPORT_ID_FULL_STATE_MCU ||
            report_id == SWITCH_LEGACY_REPORT_ID_SIMPLE_STATE ||
            report_id == SWITCH_LEGACY_REPORT_ID_SUBCOMMAND_REPLY ||
            report_id == SWITCH_LEGACY_REPORT_ID_COMMAND_ACK) {
            report[0] = report_id;
        }

        if (report_id == 0) {
            resp_len = SWITCH_LEGACY_REPORT_SIZE;
            if (resp_len > reqlen) {
                resp_len = reqlen;
            }
            memcpy(buffer, report, resp_len);
        } else {
            resp_len = (uint16_t)(SWITCH_LEGACY_REPORT_SIZE - 1);
            if (resp_len > reqlen) {
                resp_len = reqlen;
            }
            memcpy(buffer, report + 1, resp_len);
        }
    } else {
        resp_len = reqlen;
        memset(buffer, 0, resp_len);
    }

    s_hid_get_last_resp_len = resp_len;
    APP_LOGI(TAG, "HID GET instance=%u report_id=0x%02x type=%d req=%u resp=%u",
             (unsigned)instance,
             report_id,
             report_type,
             (unsigned)reqlen,
             (unsigned)resp_len);
    return resp_len;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize)
{
    if (instance != HID_INSTANCE_NINTENDO || !buffer || bufsize == 0) {
        return;
    }
    uint8_t effective_report_id = report_id;
    uint8_t const *payload = buffer;
    uint16_t payload_size = bufsize;

    if (effective_report_id == 0 && buffer && bufsize > 0) {
        effective_report_id = buffer[0];
        payload = buffer + 1;
        payload_size = (uint16_t)(bufsize - 1);
    }

    s_hid_out_count++;
    s_hid_out_last_report_id = report_id;
    s_hid_out_last_effective_report_id = effective_report_id;
    s_hid_out_last_type = (uint8_t)report_type;
    s_hid_out_last_len = bufsize;
    s_hid_out_last_first_byte = buffer && bufsize > 0 ? buffer[0] : 0;

    bool quiet_output = (effective_report_id == SWITCH_LEGACY_REPORT_ID_RUMBLE) &&
                        !app_log_debug_enabled();
    if (quiet_output) {
        APP_LOGD(TAG, "HID OUT instance=%u report_id=0x%02x effective=0x%02x type=%d size=%u",
                 (unsigned)instance,
                 report_id,
                 effective_report_id,
                 report_type,
                 (unsigned)bufsize);
    } else {
        APP_LOGI(TAG, "HID OUT instance=%u report_id=0x%02x effective=0x%02x type=%d size=%u",
                 (unsigned)instance,
                 report_id,
                 effective_report_id,
                 report_type,
                 (unsigned)bufsize);
    }
    if (report_type == HID_REPORT_TYPE_FEATURE &&
        effective_report_id == MANAGER_FEATURE_REPORT_ID &&
        manager_feature_set_command(payload, payload_size)) {
        manager_feature_handle_set(payload, payload_size);
        return;
    }
    if (report_type != HID_REPORT_TYPE_OUTPUT || payload_size == 0) {
        return;
    }

    uint8_t full_report[SWITCH_LEGACY_REPORT_SIZE];
    full_report[0] = effective_report_id;
    uint16_t copy_len = payload_size > (SWITCH_LEGACY_REPORT_SIZE - 1) ?
        (SWITCH_LEGACY_REPORT_SIZE - 1) : payload_size;
    memcpy(full_report + 1, payload, copy_len);
    if (copy_len < SWITCH_LEGACY_REPORT_SIZE - 1) {
        memset(full_report + 1 + copy_len, 0, (SWITCH_LEGACY_REPORT_SIZE - 1) - copy_len);
    }
    switch_legacy_handle_output(instance, full_report, (uint16_t)(copy_len + 1));
}
