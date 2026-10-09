#include "device_config.h"
#include <stdatomic.h>
#include <string.h>
#include "esp_app_desc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "app_log.h"

static const char *TAG = "config";
static const char *NVS_NAMESPACE = "bridge";
static const char *NVS_KEY_REPORT_RATE = "rate_hz";
static const char *NVS_KEY_BLE_AUTO = "ble_auto";
static const char *NVS_KEY_BLE_TARGET = "ble_target";

#define DEFAULT_REPORT_RATE_HZ 66
#define MIN_REPORT_RATE_HZ 20
#define MAX_REPORT_RATE_HZ 1000
#define BLE_TARGET_MAX_LEN 40

static atomic_bool s_bridge_running = true;
static atomic_int s_hid_test_mode = HID_TEST_NEUTRAL;
static atomic_uint s_report_rate_hz = DEFAULT_REPORT_RATE_HZ;
static atomic_bool s_ble_autoconnect = true;
static char s_ble_target[BLE_TARGET_MAX_LEN];
static portMUX_TYPE s_target_lock = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t s_save_mutex;

static void replace_ble_target(const char *target)
{
    portENTER_CRITICAL(&s_target_lock);
    memcpy(s_ble_target, target, sizeof(s_ble_target));
    portEXIT_CRITICAL(&s_target_lock);
}

static bool config_save_lock(void)
{
    return s_save_mutex != NULL && xSemaphoreTake(s_save_mutex, portMAX_DELAY) == pdTRUE;
}

static uint16_t sanitize_report_rate_hz(uint16_t rate_hz)
{
    if (rate_hz < MIN_REPORT_RATE_HZ) {
        return MIN_REPORT_RATE_HZ;
    }
    if (rate_hz > MAX_REPORT_RATE_HZ) {
        return MAX_REPORT_RATE_HZ;
    }
    return rate_hz;
}

void device_config_init(void)
{
    /* Called once during startup, before BLE and console tasks are created. */
    if (s_save_mutex == NULL) {
        s_save_mutex = xSemaphoreCreateMutex();
        ESP_ERROR_CHECK(s_save_mutex != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    }
    atomic_store_explicit(&s_bridge_running, true, memory_order_relaxed);
    atomic_store_explicit(&s_hid_test_mode, HID_TEST_NEUTRAL, memory_order_relaxed);
    atomic_store_explicit(&s_report_rate_hz, DEFAULT_REPORT_RATE_HZ, memory_order_relaxed);
    atomic_store_explicit(&s_ble_autoconnect, true, memory_order_relaxed);
    char loaded_target[BLE_TARGET_MAX_LEN] = {0};

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_OK) {
        uint16_t stored_rate = DEFAULT_REPORT_RATE_HZ;
        err = nvs_get_u16(handle, NVS_KEY_REPORT_RATE, &stored_rate);
        if (err == ESP_OK) {
            atomic_store_explicit(&s_report_rate_hz, sanitize_report_rate_hz(stored_rate), memory_order_relaxed);
        } else if (err == ESP_ERR_NVS_NOT_FOUND) {
            APP_LOGI(TAG, "no persisted report rate; defaulting to %u Hz", (unsigned)s_report_rate_hz);
        } else {
            APP_LOGW(TAG, "failed to read report rate err=%d; defaulting to %u Hz",
                     (int)err,
                     (unsigned)s_report_rate_hz);
        }

        uint8_t stored_auto = 1;
        err = nvs_get_u8(handle, NVS_KEY_BLE_AUTO, &stored_auto);
        if (err == ESP_OK) {
            atomic_store_explicit(&s_ble_autoconnect, stored_auto != 0, memory_order_relaxed);
        } else if (err == ESP_ERR_NVS_NOT_FOUND) {
            APP_LOGI(TAG, "no persisted BLE autoconnect; defaulting to enabled");
        } else {
            APP_LOGW(TAG, "failed to read BLE autoconnect err=%d; defaulting to enabled", (int)err);
        }

        size_t target_len = sizeof(loaded_target);
        err = nvs_get_str(handle, NVS_KEY_BLE_TARGET, loaded_target, &target_len);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            loaded_target[0] = 0;
            APP_LOGI(TAG, "no persisted BLE target; autoconnect will scan");
        } else if (err != ESP_OK) {
            loaded_target[0] = 0;
            APP_LOGW(TAG, "failed to read BLE target err=%d; autoconnect will scan", (int)err);
        } else {
            loaded_target[sizeof(loaded_target) - 1] = 0;
        }
        nvs_close(handle);
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        APP_LOGI(TAG, "config namespace not found; using defaults");
    } else {
        APP_LOGW(TAG, "failed to open config namespace err=%d; using defaults", (int)err);
    }

    replace_ble_target(loaded_target);
    APP_LOGI(TAG, "config loaded: report_rate_hz=%u ble_auto=%s ble_target=%s",
             (unsigned)s_report_rate_hz,
             s_ble_autoconnect ? "on" : "off",
             loaded_target[0] ? loaded_target : "<scan>");
}

bool device_config_bridge_running(void)
{
    return atomic_load_explicit(&s_bridge_running, memory_order_relaxed);
}

void device_config_set_bridge_running(bool running)
{
    atomic_store_explicit(&s_bridge_running, running, memory_order_relaxed);
}

hid_test_mode_t device_config_get_hid_test_mode(void)
{
    return (hid_test_mode_t)atomic_load_explicit(&s_hid_test_mode, memory_order_relaxed);
}

void device_config_set_hid_test_mode(hid_test_mode_t mode)
{
    atomic_store_explicit(&s_hid_test_mode, mode, memory_order_relaxed);
}

const char *hid_test_mode_to_string(hid_test_mode_t mode)
{
    switch (mode) {
    case HID_TEST_AUTO_A:
        return "auto_a";
    case HID_TEST_NEUTRAL:
        return "neutral";
    case HID_TEST_A_HELD:
        return "a_held";
    default:
        return "unknown";
    }
}

uint16_t device_config_get_report_rate_hz(void)
{
    return (uint16_t)atomic_load_explicit(&s_report_rate_hz, memory_order_relaxed);
}

void device_config_set_report_rate_hz(uint16_t rate_hz)
{
    atomic_store_explicit(&s_report_rate_hz, sanitize_report_rate_hz(rate_hz), memory_order_relaxed);
}

esp_err_t device_config_save_report_rate_hz(uint16_t rate_hz)
{
    uint16_t sanitized = sanitize_report_rate_hz(rate_hz);
    if (!config_save_lock()) {
        return ESP_ERR_INVALID_STATE;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        xSemaphoreGive(s_save_mutex);
        APP_LOGW(TAG, "failed to open config namespace for report-rate write err=%d", (int)err);
        return err;
    }

    err = nvs_set_u16(handle, NVS_KEY_REPORT_RATE, sanitized);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err == ESP_OK) {
        atomic_store_explicit(&s_report_rate_hz, sanitized, memory_order_relaxed);
    } else {
        APP_LOGW(TAG, "failed to save report rate err=%d", (int)err);
    }
    xSemaphoreGive(s_save_mutex);
    if (err == ESP_OK) {
        APP_LOGI(TAG, "report rate saved: %u Hz", (unsigned)sanitized);
    }
    return err;
}

bool device_config_get_ble_autoconnect(void)
{
    return atomic_load_explicit(&s_ble_autoconnect, memory_order_relaxed);
}

esp_err_t device_config_save_ble_autoconnect(bool enabled)
{
    if (!config_save_lock()) {
        return ESP_ERR_INVALID_STATE;
    }
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        xSemaphoreGive(s_save_mutex);
        APP_LOGW(TAG, "failed to open config namespace for BLE auto write err=%d", (int)err);
        return err;
    }

    err = nvs_set_u8(handle, NVS_KEY_BLE_AUTO, enabled ? 1 : 0);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err == ESP_OK) {
        atomic_store_explicit(&s_ble_autoconnect, enabled, memory_order_relaxed);
    } else {
        APP_LOGW(TAG, "failed to save BLE autoconnect err=%d", (int)err);
    }
    xSemaphoreGive(s_save_mutex);
    if (err == ESP_OK) {
        APP_LOGI(TAG, "BLE autoconnect saved: %s", enabled ? "on" : "off");
    }
    return err;
}

void device_config_copy_ble_target(char *out, size_t capacity)
{
    if (out == NULL || capacity == 0) {
        return;
    }
    portENTER_CRITICAL(&s_target_lock);
    size_t length = strlen(s_ble_target);
    if (length >= capacity) {
        length = capacity - 1;
    }
    memcpy(out, s_ble_target, length);
    out[length] = 0;
    portEXIT_CRITICAL(&s_target_lock);
}

esp_err_t device_config_save_ble_target(const char *target)
{
    char sanitized[BLE_TARGET_MAX_LEN] = {0};
    if (target != NULL) {
        size_t length = 0;
        while (length < sizeof(sanitized) && target[length] != 0) {
            length++;
        }
        if (length >= sizeof(sanitized)) {
            return ESP_ERR_INVALID_ARG;
        }
        memcpy(sanitized, target, length);
    }
    if (!config_save_lock()) {
        return ESP_ERR_INVALID_STATE;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        xSemaphoreGive(s_save_mutex);
        APP_LOGW(TAG, "failed to open config namespace for BLE target write err=%d", (int)err);
        return err;
    }

    if (sanitized[0]) {
        err = nvs_set_str(handle, NVS_KEY_BLE_TARGET, sanitized);
    } else {
        err = nvs_erase_key(handle, NVS_KEY_BLE_TARGET);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            err = ESP_OK;
        }
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err == ESP_OK) {
        replace_ble_target(sanitized);
    } else {
        APP_LOGW(TAG, "failed to save BLE target err=%d", (int)err);
    }
    xSemaphoreGive(s_save_mutex);
    if (err == ESP_OK) {
        APP_LOGI(TAG, "BLE target saved: %s", sanitized[0] ? sanitized : "<scan>");
    }
    return err;
}

const char *device_config_get_version(void)
{
    return esp_app_get_description()->version;
}
