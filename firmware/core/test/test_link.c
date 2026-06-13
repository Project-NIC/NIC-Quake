/*
 * Host-side test for nq-link: the lean data frame and the control frame.
 */

#include "nic_link.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { printf("  FAIL: %s (line %d)\n", #cond, __LINE__); g_failures++; } \
} while (0)

static void test_data_roundtrip(void) {
    printf("test_data_roundtrip\n");
    const uint8_t payload[] = { 0x10, 0x20, 0x30, 0x40, 0x55 };
    uint8_t buf[64];

    int n = nic_link_data_encode(0x07, 0x1234, payload, sizeof payload, buf, sizeof buf);
    CHECK(n == (int)(NIC_LINK_DATA_OVERHEAD + sizeof payload));
    CHECK(buf[0] == NIC_LINK_MAGIC_DATA);

    nic_link_data_t f;
    CHECK(nic_link_data_decode(buf, (size_t)n, sizeof payload, &f) == NIC_OK);
    CHECK(f.station == 0x07 && f.index == 0x1234 && f.len == sizeof payload);
    CHECK(f.payload && memcmp(f.payload, payload, sizeof payload) == 0);
}

static void test_data_crc_and_format(void) {
    printf("test_data_crc_and_format\n");
    const uint8_t payload[] = { 1, 2, 3 };
    uint8_t buf[32];
    int n = nic_link_data_encode(0x02, 0x00FF, payload, sizeof payload, buf, sizeof buf);

    nic_link_data_t f;
    buf[NIC_LINK_DATA_HEADER] ^= 0x01u;                       /* corrupt payload */
    CHECK(nic_link_data_decode(buf, (size_t)n, sizeof payload, &f) == NIC_ERR_CRC);
    buf[NIC_LINK_DATA_HEADER] ^= 0x01u;
    CHECK(nic_link_data_decode(buf, (size_t)n, sizeof payload, &f) == NIC_OK);

    buf[0] = 0x00u;                                          /* wrong magic */
    CHECK(nic_link_data_decode(buf, (size_t)n, sizeof payload, &f) == NIC_ERR_FORMAT);
    buf[0] = NIC_LINK_MAGIC_DATA;
    CHECK(nic_link_data_decode(buf, (size_t)n - 1, sizeof payload, &f) == NIC_ERR_FORMAT); /* short */

    /* the data magic must not parse as a control frame */
    nic_link_ctrl_t c;
    CHECK(nic_link_ctrl_decode(buf, (size_t)n, &c) == NIC_ERR_FORMAT);
}

static void test_data_capacity_guard(void) {
    printf("test_data_capacity_guard\n");
    const uint8_t payload[] = { 1, 2, 3, 4 };
    uint8_t small[NIC_LINK_DATA_OVERHEAD + 2];
    CHECK(nic_link_data_encode(1, 1, payload, sizeof payload, small, sizeof small) == NIC_ERR);
}

static void test_ctrl_roundtrip(void) {
    printf("test_ctrl_roundtrip\n");
    uint8_t buf[NIC_LINK_CTRL_LEN];
    int n = nic_link_ctrl_encode(NIC_LINK_BROADCAST, 0x12, 0xAB, buf, sizeof buf);
    CHECK(n == (int)NIC_LINK_CTRL_LEN);
    CHECK(buf[0] == NIC_LINK_MAGIC_CTRL);

    nic_link_ctrl_t c;
    CHECK(nic_link_ctrl_decode(buf, (size_t)n, &c) == NIC_OK);
    CHECK(c.station == NIC_LINK_BROADCAST && c.attribute == 0x12 && c.value == 0xAB);
}

static void test_ctrl_crc_and_format(void) {
    printf("test_ctrl_crc_and_format\n");
    uint8_t buf[NIC_LINK_CTRL_LEN];
    nic_link_ctrl_encode(0x03, 0x04, 0x05, buf, sizeof buf);

    nic_link_ctrl_t c;
    buf[2] ^= 0x01u;                                         /* corrupt attribute */
    CHECK(nic_link_ctrl_decode(buf, NIC_LINK_CTRL_LEN, &c) == NIC_ERR_CRC);
    buf[2] ^= 0x01u;
    CHECK(nic_link_ctrl_decode(buf, NIC_LINK_CTRL_LEN, &c) == NIC_OK);

    buf[0] = 0x00u;                                          /* wrong magic */
    CHECK(nic_link_ctrl_decode(buf, NIC_LINK_CTRL_LEN, &c) == NIC_ERR_FORMAT);
    CHECK(nic_link_ctrl_decode(buf, 3, &c) == NIC_ERR_FORMAT); /* short */
}

int main(void) {
    test_data_roundtrip();
    test_data_crc_and_format();
    test_data_capacity_guard();
    test_ctrl_roundtrip();
    test_ctrl_crc_and_format();

    if (g_failures == 0) {
        printf("OK: all checks passed\n");
        return 0;
    }
    printf("FAILED: %d check(s)\n", g_failures);
    return 1;
}
