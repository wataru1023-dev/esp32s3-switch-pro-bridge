#include "hid_report.h"
#include "usb_descriptors.h"

#define EPNUM_HID_OUT 0x01
#define EPNUM_HID 0x81
#define EPNUM_VENDOR_OUT 0x02
#define EPNUM_VENDOR_IN 0x82
#define ITF_NUM_HID 0
#define ITF_NUM_VENDOR USB_SWITCH2_VENDOR_INTERFACE
#define ITF_NUM_TOTAL_NINTENDO 2
#define CONFIG_TOTAL_LEN_NINTENDO (TUD_CONFIG_DESC_LEN + TUD_HID_INOUT_DESC_LEN + TUD_VENDOR_DESC_LEN)
#define HID_POLL_INTERVAL_MS 1
#define VENDOR_BULK_PACKET_SIZE 64
#define CONFIG_ATTR_NINTENDO 0
#define CONFIG_POWER_MA_NINTENDO 500

#define TUD_VENDOR_INOUT_DESCRIPTOR(_itfnum, _stridx, _epin, _epout, _epsize) \
    9, TUSB_DESC_INTERFACE, _itfnum, 0, 2, TUSB_CLASS_VENDOR_SPECIFIC, 0x00, 0x00, _stridx, \
    7, TUSB_DESC_ENDPOINT, _epin, TUSB_XFER_BULK, U16_TO_U8S_LE(_epsize), 0, \
    7, TUSB_DESC_ENDPOINT, _epout, TUSB_XFER_BULK, U16_TO_U8S_LE(_epsize), 0

#define TUD_HID_Y700_INOUT_DESCRIPTOR(_itfnum, _stridx, _boot_protocol, _report_desc_len, _epin, _epout, _epsize, _ep_interval) \
    9, TUSB_DESC_INTERFACE, _itfnum, 0, 2, TUSB_CLASS_HID, (uint8_t)((_boot_protocol) ? (uint8_t)HID_SUBCLASS_BOOT : 0), _boot_protocol, _stridx, \
    9, HID_DESC_TYPE_HID, U16_TO_U8S_LE(0x0101), 0, 1, HID_DESC_TYPE_REPORT, U16_TO_U8S_LE(_report_desc_len), \
    7, TUSB_DESC_ENDPOINT, _epin, TUSB_XFER_INTERRUPT, U16_TO_U8S_LE(_epsize), _ep_interval, \
    7, TUSB_DESC_ENDPOINT, _epout, TUSB_XFER_INTERRUPT, U16_TO_U8S_LE(_epsize), _ep_interval

enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_CONFIG,
    STRID_HID_INTERFACE,
    STRID_HID2_INTERFACE,
    STRID_VENDOR_INTERFACE,
};

const uint8_t desc_hid_report_switch_legacy[] = {
    0x05, 0x01, 0x09, 0x05, 0xa1, 0x01, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08,
    0x85, SWITCH_LEGACY_REPORT_ID_SUBCOMMAND_REPLY, 0x95, 0x3f, 0x09, 0x01, 0x81, 0x02,
    0x85, SWITCH_LEGACY_REPORT_ID_FULL_STATE, 0x95, 0x3f, 0x09, 0x01, 0x81, 0x02,
    0x85, SWITCH_LEGACY_REPORT_ID_FULL_STATE_MCU, 0x95, 0x3f, 0x09, 0x01, 0x81, 0x02,
    0x85, SWITCH_LEGACY_REPORT_ID_SIMPLE_STATE, 0x95, 0x3f, 0x09, 0x01, 0x81, 0x02,
    0x85, SWITCH_LEGACY_REPORT_ID_COMMAND_ACK, 0x95, 0x3f, 0x09, 0x01, 0x81, 0x02,
    0x85, SWITCH_LEGACY_REPORT_ID_RUMBLE_SUBCOMMAND, 0x95, 0x3f, 0x09, 0x01, 0x91, 0x02,
    0x85, SWITCH_LEGACY_REPORT_ID_RUMBLE, 0x95, 0x3f, 0x09, 0x01, 0x91, 0x02,
    0x85, SWITCH_LEGACY_REPORT_ID_PROPRIETARY, 0x95, 0x3f, 0x09, 0x01, 0x91, 0x02,
    0x85, MANAGER_FEATURE_REPORT_ID, 0x95, 0x3f, 0x09, 0x01, 0xb1, 0x02,
    0xc0,
};

static const tusb_desc_device_t desc_device_switch_pro = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_VID_NINTENDO_EXPERIMENT,
    .idProduct = USB_PID_NINTENDO_SWITCH_PRO_LEGACY,
    .bcdDevice = 0x0209,
    .iManufacturer = STRID_MANUFACTURER,
    .iProduct = STRID_PRODUCT,
    .iSerialNumber = STRID_SERIAL,
    .bNumConfigurations = 0x01,
};

static const uint8_t desc_configuration_switch_pro[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL_NINTENDO, STRID_CONFIG, CONFIG_TOTAL_LEN_NINTENDO, CONFIG_ATTR_NINTENDO, CONFIG_POWER_MA_NINTENDO),
    TUD_HID_Y700_INOUT_DESCRIPTOR(ITF_NUM_HID, STRID_HID_INTERFACE, HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_report_switch_legacy), EPNUM_HID, EPNUM_HID_OUT, SWITCH_LEGACY_REPORT_SIZE, HID_POLL_INTERVAL_MS),
    TUD_VENDOR_INOUT_DESCRIPTOR(ITF_NUM_VENDOR, STRID_VENDOR_INTERFACE, EPNUM_VENDOR_IN, EPNUM_VENDOR_OUT, VENDOR_BULK_PACKET_SIZE),
};

static const char *string_desc_nintendo[] = {
    "",
    "Nintendo Co., Ltd.",
    "Nintendo Switch Pro Controller",
    "HA2F83JF",
    "Nintendo Switch Pro Controller",
    "HID Interface",
    "",
    "Nintendo Switch 2 bulk",
};

uint16_t usb_descriptors_current_vid(void)
{
    return USB_VID_NINTENDO_EXPERIMENT;
}

uint16_t usb_descriptors_current_pid(void)
{
    return USB_PID_NINTENDO_SWITCH_PRO_LEGACY;
}

const char *usb_descriptors_current_product(void)
{
    return "Nintendo Switch Pro Controller";
}

const char *usb_descriptors_current_manufacturer(void)
{
    return "Nintendo Co., Ltd.";
}

const tusb_desc_device_t *usb_descriptors_current_device(void)
{
    return &desc_device_switch_pro;
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return desc_hid_report_switch_legacy;
}

const uint8_t *usb_descriptors_current_configuration(void)
{
    return desc_configuration_switch_pro;
}

const char **usb_descriptors_current_strings(void)
{
    return string_desc_nintendo;
}

int usb_descriptors_current_string_count(void)
{
    return 8;
}
