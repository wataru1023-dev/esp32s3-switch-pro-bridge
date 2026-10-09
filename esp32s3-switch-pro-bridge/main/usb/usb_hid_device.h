#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "hid_report.h"

typedef struct {
    bool mounted;
    bool suspended;
    bool endpoint_ready;
    bool input_enabled;
    uint32_t input_submitted;
    uint32_t input_completed;
    uint32_t input_failed;
    uint32_t input_blocked;
    uint32_t replies_queued;
    uint32_t replies_submitted;
    uint32_t replies_completed;
    uint32_t replies_failed;
    uint32_t reply_queue_full;
    uint16_t reply_pending;
    uint8_t last_completed_report_id;
    uint16_t last_completed_axes[4];
} usb_hid_diagnostics_t;

esp_err_t usb_hid_device_init(void);
bool usb_hid_device_ready(void);
/* Called by the input task. Returns true while command replies take priority. */
bool usb_hid_device_service_replies(void);
void usb_hid_device_get_diagnostics(usb_hid_diagnostics_t *out);
const char *usb_hid_device_state_string(void);
uint32_t usb_hid_device_out_count(void);
uint8_t usb_hid_device_last_out_report_id(void);
uint8_t usb_hid_device_last_out_effective_report_id(void);
uint8_t usb_hid_device_last_out_type(void);
uint16_t usb_hid_device_last_out_len(void);
uint8_t usb_hid_device_last_out_first_byte(void);
uint32_t usb_hid_device_get_count(void);
uint8_t usb_hid_device_last_get_report_id(void);
uint8_t usb_hid_device_last_get_type(void);
uint16_t usb_hid_device_last_get_req_len(void);
uint16_t usb_hid_device_last_get_resp_len(void);
esp_err_t usb_hid_device_send_switch_pro_report(const uint8_t report[SWITCH_LEGACY_REPORT_SIZE]);
