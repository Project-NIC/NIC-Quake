/*
 * Host-side test for nq-sensors auto-detection.
 *
 * No STM32, no hardware: each SPI bus is a mock that answers the way the real
 * chip would. This is the whole point of the hardware-agnostic driver layer —
 * edit a module, run it through the compiler, prove it on the PC, done.
 */

#include "nq_sensors.h"
#include "nq_icm42688.h"
#include "nq_adxl355.h"
#include "nq_scl3300.h"

#include <stdio.h>
#include <string.h>

/* ---- Mock SPI devices -------------------------------------------------- */

/* An ICM-42688-P: answers WHO_AM_I (read = reg|0x80) with 0x47. */
static int mock_icm_transfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len) {
    (void)ctx;
    if (len < 2 || rx == NULL || tx == NULL) return NIC_ERR;
    uint8_t reg = tx[0] & 0x7Fu;
    rx[1] = (reg == NQ_ICM42688_REG_WHO_AM_I) ? NQ_ICM42688_WHO_AM_I_VALUE : 0x00u;
    return NIC_OK;
}

/* An ADXL355: answers the three ID registers (read = (addr<<1)|1). */
static int mock_adxl_transfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len) {
    (void)ctx;
    if (len < 2 || rx == NULL || tx == NULL) return NIC_ERR;
    uint8_t reg = (uint8_t)(tx[0] >> 1);
    switch (reg) {
        case NQ_ADXL355_REG_DEVID_AD:  rx[1] = NQ_ADXL355_DEVID_AD_VALUE;  break;
        case NQ_ADXL355_REG_DEVID_MST: rx[1] = NQ_ADXL355_DEVID_MST_VALUE; break;
        case NQ_ADXL355_REG_PARTID:    rx[1] = NQ_ADXL355_PARTID_VALUE;    break;
        default:                       rx[1] = 0x00u;                      break;
    }
    return NIC_OK;
}

/* An SCL3300: pipelined 32-bit frames, WHOAMI -> 0xC1 with a valid CRC-8. */
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
typedef struct { uint32_t pending; } sclmock_t;
static int mock_scl_transfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len) {
    sclmock_t *m = ctx;
    if (len < 4 || rx == NULL || tx == NULL) return NIC_ERR;
    uint32_t cmd = ((uint32_t)tx[0] << 24) | ((uint32_t)tx[1] << 16)
                 | ((uint32_t)tx[2] << 8)  |  (uint32_t)tx[3];
    uint16_t data = (m->pending == NQ_SCL3300_CMD_WHOAMI) ? 0x00C1u : 0x0000u;
    uint32_t partial = (0x01u << 24) | ((uint32_t)data << 8);
    uint32_t resp = partial | ref_crc8(partial);
    rx[0] = (uint8_t)(resp >> 24); rx[1] = (uint8_t)(resp >> 16);
    rx[2] = (uint8_t)(resp >> 8);  rx[3] = (uint8_t)resp;
    m->pending = cmd;
    return NIC_OK;
}

/* An empty bus: reads float high. */
static int mock_absent_transfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len) {
    (void)ctx; (void)tx;
    if (len < 2 || rx == NULL) return NIC_ERR;
    for (size_t i = 0; i < len; i++) rx[i] = 0xFFu;
    return NIC_OK;
}

/* A dead bus: every transfer errors. */
static int mock_error_transfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len) {
    (void)ctx; (void)tx; (void)rx; (void)len;
    return NIC_ERR;
}

/* ---- Tiny assert harness ----------------------------------------------- */

static int g_failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { printf("  FAIL: %s (line %d)\n", #cond, __LINE__); g_failures++; } \
} while (0)

/* ---- Tests ------------------------------------------------------------- */

static void test_full_node(void) {
    printf("test_full_node: all three sensors populated\n");
    sclmock_t scl = {0};
    nq_sensors_buses_t buses = {
        .icm     = { .ctx = NULL, .transfer = mock_icm_transfer  },
        .adxl355 = { .ctx = NULL, .transfer = mock_adxl_transfer },
        .scl3300 = { .ctx = &scl, .transfer = mock_scl_transfer  },
    };
    nq_sensors_map_t map;
    nq_sensors_detect(&buses, &map);

    CHECK(map.present[NQ_SENSOR_ICM42688] == 1);
    CHECK(map.present[NQ_SENSOR_ADXL355]  == 1);
    CHECK(map.present[NQ_SENSOR_SCL3300]  == 1);
    CHECK(map.count == 3);
}

static void test_partial_node(void) {
    printf("test_partial_node: only the IMU populated, others absent/dead\n");
    nq_sensors_buses_t buses = {
        .icm     = { .ctx = NULL, .transfer = mock_icm_transfer    },
        .adxl355 = { .ctx = NULL, .transfer = mock_absent_transfer },
        .scl3300 = { .ctx = NULL, .transfer = mock_error_transfer  },
    };
    nq_sensors_map_t map;
    nq_sensors_detect(&buses, &map);

    CHECK(map.present[NQ_SENSOR_ICM42688] == 1);
    CHECK(map.present[NQ_SENSOR_ADXL355]  == 0);
    CHECK(map.present[NQ_SENSOR_SCL3300]  == 0);   /* bus error -> absent */
    CHECK(map.count == 1);
}

static void test_names(void) {
    printf("test_names: human-readable names\n");
    CHECK(strcmp(nq_sensor_name(NQ_SENSOR_ICM42688), "ICM-42688-P") == 0);
    CHECK(strcmp(nq_sensor_name(NQ_SENSOR_ADXL355),  "ADXL355") == 0);
    CHECK(strcmp(nq_sensor_name(NQ_SENSOR_SCL3300),  "SCL3300") == 0);
}

static void test_seismic_source_fallback(void) {
    printf("test_seismic_source_fallback: prefer ADXL355, fall back to ICM\n");
    nq_sensors_map_t m = {0};

    m.present[NQ_SENSOR_ADXL355] = 1; m.present[NQ_SENSOR_ICM42688] = 1;
    CHECK(nq_sensors_seismic_source(&m) == NQ_SENSOR_ADXL355);   /* the better one */

    m.present[NQ_SENSOR_ADXL355] = 0;
    CHECK(nq_sensors_seismic_source(&m) == NQ_SENSOR_ICM42688);  /* fall back */

    m.present[NQ_SENSOR_ICM42688] = 0;
    CHECK(nq_sensors_seismic_source(&m) == NQ_SENSOR_COUNT);     /* none */
}

int main(void) {
    test_full_node();
    test_partial_node();
    test_names();
    test_seismic_source_fallback();

    if (g_failures == 0) {
        printf("OK: all checks passed\n");
        return 0;
    }
    printf("FAILED: %d check(s)\n", g_failures);
    return 1;
}
