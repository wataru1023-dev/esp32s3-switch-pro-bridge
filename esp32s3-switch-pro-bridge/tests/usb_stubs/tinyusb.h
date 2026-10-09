#pragma once

#include "esp_err.h"
#include "tusb.h"

typedef struct {
    const tusb_desc_device_t *device_descriptor;
    const char **string_descriptor;
    int string_descriptor_count;
    bool external_phy;
    const uint8_t *configuration_descriptor;
} tinyusb_config_t;

esp_err_t tinyusb_driver_install(const tinyusb_config_t *config);
