#pragma once

#include <stdbool.h>
#include <stdint.h>

#define TUSB_DIR_OUT 0
#define TUSB_DIR_IN 1
#define TUSB_REQ_TYPE_VENDOR 2
#define TUSB_REQ_RCPT_DEVICE 0
#define TUSB_REQ_RCPT_INTERFACE 1
#define CONTROL_STAGE_SETUP 0
#define CONTROL_STAGE_DATA 1
#define CONTROL_STAGE_ACK 2
#define TUSB_DESC_STRING 3
#define TUD_BOS_DESC_LEN 5
#define TUD_BOS_MICROSOFT_OS_DESC_LEN 28
#define TUD_BOS_DESCRIPTOR(total, count) 5, 15, U16_TO_U8S_LE(total), count
#define TUD_BOS_MS_OS_20_DESCRIPTOR(length, code) \
    28, 16, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
    U32_TO_U8S_LE(0x06030000), U16_TO_U8S_LE(length), code, 0
#define U16_TO_U8S_LE(value) ((value) & 0xff), (((value) >> 8) & 0xff)
#define U32_TO_U8S_LE(value) U16_TO_U8S_LE(value), U16_TO_U8S_LE((value) >> 16)
#define TU_VERIFY_STATIC(condition, message) _Static_assert(condition, message)
#define MS_OS_20_SET_HEADER_DESCRIPTOR 0
#define MS_OS_20_SUBSET_HEADER_CONFIGURATION 1
#define MS_OS_20_SUBSET_HEADER_FUNCTION 2
#define MS_OS_20_FEATURE_COMPATBLE_ID 3
#define MS_OS_20_FEATURE_REG_PROPERTY 4

typedef struct {
    struct { uint8_t recipient, type, direction; } bmRequestType_bit;
    uint8_t bRequest;
    uint16_t wValue, wIndex, wLength;
} tusb_control_request_t;

typedef struct { uint8_t unused; } tusb_desc_device_t;
typedef enum {
    HID_REPORT_TYPE_INVALID = 0,
    HID_REPORT_TYPE_INPUT = 1,
    HID_REPORT_TYPE_OUTPUT = 2,
    HID_REPORT_TYPE_FEATURE = 3,
} hid_report_type_t;

bool tud_hid_n_ready(uint8_t instance);
bool tud_hid_n_report(uint8_t instance, uint8_t report_id,
                      const void *report, uint16_t length);
bool tud_control_xfer(uint8_t rhport, const tusb_control_request_t *request,
                       void *buffer, uint16_t length);
bool tud_vendor_n_mounted(uint8_t interface);
uint32_t tud_vendor_n_write_available(uint8_t interface);
uint32_t tud_vendor_n_write(uint8_t interface, const void *buffer, uint32_t length);
uint32_t tud_vendor_n_write_flush(uint8_t interface);
void tud_vendor_n_read_flush(uint8_t interface);
