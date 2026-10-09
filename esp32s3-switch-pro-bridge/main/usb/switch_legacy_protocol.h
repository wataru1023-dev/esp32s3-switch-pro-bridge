#pragma once

#include <stddef.h>
#include <stdint.h>

/* SPI reads may start inside a region or span multiple regions. Bytes outside
 * the virtual factory data, including absent user calibration, read as 0xff.
 * Returns min(requested_len, capacity), or zero for a null destination. */
size_t switch_legacy_spi_read(uint32_t address, uint8_t *dst,
                             size_t requested_len, size_t capacity);

/* Data-bearing subcommands use their protocol-specific ACK type. */
uint8_t switch_legacy_subcommand_ack(uint8_t subcommand);
