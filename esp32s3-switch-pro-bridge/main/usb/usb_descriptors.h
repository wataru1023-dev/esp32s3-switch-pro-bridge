#pragma once

#include <stdint.h>
#include <stddef.h>
#include "tusb.h"

#define USB_VID_NINTENDO_EXPERIMENT 0x057e
#define USB_PID_NINTENDO_SWITCH_PRO_LEGACY 0x2009
#define USB_SWITCH2_VENDOR_INTERFACE 1
#define USB_SWITCH2_MS_VENDOR_CODE 0xcd

extern const uint8_t desc_hid_report_switch_legacy[];

uint16_t usb_descriptors_current_vid(void);
uint16_t usb_descriptors_current_pid(void);
const char *usb_descriptors_current_product(void);
const char *usb_descriptors_current_manufacturer(void);
const tusb_desc_device_t *usb_descriptors_current_device(void);
const uint8_t *usb_descriptors_current_configuration(void);
const char **usb_descriptors_current_strings(void);
int usb_descriptors_current_string_count(void);
