#include <stdbool.h>
#include <stdatomic.h>
#include "esp_log.h"
#include "app_log.h"

static atomic_bool s_debug;

void app_log_init(void)
{
    atomic_store_explicit(&s_debug, false, memory_order_relaxed);
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("NimBLE", ESP_LOG_WARN);
}

void app_log_set_debug(bool enabled)
{
    atomic_store_explicit(&s_debug, enabled, memory_order_relaxed);
    esp_log_level_set("*", enabled ? ESP_LOG_DEBUG : ESP_LOG_INFO);
    esp_log_level_set("NimBLE", enabled ? ESP_LOG_DEBUG : ESP_LOG_WARN);
}

bool app_log_debug_enabled(void)
{
    return atomic_load_explicit(&s_debug, memory_order_relaxed);
}
