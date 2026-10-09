#include "switch_legacy_protocol.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static uint16_t unpack_x(const uint8_t *pair)
{
    return (uint16_t)(pair[0] | ((pair[1] & 0x0f) << 8));
}

static uint16_t unpack_y(const uint8_t *pair)
{
    return (uint16_t)((pair[1] >> 4) | (pair[2] << 4));
}

static void assert_pair(const uint8_t *pair, uint16_t x, uint16_t y)
{
    assert(unpack_x(pair) == x);
    assert(unpack_y(pair) == y);
}

static void test_factory_layout(void)
{
    uint8_t data[18];
    assert(switch_legacy_spi_read(0x603d, data, sizeof(data), sizeof(data)) == 18);
    /* Left and right calibration use different orderings. Check both endpoints
     * against the full range actually sent by the firmware. */
    assert_pair(data, 2047, 2047);
    assert_pair(data + 3, 2048, 2048);
    assert_pair(data + 6, 2048, 2048);
    assert_pair(data + 9, 2048, 2048);
    assert_pair(data + 12, 2048, 2048);
    assert_pair(data + 15, 2047, 2047);
    assert(unpack_x(data + 3) - unpack_x(data + 6) == 0);
    assert(unpack_x(data + 3) + unpack_x(data) == 4095);
    assert(unpack_x(data + 9) - unpack_x(data + 12) == 0);
    assert(unpack_x(data + 9) + unpack_x(data + 15) == 4095);
}

static void test_offset_and_crossing_reads(void)
{
    uint8_t full[128];
    uint8_t slice[32];
    assert(switch_legacy_spi_read(0x601f, full, sizeof(full), sizeof(full)) == sizeof(full));
    assert(full[0] == 0xff);
    assert(full[7] == 0x00 && full[8] == 0x40); /* IMU sensitivity at 0x6026. */
    for (size_t i = 0x6038 - 0x601f; i < 0x603d - 0x601f; ++i) {
        assert(full[i] == 0xff);
    }
    assert_pair(full + (0x603d - 0x601f), 2047, 2047);
    assert(switch_legacy_spi_read(0x6040, slice, 12, sizeof(slice)) == 12);
    assert(memcmp(slice, full + (0x6040 - 0x601f), 12) == 0);

    /* Offset + parameter boundary: offset zeros, left params, then right params. */
    assert(switch_legacy_spi_read(0x6084, slice, sizeof(slice), sizeof(slice)) == sizeof(slice));
    assert(slice[0] == 0 && slice[1] == 0);
    assert_pair(slice + 2, 80, 4095);
    assert_pair(slice + 5, 64, 0);
    assert_pair(slice + 20, 80, 4095);
    assert_pair(slice + 23, 64, 0);
}

static void test_parameters_and_absent_user_calibration(void)
{
    uint8_t data[48];
    for (uint32_t address = 0x6086; address <= 0x6098; address += 18) {
        assert(switch_legacy_spi_read(address, data, 18, sizeof(data)) == 18);
        assert_pair(data, 80, 4095);
        /* The driver reads inner dead zone from the second pair. */
        assert_pair(data + 3, 64, 0);
    }
    assert(switch_legacy_spi_read(0x8010, data, sizeof(data), sizeof(data)) == sizeof(data));
    for (size_t i = 0; i < sizeof(data); ++i) {
        assert(data[i] == 0xff);
    }
    /* Both user stick magic words must be absent; otherwise the host would
     * prefer nonexistent user data over the valid factory calibration. */
    assert(!(data[0] == 0xb2 && data[1] == 0xa1));
    assert(!(data[11] == 0xb2 && data[12] == 0xa1));
    assert(!(data[22] == 0xb2 && data[23] == 0xa1));
}

static void test_capacity_and_address_limits(void)
{
    uint8_t guarded[8] = {0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa};
    assert(switch_legacy_spi_read(0x603d, guarded + 1, SIZE_MAX, 6) == 6);
    assert(guarded[0] == 0xaa && guarded[7] == 0xaa);
    assert_pair(guarded + 1, 2047, 2047);
    assert_pair(guarded + 4, 2048, 2048);
    memset(guarded, 0xaa, sizeof(guarded));
    assert(switch_legacy_spi_read(0x603d, guarded, 0, sizeof(guarded)) == 0);
    assert(switch_legacy_spi_read(0x603d, guarded, 18, 0) == 0);
    assert(guarded[0] == 0xaa);
    assert(switch_legacy_spi_read(0x603d, NULL, 18, 18) == 0);
    assert(switch_legacy_spi_read(UINT32_MAX - 1, guarded, sizeof(guarded), sizeof(guarded)) == sizeof(guarded));
    for (size_t i = 0; i < sizeof(guarded); ++i) {
        assert(guarded[i] == 0xff);
    }
    memset(guarded, 0xaa, sizeof(guarded));
    assert(switch_legacy_spi_read(0x603d, guarded, 3, sizeof(guarded)) == 3);
    assert(guarded[3] == 0xaa);
}

static void test_ack_types(void)
{
    assert(switch_legacy_subcommand_ack(0x02) == 0x82);
    assert(switch_legacy_subcommand_ack(0x10) == 0x90);
    assert(switch_legacy_subcommand_ack(0x03) == 0x80);
    assert(switch_legacy_subcommand_ack(0x40) == 0x80);
    assert(switch_legacy_subcommand_ack(0x48) == 0x80);
}

int main(void)
{
    test_factory_layout();
    test_offset_and_crossing_reads();
    test_parameters_and_absent_user_calibration();
    test_capacity_and_address_limits();
    test_ack_types();
    puts("Switch legacy calibration/ACK tests passed");
    return 0;
}
