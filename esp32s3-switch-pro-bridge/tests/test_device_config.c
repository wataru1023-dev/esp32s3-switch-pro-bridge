#include "device_config.h"
#include "esp_app_desc.h"
#include "freertos/semphr.h"
#include "nvs.h"

#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static pthread_mutex_t s_save_lock = PTHREAD_MUTEX_INITIALIZER;
static uint16_t s_disk_rate = 66, s_pending_rate;
static uint8_t s_disk_auto = 1, s_pending_auto;
static char s_disk_target[40], s_pending_target[40];
static bool s_target_exists, s_pending_target_exists;
static unsigned s_open_count;
static esp_err_t s_open_error = ESP_OK, s_set_error = ESP_OK, s_commit_error = ESP_OK;

SemaphoreHandle_t xSemaphoreCreateMutex(void) { return &s_save_lock; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t wait)
{
    assert(wait == portMAX_DELAY);
    return pthread_mutex_lock(mutex) == 0 ? pdTRUE : pdFALSE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t mutex)
{
    return pthread_mutex_unlock(mutex) == 0 ? pdTRUE : pdFALSE;
}

const esp_app_desc_t *esp_app_get_description(void)
{
    static const esp_app_desc_t description = {.version = "test-build-version"};
    return &description;
}

esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle)
{
    assert(strcmp(name, "bridge") == 0);
    s_open_count++;
    if (s_open_error != ESP_OK) {
        return s_open_error;
    }
    *handle = (unsigned)mode + 1;
    s_pending_rate = s_disk_rate;
    s_pending_auto = s_disk_auto;
    memcpy(s_pending_target, s_disk_target, sizeof(s_disk_target));
    s_pending_target_exists = s_target_exists;
    return ESP_OK;
}
esp_err_t nvs_get_u16(nvs_handle_t handle, const char *key, uint16_t *value)
{
    assert(handle == 1 && strcmp(key, "rate_hz") == 0);
    *value = s_disk_rate;
    return ESP_OK;
}
esp_err_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *value)
{
    assert(handle == 1 && strcmp(key, "ble_auto") == 0);
    *value = s_disk_auto;
    return ESP_OK;
}
esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *out, size_t *length)
{
    assert(handle == 1 && strcmp(key, "ble_target") == 0);
    if (!s_target_exists) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    size_t needed = strlen(s_disk_target) + 1;
    if (*length < needed) {
        *length = needed;
        return ESP_ERR_NVS_INVALID_LENGTH;
    }
    memcpy(out, s_disk_target, needed);
    *length = needed;
    return ESP_OK;
}
esp_err_t nvs_set_u16(nvs_handle_t handle, const char *key, uint16_t value)
{
    assert(handle == 2 && strcmp(key, "rate_hz") == 0);
    if (s_set_error == ESP_OK) {
        s_pending_rate = value;
    }
    return s_set_error;
}
esp_err_t nvs_set_u8(nvs_handle_t handle, const char *key, uint8_t value)
{
    assert(handle == 2 && strcmp(key, "ble_auto") == 0);
    if (s_set_error == ESP_OK) {
        s_pending_auto = value;
    }
    return s_set_error;
}
esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value)
{
    assert(handle == 2 && strcmp(key, "ble_target") == 0);
    assert(strlen(value) < sizeof(s_pending_target));
    if (s_set_error == ESP_OK) {
        strcpy(s_pending_target, value);
        s_pending_target_exists = true;
    }
    return s_set_error;
}
esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key)
{
    assert(handle == 2 && strcmp(key, "ble_target") == 0);
    if (s_set_error != ESP_OK) {
        return s_set_error;
    }
    s_pending_target[0] = 0;
    if (!s_pending_target_exists) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    s_pending_target_exists = false;
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle)
{
    assert(handle == 2);
    if (s_commit_error == ESP_OK) {
        s_disk_rate = s_pending_rate;
        s_disk_auto = s_pending_auto;
        memcpy(s_disk_target, s_pending_target, sizeof(s_disk_target));
        s_target_exists = s_pending_target_exists;
    }
    return s_commit_error;
}
void nvs_close(nvs_handle_t handle) { assert(handle == 1 || handle == 2); }

static void assert_target(const char *expected)
{
    char copy[40];
    device_config_copy_ble_target(copy, sizeof(copy));
    assert(strcmp(copy, expected) == 0);
}

static void test_defaults_and_save_failures(void)
{
    device_config_init();
    assert(device_config_bridge_running());
    assert(device_config_get_hid_test_mode() == HID_TEST_NEUTRAL);
    assert(device_config_get_report_rate_hz() == 66);
    assert(device_config_get_ble_autoconnect());
    assert_target("");
    assert(strcmp(device_config_get_version(), "test-build-version") == 0);

    assert(device_config_save_report_rate_hz(1) == ESP_OK);
    assert(device_config_get_report_rate_hz() == 20 && s_disk_rate == 20);
    assert(device_config_save_report_rate_hz(5000) == ESP_OK);
    assert(device_config_get_report_rate_hz() == 1000 && s_disk_rate == 1000);
    assert(device_config_save_report_rate_hz(120) == ESP_OK);
    assert(device_config_save_ble_autoconnect(false) == ESP_OK);
    assert(device_config_save_ble_target("12:34:56:78:9a:bc") == ESP_OK);
    const esp_err_t failures[] = {ESP_FAIL, ESP_ERR_NO_MEM};
    for (size_t i = 0; i < sizeof(failures) / sizeof(failures[0]); ++i) {
        for (unsigned stage = 0; stage < 3; ++stage) {
            s_open_error = stage == 0 ? failures[i] : ESP_OK;
            s_set_error = stage == 1 ? failures[i] : ESP_OK;
            s_commit_error = stage == 2 ? failures[i] : ESP_OK;
            assert(device_config_save_report_rate_hz(240) == failures[i]);
            assert(device_config_get_report_rate_hz() == 120);
            assert(device_config_save_ble_autoconnect(true) == failures[i]);
            assert(!device_config_get_ble_autoconnect());
            assert(device_config_save_ble_target("ff:ff:ff:ff:ff:ff") == failures[i]);
            assert_target("12:34:56:78:9a:bc");
        }
    }
    s_open_error = s_set_error = s_commit_error = ESP_OK;
    device_config_init();
    assert(device_config_get_report_rate_hz() == 120);
    assert(!device_config_get_ble_autoconnect());
    assert_target("12:34:56:78:9a:bc");
}

static void test_target_bounds_and_copy(void)
{
    char long_target[41];
    memset(long_target, 'a', sizeof(long_target));
    long_target[40] = 0;
    unsigned opens = s_open_count;
    assert(device_config_save_ble_target(long_target) == ESP_ERR_INVALID_ARG);
    assert(s_open_count == opens);
    assert_target("12:34:56:78:9a:bc");
    long_target[39] = 0;
    assert(device_config_save_ble_target(long_target) == ESP_OK);
    assert_target(long_target);
    char one = 'x';
    device_config_copy_ble_target(&one, 1);
    assert(one == 0);
    char guarded[6] = "xxxxx";
    device_config_copy_ble_target(guarded + 1, 3);
    assert(guarded[0] == 'x' && guarded[1] == 'a' && guarded[2] == 'a');
    assert(guarded[3] == 0 && guarded[4] == 'x');
    device_config_copy_ble_target(NULL, 40);
    device_config_copy_ble_target(guarded, 0);
    assert(guarded[0] == 'x');
    assert(device_config_save_ble_target(NULL) == ESP_OK);
    assert_target("");
    assert(device_config_save_ble_target("") == ESP_OK);
    assert_target("");
}

static atomic_bool s_done;
static const char *const s_target_a = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
static const char *const s_target_b = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

static void *target_writer(void *argument)
{
    const char *target = argument;
    for (unsigned i = 0; i < 2000; ++i) {
        assert(device_config_save_ble_target(target) == ESP_OK);
        assert(device_config_save_report_rate_hz(i % 2 ? 66 : 120) == ESP_OK);
        assert(device_config_save_ble_autoconnect((i % 2) != 0) == ESP_OK);
    }
    return NULL;
}

static void *target_reader(void *argument)
{
    (void)argument;
    char copy[40];
    while (!atomic_load(&s_done)) {
        device_config_copy_ble_target(copy, sizeof(copy));
        assert(strcmp(copy, s_target_a) == 0 || strcmp(copy, s_target_b) == 0);
        uint16_t rate = device_config_get_report_rate_hz();
        assert(rate == 66 || rate == 120);
        (void)device_config_get_ble_autoconnect();
        device_config_set_bridge_running(false);
        assert(!device_config_bridge_running());
        device_config_set_hid_test_mode(HID_TEST_A_HELD);
        assert(device_config_get_hid_test_mode() == HID_TEST_A_HELD);
    }
    return NULL;
}

static void test_concurrent_saves_and_snapshots(void)
{
    assert(device_config_save_ble_target(s_target_a) == ESP_OK);
    assert(device_config_save_report_rate_hz(66) == ESP_OK);
    pthread_t reader, writers[2];
    assert(pthread_create(&reader, NULL, target_reader, NULL) == 0);
    assert(pthread_create(&writers[0], NULL, target_writer, (void *)s_target_a) == 0);
    assert(pthread_create(&writers[1], NULL, target_writer, (void *)s_target_b) == 0);
    assert(pthread_join(writers[0], NULL) == 0);
    assert(pthread_join(writers[1], NULL) == 0);
    atomic_store(&s_done, true);
    assert(pthread_join(reader, NULL) == 0);
    assert_target(s_disk_target);
    assert(device_config_get_report_rate_hz() == s_disk_rate);
    assert(device_config_get_ble_autoconnect() == (s_disk_auto != 0));
}

int main(void)
{
    test_defaults_and_save_failures();
    test_target_bounds_and_copy();
    test_concurrent_saves_and_snapshots();
    puts("device config tests passed");
    return 0;
}
