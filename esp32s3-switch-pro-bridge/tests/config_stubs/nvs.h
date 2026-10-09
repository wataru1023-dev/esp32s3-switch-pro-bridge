#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef unsigned nvs_handle_t;
#define NVS_READONLY 0
#define NVS_READWRITE 1

esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle);
esp_err_t nvs_get_u16(nvs_handle_t handle, const char *key, uint16_t *value);
esp_err_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *value);
esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *out, size_t *length);
esp_err_t nvs_set_u16(nvs_handle_t handle, const char *key, uint16_t value);
esp_err_t nvs_set_u8(nvs_handle_t handle, const char *key, uint8_t value);
esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value);
esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key);
esp_err_t nvs_commit(nvs_handle_t handle);
void nvs_close(nvs_handle_t handle);
