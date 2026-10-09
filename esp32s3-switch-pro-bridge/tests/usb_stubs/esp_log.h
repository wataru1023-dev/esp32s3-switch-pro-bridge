#pragma once

static inline void test_discard_log(const char *tag, const char *format, ...)
{
    (void)tag;
    (void)format;
}

#define ESP_LOGI(...) test_discard_log(__VA_ARGS__)
#define ESP_LOGW(...) test_discard_log(__VA_ARGS__)
#define ESP_LOGE(...) test_discard_log(__VA_ARGS__)
#define ESP_LOGD(...) test_discard_log(__VA_ARGS__)
