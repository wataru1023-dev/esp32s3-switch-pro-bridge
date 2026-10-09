/* Compile the real vendor responder with host-only service stubs. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include "freertos/task.h"
#include "tusb.h"
#include "usb_descriptors.h"
#include "usb_switch2_vendor.h"

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                const tusb_control_request_t *request);
void tud_vendor_rx_cb(uint8_t itf, const uint8_t *buffer, uint16_t length);
void tud_vendor_tx_cb(uint8_t itf, uint32_t length);

static unsigned s_control_transfers;
static uint16_t s_last_control_length;
static uint32_t s_available;
static unsigned s_bulk_bytes;
static unsigned s_control_commands;
static uint8_t s_bulk_capture[3072];
static size_t s_capture_len;
static TaskFunction_t s_rumble_task;
static jmp_buf s_task_exit;
static unsigned s_task_ticks, s_ble_writes;
static esp_err_t s_ble_result = ESP_OK;
static bool s_request_new_stop_during_write;
static uint8_t s_last_ble_packet[33];

bool tud_control_xfer(uint8_t rhport, const tusb_control_request_t *request,
                       void *buffer, uint16_t length)
{
    (void)rhport;
    assert(request && buffer && request->bmRequestType_bit.direction == TUSB_DIR_IN);
    s_control_transfers++;
    s_last_control_length = length;
    return true;
}
bool tud_vendor_n_mounted(uint8_t itf) { return itf == 0; }
uint32_t tud_vendor_n_write_available(uint8_t itf) { assert(itf == 0); return s_available; }
uint32_t tud_vendor_n_write(uint8_t itf, const void *buffer, uint32_t length)
{
    assert(itf == 0 && buffer && length <= s_available);
    s_available -= length;
    s_bulk_bytes += length;
    assert(s_capture_len + length <= sizeof(s_bulk_capture));
    memcpy(s_bulk_capture + s_capture_len, buffer, length);
    s_capture_len += length;
    return length;
}
uint32_t tud_vendor_n_write_flush(uint8_t itf) { assert(itf == 0); return 0; }
void tud_vendor_n_read_flush(uint8_t itf) { assert(itf == 0); }
int64_t esp_timer_get_time(void) { return 1000000; }
bool app_log_debug_enabled(void) { return false; }
const char *esp_err_to_name(esp_err_t err) { (void)err; return "error"; }
esp_err_t ble_central_send_rumble(const uint8_t *data, uint16_t length)
{
    assert(data && length == 33);
    memcpy(s_last_ble_packet, data, length);
    s_ble_writes++;
    if (s_request_new_stop_during_write) {
        s_request_new_stop_during_write = false;
        usb_switch2_vendor_stop_hd_rumble();
    }
    return s_ble_result;
}
esp_err_t control_protocol_handle_line(const char *line, char *reply, int length)
{
    assert(line && reply && length > 0);
    s_control_commands++;
    snprintf(reply, (size_t)length, "{\"ok\":true}");
    return ESP_OK;
}
BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, uint32_t stack,
                       void *arg, UBaseType_t priority, void *handle)
{
    (void)name; (void)stack; (void)arg; (void)priority; (void)handle;
    s_rumble_task = fn;
    return pdPASS;
}
void vTaskDelay(TickType_t ticks)
{
    (void)ticks;
    assert(s_task_ticks > 0);
    if (--s_task_ticks == 0) longjmp(s_task_exit, 1);
}

static void run_rumble_ticks(unsigned count)
{
    assert(s_rumble_task && count > 0);
    s_task_ticks = count;
    if (setjmp(s_task_exit) == 0) s_rumble_task(NULL);
}

static tusb_control_request_t request(uint16_t index)
{
    tusb_control_request_t req = {0};
    req.bmRequestType_bit.direction = TUSB_DIR_IN;
    req.bmRequestType_bit.type = TUSB_REQ_TYPE_VENDOR;
    req.bmRequestType_bit.recipient = index == 5 ? TUSB_REQ_RCPT_INTERFACE : TUSB_REQ_RCPT_DEVICE;
    req.bRequest = USB_SWITCH2_MS_VENDOR_CODE;
    req.wIndex = index;
    req.wValue = index == 5 ? USB_SWITCH2_VENDOR_INTERFACE : 0;
    req.wLength = 4096;
    return req;
}

static void test_descriptor_requests(void)
{
    assert(!tud_vendor_control_xfer_cb(0, CONTROL_STAGE_SETUP, NULL));
    for (uint16_t index = 4; index <= 7; ++index) {
        if (index == 6) continue;
        tusb_control_request_t req = request(index);
        unsigned before = s_control_transfers;
        assert(tud_vendor_control_xfer_cb(0, CONTROL_STAGE_SETUP, &req));
        assert(s_control_transfers == before + 1 && s_last_control_length > 0);
        req.bmRequestType_bit.direction = TUSB_DIR_OUT;
        assert(!tud_vendor_control_xfer_cb(0, CONTROL_STAGE_SETUP, &req));
        assert(s_control_transfers == before + 1); /* No writable flash pointer. */
        req = request(index);
        req.bmRequestType_bit.recipient = 3;
        assert(!tud_vendor_control_xfer_cb(0, CONTROL_STAGE_SETUP, &req));
        req = request(index);
        req.wValue = 99;
        assert(!tud_vendor_control_xfer_cb(0, CONTROL_STAGE_SETUP, &req));
        req = request(index);
        req.bRequest++;
        assert(!tud_vendor_control_xfer_cb(0, CONTROL_STAGE_SETUP, &req));
    }
}

static void test_bulk_tail_reset(void)
{
    uint8_t flash_read[16] = {0x02};
    flash_read[12] = 0x80; flash_read[13] = 0x30; flash_read[14] = 0x01;
    s_available = 4;
    tud_vendor_rx_cb(0, flash_read, sizeof(flash_read));
    assert(usb_switch2_vendor_pending_len() == 80);
    assert(usb_switch2_vendor_pending_offset() == 4);
    unsigned before = s_bulk_bytes;
    usb_switch2_vendor_reset_hid_guard();
    assert(usb_switch2_vendor_pending_len() == 0);
    s_available = 256;
    tud_vendor_tx_cb(0, 4);
    assert(s_bulk_bytes == before); /* No prior session's response tail. */
}

static void test_manager_command_packet_limits(void)
{
    uint8_t packet[64];
    memcpy(packet, "Y7CTL1", 6);
    memset(packet + 6, 'x', sizeof(packet) - 6);
    s_available = 256;
    s_capture_len = 0;
    unsigned before = s_control_commands;
    tud_vendor_rx_cb(0, packet, sizeof(packet));
    assert(s_control_commands == before);
    assert(s_capture_len > 8 && memcmp(s_bulk_capture, "Y7RSP1", 6) == 0);
    assert(memcmp(s_bulk_capture + 8, "{\"ok\":false", 11) == 0);

    packet[63] = '\n'; /* Exactly one full packet with explicit terminator. */
    s_available = 256;
    s_capture_len = 0;
    tud_vendor_rx_cb(0, packet, sizeof(packet));
    assert(s_control_commands == before + 1);
    assert(memcmp(s_bulk_capture + 8, "{\"ok\":true", 10) == 0);

    packet[20] = 0; /* Embedded NUL must not hide a trailing command. */
    s_available = 256;
    s_capture_len = 0;
    tud_vendor_rx_cb(0, packet, sizeof(packet));
    assert(s_control_commands == before + 1);

    memset(packet + 6, 'x', sizeof(packet) - 6);
    s_available = 256;
    s_capture_len = 0;
    tud_vendor_rx_cb(0, packet, 63); /* Existing short-packet framing works. */
    assert(s_control_commands == before + 2);
}

static void test_standard_neutral_rumble(void)
{
    const uint8_t neutral[8] = {0x00, 0x01, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40};
    usb_switch2_vendor_switch_pro_rumble(neutral, sizeof(neutral));
    assert(!usb_switch2_vendor_hd_rumble_active());
    uint8_t mixed[8];
    memcpy(mixed, neutral, sizeof(mixed));
    memset(mixed + 4, 0x80, 4);
    usb_switch2_vendor_switch_pro_rumble(mixed, sizeof(mixed));
    assert(usb_switch2_vendor_hd_rumble_active());
    run_rumble_ticks(1);
    /* Neutral left motor encodes zero amplitude in the translated BLE block. */
    uint64_t left = 0;
    for (size_t i = 0; i < 5; ++i) left |= (uint64_t)s_last_ble_packet[i + 2] << (8 * i);
    assert(((left >> 10) & 0x3ff) == 0 && ((left >> 30) & 0x3ff) == 0);
    usb_switch2_vendor_switch_pro_rumble(neutral, sizeof(neutral));
    assert(!usb_switch2_vendor_hd_rumble_active());
}

static void test_stop_packets_retry_and_generation(void)
{
    usb_switch2_vendor_stop_hd_rumble();
    unsigned before = s_ble_writes;
    s_ble_result = ESP_ERR_INVALID_STATE;
    run_rumble_ticks(1); /* Failed write must leave all three stops pending. */
    assert(s_ble_writes == before + 1);
    s_ble_result = ESP_OK;
    run_rumble_ticks(4);
    assert(s_ble_writes == before + 4);

    usb_switch2_vendor_stop_hd_rumble();
    before = s_ble_writes;
    s_request_new_stop_during_write = true;
    run_rumble_ticks(1); /* Older completion must not consume the new request. */
    run_rumble_ticks(4);
    assert(s_ble_writes == before + 4);
}

int main(void)
{
    usb_switch2_vendor_init();
    test_descriptor_requests();
    test_bulk_tail_reset();
    test_manager_command_packet_limits();
    test_standard_neutral_rumble();
    test_stop_packets_retry_and_generation();
    puts("USB vendor safety regression tests passed");
    return 0;
}
