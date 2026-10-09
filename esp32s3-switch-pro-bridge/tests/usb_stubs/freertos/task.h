#pragma once
#include "FreeRTOS.h"
#define pdPASS 1
#define pdMS_TO_TICKS(ms) (ms)
typedef void (*TaskFunction_t)(void *);
BaseType_t xTaskCreate(TaskFunction_t function, const char *name, uint32_t stack,
                       void *arg, UBaseType_t priority, void *handle);
void vTaskDelay(TickType_t ticks);
