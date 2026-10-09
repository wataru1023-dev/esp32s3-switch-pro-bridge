#include "switch_legacy_protocol.h"

#include <string.h>

/* Axes sent to the host are already normalized to 0..4095, center 2048.
 * Left layout: positive extent, center, negative extent.
 * Right layout: center, negative extent, positive extent. */
static const uint8_t s_factory_sticks[] = {
    0xff, 0xf7, 0x7f, 0x00, 0x08, 0x80, 0x00, 0x08, 0x80,
    0x00, 0x08, 0x80, 0x00, 0x08, 0x80, 0xff, 0xf7, 0x7f,
};

/* Origins are zero. Accelerometer sensitivity: 16384 on each axis.
 * Gyroscope sensitivity: 13371 on each axis, matching the report format. */
static const uint8_t s_factory_imu[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x40, 0x00, 0x40, 0x00, 0x40,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x3b, 0x34, 0x3b, 0x34, 0x3b, 0x34,
};

static const uint8_t s_imu_horizontal_offset[6] = {0};

/* Six packed 12-bit pairs. The inner dead zone is the first value of the
 * SECOND pair (bytes 3..5). The host initializes noise/outer radius separately.
 * A 64-unit inner radius is small relative to the normalized 2048-unit range. */
static const uint8_t s_stick_parameters[] = {
    0x50, 0xf0, 0xff, /* unknown1=80, unknown2=4095 */
    0x40, 0x00, 0x00, /* innerDeadZone=64, unknown4=0 */
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
};

typedef struct {
    uint32_t address;
    const uint8_t *data;
    size_t length;
} virtual_spi_region_t;

static const virtual_spi_region_t s_regions[] = {
    {0x6020, s_factory_imu, sizeof(s_factory_imu)},
    {0x603d, s_factory_sticks, sizeof(s_factory_sticks)},
    {0x6080, s_imu_horizontal_offset, sizeof(s_imu_horizontal_offset)},
    {0x6086, s_stick_parameters, sizeof(s_stick_parameters)},
    {0x6098, s_stick_parameters, sizeof(s_stick_parameters)},
};

size_t switch_legacy_spi_read(uint32_t address, uint8_t *dst,
                             size_t requested_len, size_t capacity)
{
    if (!dst) {
        return 0;
    }

    const size_t length = requested_len < capacity ? requested_len : capacity;
    memset(dst, 0xff, length);
    const uint64_t read_start = address;
    /* Saturate rather than wrapping for a size_t length near SIZE_MAX. */
    const uint64_t read_end = length > UINT64_MAX - read_start
                                 ? UINT64_MAX
                                 : read_start + length;

    for (size_t i = 0; i < sizeof(s_regions) / sizeof(s_regions[0]); ++i) {
        const virtual_spi_region_t *region = &s_regions[i];
        const uint64_t region_start = region->address;
        const uint64_t region_end = region_start + region->length;
        const uint64_t copy_start = read_start > region_start ? read_start : region_start;
        const uint64_t copy_end = read_end < region_end ? read_end : region_end;
        if (copy_start < copy_end) {
            memcpy(dst + (size_t)(copy_start - read_start),
                   region->data + (size_t)(copy_start - region_start),
                   (size_t)(copy_end - copy_start));
        }
    }
    return length;
}

uint8_t switch_legacy_subcommand_ack(uint8_t subcommand)
{
    switch (subcommand) {
    case 0x02: /* Request device information. */
        return 0x82;
    case 0x10: /* Read SPI flash. */
        return 0x90;
    default:
        return 0x80;
    }
}
