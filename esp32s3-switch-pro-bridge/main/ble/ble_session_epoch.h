#pragma once

#include <stdbool.h>
#include <stdint.h>

/* The generation disambiguates a connection handle reused after reconnect. */
typedef struct {
    uint32_t generation;
    uint16_t conn_handle;
    bool active;
} ble_session_epoch_t;

static inline void ble_session_epoch_invalidate(ble_session_epoch_t *epoch)
{
    if (++epoch->generation == 0) {
        ++epoch->generation;
    }
    epoch->active = false;
}

static inline void ble_session_epoch_activate(ble_session_epoch_t *epoch,
                                             uint16_t conn_handle)
{
    epoch->conn_handle = conn_handle;
    epoch->active = true;
}

static inline bool ble_session_epoch_matches(const ble_session_epoch_t *epoch,
                                             uint16_t conn_handle,
                                             uint32_t generation)
{
    return epoch->active && epoch->generation == generation &&
           epoch->conn_handle == conn_handle;
}
