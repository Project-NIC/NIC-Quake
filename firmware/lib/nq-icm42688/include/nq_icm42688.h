#ifndef NQ_ICM42688_H
#define NQ_ICM42688_H

/*
 * Driver for the TDK InvenSense ICM-42688-P 6-axis IMU (gyro + accel).
 * In NIC-Quake it sits on SPI1. Hardware-agnostic: all bus access goes through
 * an nic_spi_t handle supplied by the glue layer.
 *
 * This first cut implements identity probing (auto-detection). Configuration,
 * the CLKIN external-clock lock and FIFO/sample reads land in later steps —
 * see the TODOs.
 */

#include "nic_spi.h"
#include "nic_types.h"

/* Register map (subset). SPI read sets bit 7 of the address byte. */
#define NQ_ICM42688_REG_WHO_AM_I   0x75u
#define NQ_ICM42688_WHO_AM_I_VALUE 0x47u   /* per DS-000347 v1.7 */

/*
 * Probe the bus for an ICM-42688-P.
 * Returns 1 if WHO_AM_I matches, 0 if a different/no device answered,
 * negative (NIC_ERR) on a bus error.
 */
int nq_icm42688_probe(const nic_spi_t *bus);

/*
 * Configuration & data registers (bank 0 unless noted). Addresses and field
 * values grounded against DS-000347 v1.7 and the PX4 InvenSense register
 * definitions.
 */
#define NQ_ICM42688_REG_INT_CONFIG     0x14u
#define NQ_ICM42688_REG_TEMP_DATA1     0x1Du  /* die temp hi byte; TEMP_DATA0 = 0x1E */
#define NQ_ICM42688_REG_ACCEL_DATA_X1  0x1Fu  /* accel+gyro burst start, big-endian */
#define NQ_ICM42688_REG_INTF_CONFIG1   0x4Du
#define NQ_ICM42688_REG_PWR_MGMT0      0x4Eu
#define NQ_ICM42688_REG_GYRO_CONFIG0   0x4Fu
#define NQ_ICM42688_REG_ACCEL_CONFIG0  0x50u
#define NQ_ICM42688_REG_INT_SOURCE0    0x65u
#define NQ_ICM42688_REG_BANK_SEL       0x76u
#define NQ_ICM42688_REG_INTF_CONFIG5   0x7Bu  /* bank 1 */

/* Free-running 125 Hz config (D3/D4): CLKIN-disciplined, accel range per the
 * caller's accel_config0 (±8g default in the reference build, D16), gyro fixed
 * at its most sensitive ±15.625 dps (D16), ODR field = 100 Hz (lands on 125 Hz
 * with the 40.96 kHz CLKIN), data-ready on INT1. */
#define NQ_ICM42688_BANK0               0x00u
#define NQ_ICM42688_BANK1               0x01u
#define NQ_ICM42688_PIN9_CLKIN          0x04u  /* INTF_CONFIG5 PIN9_FUNCTION=CLKIN */
#define NQ_ICM42688_INTF_CONFIG1_RTC     0x95u  /* default 0x91 | RTC_MODE 0x04 */
#define NQ_ICM42688_GYRO_15_625DPS_100HZ 0xE8u  /* GYRO_FS_SEL 111 (most sensitive) | ODR 1000b */
#define NQ_ICM42688_GYRO_31_25DPS_100HZ  0xC8u  /* GYRO_FS_SEL 110 | ODR 1000b */
#define NQ_ICM42688_GYRO_62_5DPS_100HZ   0xA8u  /* GYRO_FS_SEL 101 | ODR 1000b */
#define NQ_ICM42688_GYRO_125DPS_100HZ    0x88u  /* GYRO_FS_SEL 100 | ODR 1000b */
#define NQ_ICM42688_ACCEL_2G_100HZ       0x68u  /* ACCEL_FS_SEL 011 | ODR 1000b — default (CR) */
#define NQ_ICM42688_ACCEL_8G_100HZ       0x28u  /* ACCEL_FS_SEL 001 | ODR 1000b — extreme sites */
#define NQ_ICM42688_ACCEL_16G_100HZ      0x08u  /* ACCEL_FS_SEL 000 | ODR 1000b — shocks/peaks */
#define NQ_ICM42688_INT_CONFIG_INT1      0x03u  /* INT1 active-high, push-pull */
#define NQ_ICM42688_DRDY_INT1           0x08u  /* INT_SOURCE0 UI_DRDY -> INT1 */
#define NQ_ICM42688_PWR_ACCEL_GYRO_LN   0x0Fu  /* PWR_MGMT0 both low-noise */

/*
 * Configure for free-running 125 Hz sampling on the external CLKIN. The gyro is
 * fixed at its most sensitive range (±15.625 dps); `accel_config0` selects the
 * accelerometer range — NQ_ICM42688_ACCEL_2G_100HZ (default) or
 * NQ_ICM42688_ACCEL_8G_100HZ (extreme sites). Values are datasheet-grounded; the
 * CLKIN/bank routing and INT setup still want validation on hardware. Returns
 * NIC_OK or a bus error.
 */
int nq_icm42688_init(const nic_spi_t *bus, uint8_t accel_config0);

/*
 * Read one sample: accel and gyro, each three int16 axes sign-extended into
 * int32 counts (raw, pre-transform). Either pointer may be NULL to skip it.
 */
int nq_icm42688_read(const nic_spi_t *bus, nic_sample3_t *accel, nic_sample3_t *gyro);

/*
 * Read the die temperature: TEMP_DATA1/0, 16-bit big-endian signed counts
 * (degC = counts / 132.48 + 25). A slow drift covariate — read at the
 * housekeeping rate, not per sample. `out` may be NULL.
 */
int nq_icm42688_read_temp(const nic_spi_t *bus, int16_t *out);

/*
 * Change a range at runtime without a full re-init: write only ACCEL_CONFIG0 /
 * GYRO_CONFIG0 with the given config byte. Pass one of the NQ_ICM42688_ACCEL_*
 * / NQ_ICM42688_GYRO_* constants — they keep the ODR field, so the 125 Hz
 * phase-lock is preserved. Returns NIC_OK or a bus error.
 */
int nq_icm42688_set_accel_range(const nic_spi_t *bus, uint8_t accel_config0);
int nq_icm42688_set_gyro_range(const nic_spi_t *bus, uint8_t gyro_config0);

#endif /* NQ_ICM42688_H */
