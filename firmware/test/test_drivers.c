/*
 * Host-side test for the sensor driver init/read paths.
 *
 * A recording mock SPI captures the register writes an init() issues, and
 * replays canned data-register bytes for read() so the bit assembly and
 * sign-extension (20-bit ADXL, 16-bit ICM) can be checked without hardware.
 */

#include "nq_icm42688.h"
#include "nq_adxl355.h"

#include <stdio.h>

typedef struct {
    uint8_t wr_reg[32];
    uint8_t wr_val[32];
    int     wr_n;
    uint8_t rd[16];          /* bytes returned after the address byte on a read */
} mock_t;

static int g_failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { printf("  FAIL: %s (line %d)\n", #cond, __LINE__); g_failures++; } \
} while (0)

/* ADXL: byte0 = (reg<<1)|RW, RW=1 read. */
static int adxl_mock(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len) {
    mock_t *m = ctx;
    if (tx[0] & 0x01u) {                         /* read */
        for (size_t i = 1; i < len && i - 1 < sizeof m->rd; i++) rx[i] = m->rd[i - 1];
    } else {                                     /* write */
        m->wr_reg[m->wr_n] = (uint8_t)(tx[0] >> 1);
        m->wr_val[m->wr_n] = tx[1];
        m->wr_n++;
    }
    return NIC_OK;
}

/* ICM: byte0 = reg, bit7=1 read. */
static int icm_mock(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len) {
    mock_t *m = ctx;
    if (tx[0] & 0x80u) {                         /* read */
        for (size_t i = 1; i < len && i - 1 < sizeof m->rd; i++) rx[i] = m->rd[i - 1];
    } else {                                     /* write */
        m->wr_reg[m->wr_n] = tx[0];
        m->wr_val[m->wr_n] = tx[1];
        m->wr_n++;
    }
    return NIC_OK;
}

static void test_adxl_init_sequence(void) {
    printf("test_adxl_init_sequence\n");
    mock_t m = {0};
    nic_spi_t bus = { .ctx = &m, .transfer = adxl_mock };
    CHECK(nq_adxl355_init(&bus, NQ_ADXL355_RANGE_1) == NIC_OK);

    /* config first, measurement mode last */
    CHECK(m.wr_n == 4);
    CHECK(m.wr_reg[0] == NQ_ADXL355_REG_FILTER    && m.wr_val[0] == NQ_ADXL355_FILTER_ODR_125);
    CHECK(m.wr_reg[1] == NQ_ADXL355_REG_SYNC      && m.wr_val[1] == NQ_ADXL355_SYNC_EXT_CLK);
    CHECK(m.wr_reg[2] == NQ_ADXL355_REG_RANGE     && m.wr_val[2] == NQ_ADXL355_RANGE_1);
    CHECK(m.wr_reg[3] == NQ_ADXL355_REG_POWER_CTL && m.wr_val[3] == NQ_ADXL355_POWER_CTL_MEASURE);
}

static void test_adxl_read_assembly(void) {
    printf("test_adxl_read_assembly\n");
    mock_t m = {0};
    /* X = +0x12345, Y = 0xF0000 (negative), Z = +1 */
    uint8_t data[9] = {
        0x12, 0x34, 0x50,   /* X: (0x12<<12)|(0x34<<4)|(0x50>>4) = 0x12345 */
        0xF0, 0x00, 0x00,   /* Y: 0xF0000 -> -65536 after sign extend */
        0x00, 0x00, 0x10,   /* Z: 0x00001 */
    };
    for (int i = 0; i < 9; i++) m.rd[i] = data[i];
    nic_spi_t bus = { .ctx = &m, .transfer = adxl_mock };

    nic_sample3_t s;
    CHECK(nq_adxl355_read(&bus, &s) == NIC_OK);
    CHECK(s.axis[0] == 0x12345);
    CHECK(s.axis[1] == -65536);
    CHECK(s.axis[2] == 1);
}

static void test_icm_init_sequence(void) {
    printf("test_icm_init_sequence\n");
    mock_t m = {0};
    nic_spi_t bus = { .ctx = &m, .transfer = icm_mock };
    CHECK(nq_icm42688_init(&bus, NQ_ICM42688_ACCEL_2G_100HZ) == NIC_OK);

    CHECK(m.wr_n == 9);
    /* bank1 -> CLKIN -> bank0 */
    CHECK(m.wr_reg[0] == NQ_ICM42688_REG_BANK_SEL     && m.wr_val[0] == NQ_ICM42688_BANK1);
    CHECK(m.wr_reg[1] == NQ_ICM42688_REG_INTF_CONFIG5 && m.wr_val[1] == NQ_ICM42688_PIN9_CLKIN);
    CHECK(m.wr_reg[2] == NQ_ICM42688_REG_BANK_SEL     && m.wr_val[2] == NQ_ICM42688_BANK0);
    CHECK(m.wr_reg[3] == NQ_ICM42688_REG_INTF_CONFIG1 && m.wr_val[3] == NQ_ICM42688_INTF_CONFIG1_RTC);
    CHECK(m.wr_reg[4] == NQ_ICM42688_REG_GYRO_CONFIG0 && m.wr_val[4] == NQ_ICM42688_GYRO_15_625DPS_100HZ);
    CHECK(m.wr_reg[5] == NQ_ICM42688_REG_ACCEL_CONFIG0&& m.wr_val[5] == NQ_ICM42688_ACCEL_2G_100HZ);
    /* power-up is the last write */
    CHECK(m.wr_reg[8] == NQ_ICM42688_REG_PWR_MGMT0    && m.wr_val[8] == NQ_ICM42688_PWR_ACCEL_GYRO_LN);
}

static void test_icm_read_assembly(void) {
    printf("test_icm_read_assembly\n");
    mock_t m = {0};
    /* accel X=0x1234, Y=0xFFFF(-1), Z=0x8000(-32768); gyro X=1, Y=0x7FFF, Z=0xFF00(-256) */
    uint8_t data[12] = {
        0x12, 0x34,  0xFF, 0xFF,  0x80, 0x00,
        0x00, 0x01,  0x7F, 0xFF,  0xFF, 0x00,
    };
    for (int i = 0; i < 12; i++) m.rd[i] = data[i];
    nic_spi_t bus = { .ctx = &m, .transfer = icm_mock };

    nic_sample3_t a, g;
    CHECK(nq_icm42688_read(&bus, &a, &g) == NIC_OK);
    CHECK(a.axis[0] == 0x1234 && a.axis[1] == -1 && a.axis[2] == -32768);
    CHECK(g.axis[0] == 1 && g.axis[1] == 32767 && g.axis[2] == -256);
}

static void test_icm_read_skips_null(void) {
    printf("test_icm_read_skips_null\n");
    mock_t m = {0};
    for (int i = 0; i < 12; i++) m.rd[i] = 0x11;
    nic_spi_t bus = { .ctx = &m, .transfer = icm_mock };
    nic_sample3_t a;
    CHECK(nq_icm42688_read(&bus, &a, NULL) == NIC_OK);   /* gyro NULL -> no crash */
    CHECK(a.axis[0] == 0x1111);
}

static void test_icm_read_temp(void) {
    printf("test_icm_read_temp\n");
    mock_t m = {0};
    nic_spi_t bus = { .ctx = &m, .transfer = icm_mock };
    int16_t t = 0;
    m.rd[0] = 0x0A; m.rd[1] = 0xBC;                     /* TEMP_DATA = 0x0ABC */
    CHECK(nq_icm42688_read_temp(&bus, &t) == NIC_OK);
    CHECK(t == 0x0ABC);
    m.rd[0] = 0xFF; m.rd[1] = 0x00;                     /* 0xFF00 -> -256 (signed) */
    CHECK(nq_icm42688_read_temp(&bus, &t) == NIC_OK);
    CHECK(t == -256);
}

static void test_adxl_read_temp(void) {
    printf("test_adxl_read_temp\n");
    mock_t m = {0};
    nic_spi_t bus = { .ctx = &m, .transfer = adxl_mock };
    int16_t t = 0;
    /* TEMP2=0x07, TEMP1=0x3C -> 0x73C = 1852 (≈25°C nominal). */
    m.rd[0] = 0x07; m.rd[1] = 0x3C;
    CHECK(nq_adxl355_read_temp(&bus, &t) == NIC_OK);
    CHECK(t == 0x73C);
    /* the upper nibble of TEMP2 is undefined and must be masked off. */
    m.rd[0] = 0xF7; m.rd[1] = 0x3C;
    CHECK(nq_adxl355_read_temp(&bus, &t) == NIC_OK);
    CHECK(t == 0x73C);
}

static void test_adxl_set_range(void) {
    printf("test_adxl_set_range\n");
    mock_t m = {0};
    nic_spi_t bus = { .ctx = &m, .transfer = adxl_mock };
    CHECK(nq_adxl355_set_range(&bus, NQ_ADXL355_RANGE_2) == NIC_OK);
    /* standby -> write RANGE -> measure; no full re-init (filter/sync untouched) */
    CHECK(m.wr_n == 3);
    CHECK(m.wr_reg[0] == NQ_ADXL355_REG_POWER_CTL && m.wr_val[0] == NQ_ADXL355_POWER_CTL_STANDBY);
    CHECK(m.wr_reg[1] == NQ_ADXL355_REG_RANGE     && m.wr_val[1] == NQ_ADXL355_RANGE_2);
    CHECK(m.wr_reg[2] == NQ_ADXL355_REG_POWER_CTL && m.wr_val[2] == NQ_ADXL355_POWER_CTL_MEASURE);
}

static void test_icm_set_ranges(void) {
    printf("test_icm_set_ranges\n");
    mock_t m = {0};
    nic_spi_t bus = { .ctx = &m, .transfer = icm_mock };
    CHECK(nq_icm42688_set_accel_range(&bus, NQ_ICM42688_ACCEL_16G_100HZ) == NIC_OK);
    CHECK(m.wr_n == 1);                                  /* one register, not a re-init */
    CHECK(m.wr_reg[0] == NQ_ICM42688_REG_ACCEL_CONFIG0 && m.wr_val[0] == NQ_ICM42688_ACCEL_16G_100HZ);
    /* ODR field (low nibble) preserved -> still 125 Hz phase-lock */
    CHECK((m.wr_val[0] & 0x0Fu) == (NQ_ICM42688_ACCEL_2G_100HZ & 0x0Fu));

    mock_t m2 = {0};
    nic_spi_t bus2 = { .ctx = &m2, .transfer = icm_mock };
    CHECK(nq_icm42688_set_gyro_range(&bus2, NQ_ICM42688_GYRO_31_25DPS_100HZ) == NIC_OK);
    CHECK(m2.wr_n == 1);
    CHECK(m2.wr_reg[0] == NQ_ICM42688_REG_GYRO_CONFIG0 && m2.wr_val[0] == NQ_ICM42688_GYRO_31_25DPS_100HZ);
}

int main(void) {
    test_adxl_init_sequence();
    test_adxl_read_assembly();
    test_adxl_read_temp();
    test_adxl_set_range();
    test_icm_init_sequence();
    test_icm_read_assembly();
    test_icm_read_skips_null();
    test_icm_read_temp();
    test_icm_set_ranges();

    if (g_failures == 0) {
        printf("OK: all checks passed\n");
        return 0;
    }
    printf("FAILED: %d check(s)\n", g_failures);
    return 1;
}
