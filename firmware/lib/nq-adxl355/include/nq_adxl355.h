#ifndef NQ_ADXL355_H
#define NQ_ADXL355_H

/*
 * Driver for the Analog Devices ADXL355 precision seismic accelerometer. In
 * NIC-Quake it sits on SPI2 (the inclinometer / drift-reference role belongs to
 * the Murata SCL3300 on SPI3, D15). One sensor per dedicated bus, so the bus
 * identity is the sensor identity; the probe only confirms "an ADXL355 answered
 * here".
 *
 * Hardware-agnostic: all bus access goes through an nic_spi_t handle supplied by
 * the glue layer. Implements identity probing, 125 Hz free-running config on the
 * EXT_CLK, sample/temperature reads and runtime range change.
 */

#include "nic_spi.h"
#include "nic_types.h"

/* Register map (subset). SPI byte 0 = (addr << 1) | R/W, R=1. */
#define NQ_ADXL355_REG_DEVID_AD   0x00u
#define NQ_ADXL355_REG_DEVID_MST  0x01u
#define NQ_ADXL355_REG_PARTID     0x02u

#define NQ_ADXL355_DEVID_AD_VALUE  0xADu  /* Analog Devices ID            */
#define NQ_ADXL355_DEVID_MST_VALUE 0x1Du  /* MEMS family ID               */
#define NQ_ADXL355_PARTID_VALUE    0xEDu  /* ADXL355 part ID */

/*
 * Probe the bus for an ADXL355.
 * Returns 1 if the Analog Devices + MEMS + part IDs all match, 0 if a
 * different/no device answered, negative (NIC_ERR) on a bus error.
 */
int nq_adxl355_probe(const nic_spi_t *bus);

/* Configuration & data registers. SPI byte 0 = (addr << 1) | R/W. */
#define NQ_ADXL355_REG_TEMP2      0x06u  /* die temp: TEMP2[3:0]=bits[11:8], TEMP1=bits[7:0] */
#define NQ_ADXL355_REG_XDATA3     0x08u  /* X/Y/Z, 20-bit, 3 bytes each, big-endian */
#define NQ_ADXL355_REG_FILTER     0x28u
#define NQ_ADXL355_REG_SYNC       0x2Bu
#define NQ_ADXL355_REG_RANGE      0x2Cu
#define NQ_ADXL355_REG_POWER_CTL  0x2Du
#define NQ_ADXL355_REG_RESET      0x2Eu

#define NQ_ADXL355_FILTER_ODR_125    0x05u  /* ODR 125 Hz, LPF 31.25 Hz, no HPF */
#define NQ_ADXL355_SYNC_EXT_CLK      0x04u  /* EXT_CLK on, internal sync (free run) */
#define NQ_ADXL355_POWER_CTL_MEASURE 0x00u  /* clear STANDBY -> measurement mode */
#define NQ_ADXL355_POWER_CTL_STANDBY 0x01u  /* set STANDBY (required to change RANGE) */
#define NQ_ADXL355_RESET_CODE        0x52u

/* RANGE codes (bits[1:0]): ADXL355 ±2g / ±4g / ±8g. */
#define NQ_ADXL355_RANGE_1           0x01u  /* ±2g */
#define NQ_ADXL355_RANGE_2           0x02u  /* ±4g */
#define NQ_ADXL355_RANGE_3           0x03u  /* ±8g */

/*
 * Configure for free-running 125 Hz on the 1.024 MHz EXT_CLK (D3/D4). All
 * configuration is written before measurement mode is enabled, as the datasheet
 * requires. range_code selects the range (NQ_ADXL355_RANGE_1..3). Returns NIC_OK
 * or a bus error.
 */
int nq_adxl355_init(const nic_spi_t *bus, uint8_t range_code);

/* Read one X/Y/Z sample: three 20-bit, sign-extended int32 counts (raw). */
int nq_adxl355_read(const nic_spi_t *bus, nic_sample3_t *out);

/*
 * Read the die temperature: TEMP2/TEMP1, 12-bit unsigned counts (~1852 counts at
 * 25°C, slope ≈ -9.05 counts/°C). A slow drift covariate — read at the
 * housekeeping rate, not per sample. `out` may be NULL.
 */
int nq_adxl355_read_temp(const nic_spi_t *bus, int16_t *out);

/*
 * Change the range at runtime: the ADXL355 requires standby to write RANGE, so
 * this brackets the RANGE write with standby -> measure. It briefly perturbs the
 * free-running stream (a sample or two), which the master absorbs by rolling to a
 * new storage table (D20). range_code is NQ_ADXL355_RANGE_1..3. Returns NIC_OK or
 * a bus error.
 */
int nq_adxl355_set_range(const nic_spi_t *bus, uint8_t range_code);

#endif /* NQ_ADXL355_H */
