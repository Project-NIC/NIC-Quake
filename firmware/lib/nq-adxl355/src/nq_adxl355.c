#include "nq_adxl355.h"

/*
 * ADXL355 SPI read: byte 0 is (register address << 1) with the LSB set to mark
 * a read; the device shifts the value out on the following byte.
 */
static int adxl_read_reg(const nic_spi_t *bus, uint8_t reg, uint8_t *val) {
    uint8_t tx[2] = { (uint8_t)((reg << 1) | 0x01u), 0x00u };
    uint8_t rx[2] = { 0u, 0u };
    int r = nic_spi_transfer(bus, tx, rx, sizeof tx);
    if (r != NIC_OK) {
        return r;
    }
    *val = rx[1];
    return NIC_OK;
}

int nq_adxl355_probe(const nic_spi_t *bus) {
    uint8_t ad = 0u, mst = 0u, part = 0u;
    int r;

    if ((r = adxl_read_reg(bus, NQ_ADXL355_REG_DEVID_AD,  &ad))   != NIC_OK) return r;
    if ((r = adxl_read_reg(bus, NQ_ADXL355_REG_DEVID_MST, &mst))  != NIC_OK) return r;
    if ((r = adxl_read_reg(bus, NQ_ADXL355_REG_PARTID,    &part)) != NIC_OK) return r;

    return (ad   == NQ_ADXL355_DEVID_AD_VALUE &&
            mst  == NQ_ADXL355_DEVID_MST_VALUE &&
            part == NQ_ADXL355_PARTID_VALUE) ? 1 : 0;
}

/* Write: byte 0 = (register address << 1), LSB clear marks a write. */
static int adxl_write_reg(const nic_spi_t *bus, uint8_t reg, uint8_t val) {
    uint8_t tx[2] = { (uint8_t)((reg << 1) & 0xFEu), val };
    return nic_spi_transfer(bus, tx, NULL, sizeof tx);
}

int nq_adxl355_init(const nic_spi_t *bus, uint8_t range_code) {
    int r;
    /* Program all configuration before enabling measurement mode (datasheet). */
    if ((r = adxl_write_reg(bus, NQ_ADXL355_REG_FILTER, NQ_ADXL355_FILTER_ODR_125)) != NIC_OK) return r;
    if ((r = adxl_write_reg(bus, NQ_ADXL355_REG_SYNC,   NQ_ADXL355_SYNC_EXT_CLK))   != NIC_OK) return r;
    if ((r = adxl_write_reg(bus, NQ_ADXL355_REG_RANGE,  range_code))                != NIC_OK) return r;
    if ((r = adxl_write_reg(bus, NQ_ADXL355_REG_POWER_CTL, NQ_ADXL355_POWER_CTL_MEASURE)) != NIC_OK) return r;
    return NIC_OK;
}

int nq_adxl355_read(const nic_spi_t *bus, nic_sample3_t *out) {
    /* Burst read XDATA3..ZDATA1 (9 bytes), the device auto-increments. */
    uint8_t tx[10] = { (uint8_t)((NQ_ADXL355_REG_XDATA3 << 1) | 0x01u) };
    uint8_t rx[10] = { 0 };
    int r = nic_spi_transfer(bus, tx, rx, sizeof tx);
    if (r != NIC_OK) {
        return r;
    }

    for (int i = 0; i < 3; i++) {
        uint8_t b3 = rx[1 + i*3];   /* bits [19:12] */
        uint8_t b2 = rx[2 + i*3];   /* bits [11:4]  */
        uint8_t b1 = rx[3 + i*3];   /* bits [3:0] in the high nibble */
        int32_t v = ((int32_t)b3 << 12) | ((int32_t)b2 << 4) | (b1 >> 4);
        if (v & 0x80000) {          /* sign-extend from bit 19 */
            v -= 0x100000;
        }
        out->axis[i] = v;
    }
    return NIC_OK;
}

int nq_adxl355_read_temp(const nic_spi_t *bus, int16_t *out) {
    /* TEMP2/TEMP1 (0x06/0x07): 12-bit unsigned, TEMP2 holds bits [11:8]. */
    uint8_t tx[3] = { (uint8_t)((NQ_ADXL355_REG_TEMP2 << 1) | 0x01u) };
    uint8_t rx[3] = { 0 };
    int r = nic_spi_transfer(bus, tx, rx, sizeof tx);
    if (r != NIC_OK) {
        return r;
    }
    if (out) {
        *out = (int16_t)((((uint16_t)(rx[1] & 0x0Fu)) << 8) | rx[2]);
    }
    return NIC_OK;
}

int nq_adxl355_set_range(const nic_spi_t *bus, uint8_t range_code) {
    /* RANGE may only be written in standby; bracket it and return to measure. */
    int r;
    if ((r = adxl_write_reg(bus, NQ_ADXL355_REG_POWER_CTL, NQ_ADXL355_POWER_CTL_STANDBY)) != NIC_OK) return r;
    if ((r = adxl_write_reg(bus, NQ_ADXL355_REG_RANGE,     range_code))                   != NIC_OK) return r;
    if ((r = adxl_write_reg(bus, NQ_ADXL355_REG_POWER_CTL, NQ_ADXL355_POWER_CTL_MEASURE)) != NIC_OK) return r;
    return NIC_OK;
}
