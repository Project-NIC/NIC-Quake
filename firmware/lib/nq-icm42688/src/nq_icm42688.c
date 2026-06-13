#include "nq_icm42688.h"

/*
 * ICM-42688-P SPI read: first byte is the register address with bit 7 set to
 * mark a read; the device shifts the value out on the following byte.
 */
static int icm_read_reg(const nic_spi_t *bus, uint8_t reg, uint8_t *val) {
    uint8_t tx[2] = { (uint8_t)(reg | 0x80u), 0x00u };
    uint8_t rx[2] = { 0u, 0u };
    int r = nic_spi_transfer(bus, tx, rx, sizeof tx);
    if (r != NIC_OK) {
        return r;
    }
    *val = rx[1];
    return NIC_OK;
}

int nq_icm42688_probe(const nic_spi_t *bus) {
    uint8_t id = 0u;
    int r = icm_read_reg(bus, NQ_ICM42688_REG_WHO_AM_I, &id);
    if (r != NIC_OK) {
        return r;
    }
    return (id == NQ_ICM42688_WHO_AM_I_VALUE) ? 1 : 0;
}

/* Write: bit 7 of the address byte clear marks a write. */
static int icm_write_reg(const nic_spi_t *bus, uint8_t reg, uint8_t val) {
    uint8_t tx[2] = { (uint8_t)(reg & 0x7Fu), val };
    return nic_spi_transfer(bus, tx, NULL, sizeof tx);
}

int nq_icm42688_init(const nic_spi_t *bus, uint8_t accel_config0) {
    int r;

    /* Route CLKIN onto pin 9 (lives in bank 1), then return to bank 0. */
    if ((r = icm_write_reg(bus, NQ_ICM42688_REG_BANK_SEL,     NQ_ICM42688_BANK1))             != NIC_OK) return r;
    if ((r = icm_write_reg(bus, NQ_ICM42688_REG_INTF_CONFIG5, NQ_ICM42688_PIN9_CLKIN))        != NIC_OK) return r;
    if ((r = icm_write_reg(bus, NQ_ICM42688_REG_BANK_SEL,     NQ_ICM42688_BANK0))             != NIC_OK) return r;

    /* RTC mode (use external CLKIN), gyro most-sensitive, accel range per caller,
     * data-ready on INT1. */
    if ((r = icm_write_reg(bus, NQ_ICM42688_REG_INTF_CONFIG1, NQ_ICM42688_INTF_CONFIG1_RTC))     != NIC_OK) return r;
    if ((r = icm_write_reg(bus, NQ_ICM42688_REG_GYRO_CONFIG0, NQ_ICM42688_GYRO_15_625DPS_100HZ)) != NIC_OK) return r;
    if ((r = icm_write_reg(bus, NQ_ICM42688_REG_ACCEL_CONFIG0, accel_config0))                   != NIC_OK) return r;
    if ((r = icm_write_reg(bus, NQ_ICM42688_REG_INT_CONFIG,   NQ_ICM42688_INT_CONFIG_INT1))      != NIC_OK) return r;
    if ((r = icm_write_reg(bus, NQ_ICM42688_REG_INT_SOURCE0,  NQ_ICM42688_DRDY_INT1))        != NIC_OK) return r;

    /* Power up both sensors last. */
    if ((r = icm_write_reg(bus, NQ_ICM42688_REG_PWR_MGMT0,    NQ_ICM42688_PWR_ACCEL_GYRO_LN)) != NIC_OK) return r;
    return NIC_OK;
}

int nq_icm42688_read(const nic_spi_t *bus, nic_sample3_t *accel, nic_sample3_t *gyro) {
    /* Burst read accel X/Y/Z then gyro X/Y/Z (12 bytes), 16-bit big-endian. */
    uint8_t tx[13] = { (uint8_t)(NQ_ICM42688_REG_ACCEL_DATA_X1 | 0x80u) };
    uint8_t rx[13] = { 0 };
    int r = nic_spi_transfer(bus, tx, rx, sizeof tx);
    if (r != NIC_OK) {
        return r;
    }

    if (accel) {
        for (int i = 0; i < 3; i++) {
            accel->axis[i] = (int16_t)(((uint16_t)rx[1 + i*2] << 8) | rx[2 + i*2]);
        }
    }
    if (gyro) {
        for (int i = 0; i < 3; i++) {
            gyro->axis[i] = (int16_t)(((uint16_t)rx[7 + i*2] << 8) | rx[8 + i*2]);
        }
    }
    return NIC_OK;
}

int nq_icm42688_read_temp(const nic_spi_t *bus, int16_t *out) {
    /* TEMP_DATA1/0 (0x1D/0x1E): 16-bit big-endian, signed. */
    uint8_t tx[3] = { (uint8_t)(NQ_ICM42688_REG_TEMP_DATA1 | 0x80u) };
    uint8_t rx[3] = { 0 };
    int r = nic_spi_transfer(bus, tx, rx, sizeof tx);
    if (r != NIC_OK) {
        return r;
    }
    if (out) {
        *out = (int16_t)(((uint16_t)rx[1] << 8) | rx[2]);
    }
    return NIC_OK;
}

int nq_icm42688_set_accel_range(const nic_spi_t *bus, uint8_t accel_config0) {
    /* Single-register range change; ACCEL_CONFIG0 also holds ODR, kept by the
     * caller's constant so the phase-lock is undisturbed. */
    return icm_write_reg(bus, NQ_ICM42688_REG_ACCEL_CONFIG0, accel_config0);
}

int nq_icm42688_set_gyro_range(const nic_spi_t *bus, uint8_t gyro_config0) {
    return icm_write_reg(bus, NQ_ICM42688_REG_GYRO_CONFIG0, gyro_config0);
}
