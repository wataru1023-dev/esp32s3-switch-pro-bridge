/* Host regression for the ACTUAL USB transport and protocol sources.
 * The stubs replace only ESP-IDF/TinyUSB services; OUT decoding, reply queues,
 * SPI payloads, input gates and diagnostics all run in usb_hid_device.c. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "tinyusb.h"
#include "usb_hid_device.h"

void tud_mount_cb(void);
void tud_umount_cb(void);
void tud_suspend_cb(bool remote_wakeup_en);
void tud_resume_cb(void);
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t type, const uint8_t *buffer,
                           uint16_t length);
void tud_hid_report_complete_cb(uint8_t instance, const uint8_t *report,
                                uint16_t length);
void tud_hid_report_failed_cb(uint8_t instance, hid_report_type_t type,
                              const uint8_t *report, uint16_t length);
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t id,
                               hid_report_type_t type, uint8_t *buffer,
                               uint16_t length);

struct test_mutex { bool held; };

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    SemaphoreHandle_t mutex = calloc(1, sizeof(*mutex));
    assert(mutex);
    return mutex;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t wait)
{
    (void)wait;
    assert(mutex && !mutex->held); /* Catch accidental recursive acquisition. */
    mutex->held = true;
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t mutex)
{
    assert(mutex && mutex->held);
    mutex->held = false;
    return pdTRUE;
}

struct test_queue {
    size_t capacity, item_size, count, head;
    uint8_t *items;
};

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size)
{
    QueueHandle_t queue = calloc(1, sizeof(*queue));
    assert(queue);
    queue->capacity = length;
    queue->item_size = item_size;
    queue->items = calloc(length, item_size);
    assert(queue->items);
    return queue;
}

BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t wait)
{
    (void)wait;
    assert(queue && item);
    if (queue->count == queue->capacity) return pdFALSE;
    size_t tail = (queue->head + queue->count) % queue->capacity;
    memcpy(queue->items + tail * queue->item_size, item, queue->item_size);
    queue->count++;
    return pdTRUE;
}

BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t wait)
{
    (void)wait;
    assert(queue && item);
    if (!queue->count) return pdFALSE;
    memcpy(item, queue->items + queue->head * queue->item_size, queue->item_size);
    queue->head = (queue->head + 1) % queue->capacity;
    queue->count--;
    return pdTRUE;
}

BaseType_t xQueueReset(QueueHandle_t queue)
{
    assert(queue);
    queue->head = queue->count = 0;
    return pdTRUE;
}

UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue)
{
    assert(queue);
    return (UBaseType_t)queue->count;
}

typedef struct {
    uint8_t instance;
    uint8_t report[SWITCH_LEGACY_REPORT_SIZE];
} transfer_t;

static transfer_t s_transfers[128];
static size_t s_transfer_count;
static bool s_endpoint_ready[2] = {true, true};
static bool s_reject_next_submission;
static unsigned s_rate_success, s_rate_failure;
static unsigned s_control_commands;
static unsigned s_rumble_reports;
static uint8_t s_last_rumble[8];

bool tud_hid_n_ready(uint8_t instance)
{
    assert(instance < 2);
    return s_endpoint_ready[instance];
}

bool tud_hid_n_report(uint8_t instance, uint8_t id, const void *data, uint16_t length)
{
    assert(instance < 2 && length == SWITCH_LEGACY_REPORT_SIZE - 1);
    assert(s_endpoint_ready[instance]);
    if (s_reject_next_submission) {
        s_reject_next_submission = false;
        return false;
    }
    assert(s_transfer_count < sizeof(s_transfers) / sizeof(s_transfers[0]));
    transfer_t *tx = &s_transfers[s_transfer_count++];
    tx->instance = instance;
    tx->report[0] = id;
    memcpy(tx->report + 1, data, length);
    s_endpoint_ready[instance] = false;
    return true;
}

esp_err_t tinyusb_driver_install(const tinyusb_config_t *config)
{
    assert(config && !config->external_phy);
    return ESP_OK;
}

bool app_log_debug_enabled(void) { return false; }
void usb_switch2_vendor_init(void) {}
void usb_switch2_vendor_reset_hid_guard(void) {}
void usb_switch2_vendor_switch_pro_rumble(const uint8_t *data, uint16_t length)
{
    assert(data && length == 8);
    memcpy(s_last_rumble, data, sizeof(s_last_rumble));
    s_rumble_reports++;
}
void report_rate_stats_record(bool sent)
{ if (sent) s_rate_success++; else s_rate_failure++; }
esp_err_t control_protocol_handle_line(const char *line, char *reply, int length)
{
    assert(line);
    s_control_commands++;
    if (length > 0) snprintf(reply, (size_t)length, "{\"ok\":true}");
    return ESP_OK;
}

uint16_t usb_descriptors_current_vid(void) { return 0x057e; }
uint16_t usb_descriptors_current_pid(void) { return 0x2009; }
const char *usb_descriptors_current_product(void) { return "Switch Pro"; }
const tusb_desc_device_t *usb_descriptors_current_device(void)
{ static const tusb_desc_device_t descriptor = {0}; return &descriptor; }
const uint8_t *usb_descriptors_current_configuration(void)
{ static const uint8_t descriptor[] = {0}; return descriptor; }
const char **usb_descriptors_current_strings(void)
{ static const char *strings[] = {""}; return strings; }
int usb_descriptors_current_string_count(void) { return 1; }

static usb_hid_diagnostics_t diagnostics(void)
{
    usb_hid_diagnostics_t d;
    usb_hid_device_get_diagnostics(&d);
    return d;
}

static void fresh_session(void)
{
    tud_umount_cb();
    tud_mount_cb();
    s_endpoint_ready[0] = s_endpoint_ready[1] = true;
    s_reject_next_submission = false;
    assert(!usb_hid_device_service_replies());
    s_transfer_count = 0;
    assert(diagnostics().reply_pending == 0);
}

static void finish_last_transfer(void)
{
    assert(s_transfer_count);
    transfer_t *tx = &s_transfers[s_transfer_count - 1];
    s_endpoint_ready[tx->instance] = true;
    tud_hid_report_complete_cb(tx->instance, tx->report, sizeof(tx->report));
}

static void inject_subcommand(uint8_t command, const uint8_t *data,
                             size_t length, bool interrupt_out)
{
    uint8_t full[SWITCH_LEGACY_REPORT_SIZE] = {0x01};
    assert(length <= sizeof(full) - 11);
    full[10] = command;
    if (length) memcpy(full + 11, data, length);
    if (interrupt_out) {
        /* TinyUSB interrupt OUT includes the report ID in the buffer. */
        tud_hid_set_report_cb(0, 0, HID_REPORT_TYPE_OUTPUT, full, sizeof(full));
    } else {
        /* Control SET_REPORT supplies the report ID separately. */
        tud_hid_set_report_cb(0, full[0], HID_REPORT_TYPE_OUTPUT,
                              full + 1, sizeof(full) - 1);
    }
}

static void inject_spi(uint32_t address, uint8_t length, bool interrupt_out)
{
    uint8_t data[] = {(uint8_t)address, (uint8_t)(address >> 8),
                      (uint8_t)(address >> 16), (uint8_t)(address >> 24), length};
    inject_subcommand(0x10, data, sizeof(data), interrupt_out);
}

static void inject_proprietary(uint8_t command)
{
    uint8_t full[SWITCH_LEGACY_REPORT_SIZE] = {0x80, command};
    tud_hid_set_report_cb(0, 0, HID_REPORT_TYPE_OUTPUT, full, sizeof(full));
}

static uint16_t unpack_x(const uint8_t *data)
{ return data[0] | ((data[1] & 0x0f) << 8); }
static uint16_t unpack_y(const uint8_t *data)
{ return (data[1] >> 4) | (data[2] << 4); }

static void assert_pair(const uint8_t *data, uint16_t x, uint16_t y)
{ assert(unpack_x(data) == x); assert(unpack_y(data) == y); }

static const uint8_t *submit_spi(uint32_t address, uint8_t length, bool interrupt_out)
{
    assert(s_endpoint_ready[0]);
    size_t before = s_transfer_count;
    inject_spi(address, length, interrupt_out);
    assert(s_transfer_count == before); /* OUT callback never races IN task. */
    assert(usb_hid_device_service_replies());
    assert(s_transfer_count == before + 1);
    const uint8_t *reply = s_transfers[before].report;
    assert(reply[0] == 0x21 && reply[13] == 0x90 && reply[14] == 0x10);
    assert(reply[15] == (uint8_t)address && reply[16] == (uint8_t)(address >> 8));
    assert(reply[17] == (uint8_t)(address >> 16) && reply[18] == (uint8_t)(address >> 24));
    assert(reply[19] == length);
    finish_last_transfer();
    return reply + 20;
}

static void test_calibration_commands(void)
{
    fresh_session();
    const uint8_t *left = submit_spi(0x603d, 9, true);
    assert_pair(left, 2047, 2047);
    assert_pair(left + 3, 2048, 2048);
    assert_pair(left + 6, 2048, 2048);
    const uint8_t *right = submit_spi(0x6046, 9, false);
    assert_pair(right, 2048, 2048);
    assert_pair(right + 3, 2048, 2048);
    assert_pair(right + 6, 2047, 2047);

    const uint8_t *imu = submit_spi(0x6020, 24, true);
    assert(imu[6] == 0x00 && imu[7] == 0x40);
    assert(imu[18] == 0x3b && imu[19] == 0x34);
    for (uint32_t address = 0x6086; address <= 0x6098; address += 18) {
        const uint8_t *params = submit_spi(address, 18, false);
        assert_pair(params, 80, 4095);
        assert_pair(params + 3, 64, 0);
    }
    const uint8_t *cross = submit_spi(0x6084, 24, true);
    assert(cross[0] == 0 && cross[1] == 0);
    assert_pair(cross + 2, 80, 4095);
    assert_pair(cross + 20, 80, 4095);
    const uint8_t *user = submit_spi(0x8010, 24, false);
    for (size_t i = 0; i < 24; ++i) assert(user[i] == 0xff);

    inject_subcommand(0x02, NULL, 0, true);
    assert(usb_hid_device_service_replies());
    const uint8_t *info = s_transfers[s_transfer_count - 1].report;
    assert(info[13] == 0x82 && info[14] == 0x02 && info[17] == 0x03);
    finish_last_transfer();
    const uint8_t mode = 0x30;
    inject_subcommand(0x03, &mode, 1, false);
    assert(usb_hid_device_service_replies());
    const uint8_t *ack = s_transfers[s_transfer_count - 1].report;
    assert(ack[13] == 0x80 && ack[14] == 0x03);
    finish_last_transfer();
}

static void test_busy_reply_retry_and_priority(void)
{
    fresh_session();
    uint8_t input[SWITCH_LEGACY_REPORT_SIZE];
    hid_report_make_switch_legacy_neutral(input);
    usb_hid_diagnostics_t before = diagnostics();
    s_endpoint_ready[0] = false;
    inject_spi(0x603d, 18, true);
    inject_spi(0x6086, 18, false);
    assert(usb_hid_device_service_replies());
    assert(s_transfer_count == 0 && diagnostics().reply_pending == 2);
    assert(usb_hid_device_send_switch_pro_report(input) == ESP_ERR_INVALID_STATE);

    s_endpoint_ready[0] = true;
    s_reject_next_submission = true; /* Race: ready queried, submit fails. */
    assert(usb_hid_device_service_replies());
    assert(s_transfer_count == 0 && diagnostics().reply_pending == 2);
    assert(usb_hid_device_send_switch_pro_report(input) == ESP_ERR_INVALID_STATE);
    assert(usb_hid_device_service_replies());
    assert(s_transfer_count == 1 && s_transfers[0].report[15] == 0x3d);
    assert(diagnostics().reply_pending == 2); /* Pending until completion. */
    assert(usb_hid_device_send_switch_pro_report(input) == ESP_ERR_INVALID_STATE);
    finish_last_transfer();
    assert(diagnostics().reply_pending == 1);
    assert(usb_hid_device_send_switch_pro_report(input) == ESP_ERR_INVALID_STATE);
    assert(usb_hid_device_service_replies());
    assert(s_transfer_count == 2 && s_transfers[1].report[15] == 0x86);
    finish_last_transfer();
    assert(!usb_hid_device_service_replies());
    assert(usb_hid_device_send_switch_pro_report(input) == ESP_OK);
    assert(s_transfer_count == 3 && s_transfers[2].report[0] == 0x30);
    finish_last_transfer();
    usb_hid_diagnostics_t after = diagnostics();
    assert(after.replies_submitted == before.replies_submitted + 2);
    assert(after.replies_completed == before.replies_completed + 2);
    assert(after.input_submitted == before.input_submitted + 1);
}

static void test_input_gate_and_submission_accounting(void)
{
    fresh_session();
    uint8_t input[SWITCH_LEGACY_REPORT_SIZE];
    hid_report_make_switch_legacy_neutral(input);
    inject_proprietary(0x01); /* Host STATUS intentionally gates input. */
    assert(usb_hid_device_service_replies());
    finish_last_transfer();
    usb_hid_diagnostics_t before = diagnostics();
    unsigned success_before = s_rate_success;
    assert(!before.input_enabled);
    assert(usb_hid_device_send_switch_pro_report(input) == ESP_ERR_NOT_SUPPORTED);
    usb_hid_diagnostics_t blocked = diagnostics();
    assert(blocked.input_blocked == before.input_blocked + 1);
    assert(blocked.input_submitted == before.input_submitted);
    assert(s_rate_success == success_before);
    assert(s_transfer_count == 1);

    inject_proprietary(0x04);
    assert(!diagnostics().input_enabled);
    inject_proprietary(0x04);
    assert(diagnostics().input_enabled);
    s_reject_next_submission = true;
    unsigned failure_before = s_rate_failure;
    assert(usb_hid_device_send_switch_pro_report(input) == ESP_FAIL);
    assert(s_rate_failure == failure_before + 1);
    assert(diagnostics().input_submitted == before.input_submitted);
    assert(usb_hid_device_send_switch_pro_report(input) == ESP_OK);
    finish_last_transfer();
    assert(s_rate_success == success_before + 1);
}

static void test_session_reset_discards_queued_and_pending_replies(void)
{
    fresh_session();
    s_endpoint_ready[0] = false;
    inject_spi(0x603d, 9, true);
    inject_spi(0x6046, 9, true);
    assert(usb_hid_device_service_replies()); /* First reply now pending. */
    assert(diagnostics().reply_pending == 2);
    tud_umount_cb();
    assert(!usb_hid_device_service_replies());
    assert(diagnostics().reply_pending == 0);
    tud_mount_cb();
    s_endpoint_ready[0] = true;
    assert(!usb_hid_device_service_replies());
    assert(s_transfer_count == 0);
    inject_spi(0x6086, 18, false);
    assert(usb_hid_device_service_replies());
    assert(s_transfer_count == 1 && s_transfers[0].report[15] == 0x86);
    assert(s_transfers[0].report[1] == 0); /* New session sequence. */
    finish_last_transfer();

    /* Also cover SET_CONFIGURATION/mount reset without a preceding unmount. */
    s_endpoint_ready[0] = false;
    inject_spi(0x603d, 18, true);
    assert(usb_hid_device_service_replies());
    tud_mount_cb();
    s_endpoint_ready[0] = true;
    assert(!usb_hid_device_service_replies());
    assert(s_transfer_count == 1);
}

static void test_transfer_completion_axes_and_suspend(void)
{
    fresh_session();
    uint8_t input[SWITCH_LEGACY_REPORT_SIZE];
    hid_report_make_switch_legacy_neutral(input);
    /* One end of each stick, independently packed in the wire report. */
    input[6] = 0x00; input[7] = 0xf0; input[8] = 0xff; /* 0, 4095 */
    input[9] = 0xff; input[10] = 0x0f; input[11] = 0x80; /* 4095, 2048 */
    usb_hid_diagnostics_t before = diagnostics();
    tud_suspend_cb(false);
    assert(!usb_hid_device_ready());
    assert(usb_hid_device_send_switch_pro_report(input) == ESP_ERR_INVALID_STATE);
    tud_resume_cb();
    assert(usb_hid_device_send_switch_pro_report(input) == ESP_OK);
    assert(diagnostics().input_completed == before.input_completed);
    finish_last_transfer();
    usb_hid_diagnostics_t after = diagnostics();
    assert(after.input_completed == before.input_completed + 1);
    assert(after.last_completed_report_id == 0x30);
    assert(after.last_completed_axes[0] == 0 && after.last_completed_axes[1] == 4095);
    assert(after.last_completed_axes[2] == 4095 && after.last_completed_axes[3] == 2048);
}

static void test_endpoint_ready_before_callback_and_private_report_copy(void)
{
    fresh_session();
    uint8_t input[SWITCH_LEGACY_REPORT_SIZE];
    hid_report_make_switch_legacy_neutral(input);
    input[6] = 0x00; input[7] = 0xf0; input[8] = 0xff;
    input[9] = 0xff; input[10] = 0x0f; input[11] = 0x80;
    usb_hid_diagnostics_t before = diagnostics();
    assert(usb_hid_device_send_switch_pro_report(input) == ESP_OK);
    /* TinyUSB clears endpoint busy before invoking complete; the other core
     * must not submit another report while that callback is outstanding. */
    s_endpoint_ready[0] = true;
    assert(!usb_hid_device_ready());
    assert(usb_hid_device_send_switch_pro_report(input) == ESP_ERR_INVALID_STATE);
    inject_spi(0x603d, 18, true);
    assert(usb_hid_device_service_replies());
    assert(s_transfer_count == 1);
    uint8_t overwritten[SWITCH_LEGACY_REPORT_SIZE];
    memset(overwritten, 0x81, sizeof(overwritten));
    tud_hid_report_complete_cb(0, overwritten, sizeof(overwritten));
    usb_hid_diagnostics_t after = diagnostics();
    assert(after.input_completed == before.input_completed + 1);
    assert(after.replies_completed == before.replies_completed);
    assert(after.last_completed_report_id == 0x30);
    assert(after.last_completed_axes[0] == 0 && after.last_completed_axes[1] == 4095);
    assert(after.last_completed_axes[2] == 4095 && after.last_completed_axes[3] == 2048);
    assert(usb_hid_device_service_replies());
    assert(s_transfer_count == 2 && s_transfers[1].report[0] == 0x21);
    finish_last_transfer();
}

static void test_async_reply_failure_retries_same_reply(void)
{
    fresh_session();
    uint8_t input[SWITCH_LEGACY_REPORT_SIZE];
    hid_report_make_switch_legacy_neutral(input);
    usb_hid_diagnostics_t before = diagnostics();
    inject_spi(0x603d, 18, true);
    inject_spi(0x6086, 18, true);
    assert(usb_hid_device_service_replies());
    assert(s_transfer_count == 1 && diagnostics().reply_pending == 2);
    uint8_t original[SWITCH_LEGACY_REPORT_SIZE];
    memcpy(original, s_transfers[0].report, sizeof(original));
    s_endpoint_ready[0] = true;
    assert(usb_hid_device_service_replies());
    assert(s_transfer_count == 1); /* No duplicate while awaiting completion. */
    assert(usb_hid_device_send_switch_pro_report(input) == ESP_ERR_INVALID_STATE);
    uint8_t overwritten[SWITCH_LEGACY_REPORT_SIZE] = {0x30};
    tud_hid_report_failed_cb(0, HID_REPORT_TYPE_INPUT, overwritten, 0);
    assert(diagnostics().reply_pending == 2);
    assert(diagnostics().replies_failed == before.replies_failed + 1);
    assert(usb_hid_device_service_replies());
    assert(s_transfer_count == 2);
    assert(memcmp(s_transfers[1].report, original, sizeof(original)) == 0);
    finish_last_transfer();
    assert(diagnostics().reply_pending == 1);
    assert(usb_hid_device_service_replies());
    assert(s_transfer_count == 3 && s_transfers[2].report[15] == 0x86);
    finish_last_transfer();
    usb_hid_diagnostics_t after = diagnostics();
    assert(after.replies_submitted == before.replies_submitted + 3);
    assert(after.replies_completed == before.replies_completed + 2);
    assert(after.input_failed == before.input_failed);
}

static void test_queue_capacity_retains_all_accepted_commands(void)
{
    fresh_session();
    usb_hid_diagnostics_t before = diagnostics();
    s_endpoint_ready[0] = false;
    for (uint32_t i = 0; i < 17; ++i) {
        inject_spi(0x603d + i, 1, (i & 1) == 0);
    }
    assert(s_transfer_count == 0);
    usb_hid_diagnostics_t queued = diagnostics();
    assert(queued.reply_pending == 16);
    assert(queued.replies_queued == before.replies_queued + 16);
    assert(queued.reply_queue_full == before.reply_queue_full + 1);
    s_endpoint_ready[0] = true;
    for (uint32_t i = 0; i < 16; ++i) {
        assert(usb_hid_device_service_replies());
        assert(s_transfer_count == i + 1);
        assert(s_transfers[i].report[15] == 0x3d + i);
        finish_last_transfer();
    }
    assert(!usb_hid_device_service_replies());
    assert(diagnostics().reply_pending == 0);
}

static void test_unmount_abandons_inflight_and_ignores_late_callback(void)
{
    fresh_session();
    inject_spi(0x603d, 9, true);
    assert(usb_hid_device_service_replies());
    assert(s_transfer_count == 1 && diagnostics().reply_pending == 1);
    usb_hid_diagnostics_t before = diagnostics();
    tud_umount_cb();
    tud_hid_report_complete_cb(0, s_transfers[0].report,
                               sizeof(s_transfers[0].report));
    assert(diagnostics().replies_completed == before.replies_completed);
    assert(diagnostics().reply_pending == 0);
    tud_mount_cb();
    s_endpoint_ready[0] = true;
    assert(!usb_hid_device_service_replies());
    assert(s_transfer_count == 1);
    inject_spi(0x6046, 9, true);
    assert(usb_hid_device_service_replies());
    assert(s_transfer_count == 2 && s_transfers[1].report[15] == 0x46);
    finish_last_transfer();
}

static void test_async_input_failure_does_not_retry_obsolete_state(void)
{
    fresh_session();
    uint8_t input[SWITCH_LEGACY_REPORT_SIZE];
    hid_report_make_switch_legacy_neutral(input);
    usb_hid_diagnostics_t before = diagnostics();
    assert(usb_hid_device_send_switch_pro_report(input) == ESP_OK);
    s_endpoint_ready[0] = true;
    tud_hid_report_failed_cb(0, HID_REPORT_TYPE_INPUT, s_transfers[0].report, 0);
    assert(diagnostics().input_failed == before.input_failed + 1);
    assert(diagnostics().replies_failed == before.replies_failed);
    assert(!usb_hid_device_service_replies());
    assert(s_transfer_count == 1);
    input[6] = 0xff; input[7] = 0x0f; input[8] = 0;
    assert(usb_hid_device_send_switch_pro_report(input) == ESP_OK);
    assert(s_transfer_count == 2 && s_transfers[1].report[6] == 0xff);
    finish_last_transfer();
}

static void test_malformed_output_and_spi_lengths(void)
{
    fresh_session();
    const uint8_t status[] = {0x80, 0x01};
    tud_hid_set_report_cb(0, 0, HID_REPORT_TYPE_FEATURE, status, sizeof(status));
    tud_hid_set_report_cb(0, 0, HID_REPORT_TYPE_INPUT, status, sizeof(status));
    tud_hid_set_report_cb(0, 0x80, HID_REPORT_TYPE_OUTPUT, NULL, 1);
    tud_hid_set_report_cb(0, 0x80, HID_REPORT_TYPE_OUTPUT, NULL, 0);
    tud_hid_set_report_cb(0, 0, HID_REPORT_TYPE_OUTPUT, status, 1);
    tud_hid_set_report_cb(1, 0, HID_REPORT_TYPE_OUTPUT, status, sizeof(status));
    assert(diagnostics().input_enabled && diagnostics().reply_pending == 0);

    uint8_t partial[15] = {0x01};
    partial[10] = 0x10;
    tud_hid_set_report_cb(0, 0, HID_REPORT_TYPE_OUTPUT, partial, sizeof(partial));
    assert(usb_hid_device_service_replies());
    assert(s_transfers[0].report[13] == 0 && s_transfers[0].report[14] == 0x10);
    finish_last_transfer();
    inject_spi(0x603d, 255, true);
    assert(usb_hid_device_service_replies());
    assert(s_transfers[1].report[13] == 0);
    finish_last_transfer();
    inject_spi(0x603d, 45, true);
    assert(usb_hid_device_service_replies());
    assert(s_transfers[2].report[13] == 0);
    finish_last_transfer();
    assert(submit_spi(0x603d, 44, true)[0] == 0xff);
    assert(usb_hid_device_send_switch_pro_report(NULL) == ESP_ERR_INVALID_ARG);
    uint8_t invalid[SWITCH_LEGACY_REPORT_SIZE] = {0x21};
    assert(usb_hid_device_send_switch_pro_report(invalid) == ESP_ERR_INVALID_ARG);
}

static void test_get_report_buffer_bounds(void)
{
    fresh_session();
    uint8_t guarded[66];
    memset(guarded, 0xa5, sizeof(guarded));
    assert(tud_hid_get_report_cb(0, 0x30, HID_REPORT_TYPE_INPUT, NULL, 64) == 0);
    assert(tud_hid_get_report_cb(0, 0x30, HID_REPORT_TYPE_INPUT, guarded + 1, 0) == 0);
    for (uint16_t size = 1; size <= 64; ++size) {
        memset(guarded, 0xa5, sizeof(guarded));
        uint16_t written = tud_hid_get_report_cb(0, 0x30, HID_REPORT_TYPE_INPUT, guarded + 1, size);
        assert(written <= size);
        assert(guarded[0] == 0xa5 && guarded[size + 1] == 0xa5);
        memset(guarded, 0xa5, sizeof(guarded));
        written = tud_hid_get_report_cb(0, MANAGER_FEATURE_REPORT_ID, HID_REPORT_TYPE_FEATURE,
                                       guarded + 1, size);
        assert(written <= size);
        assert(guarded[0] == 0xa5 && guarded[size + 1] == 0xa5);
    }
}

static void test_feature_command_framing_and_session_reset(void)
{
    fresh_session();
    const uint8_t embedded[] = "Y7HID1status\0reboot";
    unsigned before = s_control_commands;
    tud_hid_set_report_cb(0, MANAGER_FEATURE_REPORT_ID, HID_REPORT_TYPE_FEATURE,
                          embedded, sizeof(embedded));
    assert(s_control_commands == before);
    uint8_t response[64];
    assert(tud_hid_get_report_cb(0, MANAGER_FEATURE_REPORT_ID, HID_REPORT_TYPE_FEATURE,
                                response, sizeof(response)) == sizeof(response));
    assert(response[6] != 0);
    fresh_session();
    tud_hid_get_report_cb(0, MANAGER_FEATURE_REPORT_ID, HID_REPORT_TYPE_FEATURE,
                          response, sizeof(response));
    assert(response[6] == 0 && response[7] == 0); /* No old session's response. */

    const uint8_t padded[] = "Y7HID1status\0\0\0";
    tud_hid_set_report_cb(0, MANAGER_FEATURE_REPORT_ID, HID_REPORT_TYPE_FEATURE,
                          padded, sizeof(padded));
    assert(s_control_commands == before + 1);
}

static void test_rumble_in_subcommand_report(void)
{
    fresh_session();
    uint8_t full[11] = {0x01, 0x01, 0x00, 0x01, 0x40, 0x40,
                        0x80, 0x80, 0x80, 0x80, 0x02};
    unsigned before = s_rumble_reports;
    tud_hid_set_report_cb(0, 0, HID_REPORT_TYPE_OUTPUT, full, sizeof(full));
    assert(s_rumble_reports == before + 1);
    assert(memcmp(s_last_rumble, full + 2, 8) == 0);
    assert(usb_hid_device_service_replies());
    assert(s_transfers[0].report[14] == 0x02);
    finish_last_transfer();
}

int main(void)
{
    assert(usb_hid_device_init() == ESP_OK);
    test_calibration_commands();
    test_busy_reply_retry_and_priority();
    test_input_gate_and_submission_accounting();
    test_session_reset_discards_queued_and_pending_replies();
    test_transfer_completion_axes_and_suspend();
    test_endpoint_ready_before_callback_and_private_report_copy();
    test_async_reply_failure_retries_same_reply();
    test_queue_capacity_retains_all_accepted_commands();
    test_unmount_abandons_inflight_and_ignores_late_callback();
    test_async_input_failure_does_not_retry_obsolete_state();
    test_malformed_output_and_spi_lengths();
    test_get_report_buffer_bounds();
    test_feature_command_framing_and_session_reset();
    test_rumble_in_subcommand_report();
    puts("USB HID transport regression tests passed");
    return 0;
}
