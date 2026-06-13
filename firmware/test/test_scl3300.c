/*
 * Host-side test for nq-scl3300. A pipelined mock emulates the SCL3300: each
 * 32-bit response answers the PREVIOUS command and carries a valid CRC-8.
 */

#include "nq_scl3300.h"

#include <stdio.h>

/* Same CRC as the driver, so the mock builds frames the driver will accept. */
static uint8_t ref_crc8(uint32_t data) {
    uint8_t crc = 0xFFu;
    for (int b = 31; b >= 8; b--) {
        uint8_t bit = (uint8_t)((data >> b) & 1u);
        uint8_t msb = (uint8_t)(crc & 0x80u);
        if (bit) msb ^= 0x80u;
        crc = (uint8_t)(crc << 1);
        if (msb) crc ^= 0x1Du;
    }
    return (uint8_t)(~crc);
}

static uint16_t data_for(uint32_t cmd) {
    switch (cmd) {
        case NQ_SCL3300_CMD_WHOAMI:     return 0x00C1u;
        case NQ_SCL3300_CMD_READ_ACC_X: return 0x1234u;
        case NQ_SCL3300_CMD_READ_ACC_Y: return 0x5678u;
        case NQ_SCL3300_CMD_READ_ACC_Z: return 0xF000u;   /* -4096 */
        case NQ_SCL3300_CMD_READ_ANG_X: return 0x2000u;
        case NQ_SCL3300_CMD_READ_TEMP:  return 0x0ABCu;
        default:                        return 0x0000u;
    }
}

static uint32_t build_frame(uint32_t cmd, int corrupt) {
    uint32_t partial = (0x01u << 24) | ((uint32_t)data_for(cmd) << 8);  /* RS=01 */
    uint8_t crc = ref_crc8(partial);
    if (corrupt) crc ^= 0xFFu;
    return partial | crc;
}

typedef struct { uint32_t pending; int corrupt; } sclmock_t;

static int scl_mock(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len) {
    sclmock_t *m = ctx;
    if (len < 4) return NIC_ERR;
    uint32_t cmd = ((uint32_t)tx[0] << 24) | ((uint32_t)tx[1] << 16)
                 | ((uint32_t)tx[2] << 8)  |  (uint32_t)tx[3];
    uint32_t resp = build_frame(m->pending, m->corrupt);   /* answer the PREVIOUS cmd */
    rx[0] = (uint8_t)(resp >> 24); rx[1] = (uint8_t)(resp >> 16);
    rx[2] = (uint8_t)(resp >> 8);  rx[3] = (uint8_t)resp;
    m->pending = cmd;
    return NIC_OK;
}

static int g_failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { printf("  FAIL: %s (line %d)\n", #cond, __LINE__); g_failures++; } \
} while (0)

static void test_probe(void) {
    printf("test_probe\n");
    sclmock_t m = {0};
    nic_spi_t bus = { .ctx = &m, .transfer = scl_mock };
    CHECK(nq_scl3300_probe(&bus) == 1);
}

static void test_read_acc(void) {
    printf("test_read_acc\n");
    sclmock_t m = {0};
    nic_spi_t bus = { .ctx = &m, .transfer = scl_mock };
    nic_sample3_t s;
    CHECK(nq_scl3300_read_acc(&bus, &s) == NIC_OK);
    CHECK(s.axis[0] == 0x1234);
    CHECK(s.axis[1] == 0x5678);
    CHECK(s.axis[2] == -4096);          /* 0xF000 sign-extended */
}

static void test_read_temp(void) {
    printf("test_read_temp\n");
    sclmock_t m = {0};
    nic_spi_t bus = { .ctx = &m, .transfer = scl_mock };
    int16_t t = 0;
    CHECK(nq_scl3300_read_temp(&bus, &t) == NIC_OK);
    CHECK(t == 0x0ABC);
}

/* A configurable-gravity mock for the auto-mode test. */
static int16_t g_grav[3];
static uint16_t auto_data_for(uint32_t cmd) {
    switch (cmd) {
        case NQ_SCL3300_CMD_WHOAMI:     return 0x00C1u;
        case NQ_SCL3300_CMD_READ_ACC_X: return (uint16_t)g_grav[0];
        case NQ_SCL3300_CMD_READ_ACC_Y: return (uint16_t)g_grav[1];
        case NQ_SCL3300_CMD_READ_ACC_Z: return (uint16_t)g_grav[2];
        default:                        return 0x0000u;
    }
}
static int auto_mock(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len) {
    sclmock_t *m = ctx;
    if (len < 4) return NIC_ERR;
    uint32_t cmd = ((uint32_t)tx[0] << 24) | ((uint32_t)tx[1] << 16)
                 | ((uint32_t)tx[2] << 8)  |  (uint32_t)tx[3];
    uint32_t partial = (0x01u << 24) | ((uint32_t)auto_data_for(m->pending) << 8);
    uint32_t resp = partial | ref_crc8(partial);
    rx[0] = (uint8_t)(resp >> 24); rx[1] = (uint8_t)(resp >> 16);
    rx[2] = (uint8_t)(resp >> 8);  rx[3] = (uint8_t)resp;
    m->pending = cmd;
    return NIC_OK;
}

static void test_init_auto(void) {
    printf("test_init_auto\n");
    int near;

    /* near level: tiny horizontal component, ~1g on Z -> Mode 4 */
    sclmock_t m1 = {0};
    nic_spi_t b1 = { .ctx = &m1, .transfer = auto_mock };
    g_grav[0] = 100; g_grav[1] = 100; g_grav[2] = 16000;
    near = -1;
    CHECK(nq_scl3300_init_auto(&b1, &near) == NIC_OK);
    CHECK(near == 1);

    /* ~45° tilt: large horizontal component -> stay in Mode 1 */
    sclmock_t m2 = {0};
    nic_spi_t b2 = { .ctx = &m2, .transfer = auto_mock };
    g_grav[0] = 16000; g_grav[1] = 0; g_grav[2] = 16000;
    near = -1;
    CHECK(nq_scl3300_init_auto(&b2, &near) == NIC_OK);
    CHECK(near == 0);
}

static void test_crc_rejected(void) {
    printf("test_crc_rejected\n");
    sclmock_t m = { .corrupt = 1 };
    nic_spi_t bus = { .ctx = &m, .transfer = scl_mock };
    int16_t id = 0;
    /* probe reads WHOAMI -> a corrupt CRC must surface as NIC_ERR_CRC */
    CHECK(nq_scl3300_probe(&bus) == NIC_ERR_CRC);
    (void)id;
}

int main(void) {
    test_probe();
    test_read_acc();
    test_read_temp();
    test_init_auto();
    test_crc_rejected();

    if (g_failures == 0) {
        printf("OK: all checks passed\n");
        return 0;
    }
    printf("FAILED: %d check(s)\n", g_failures);
    return 1;
}
