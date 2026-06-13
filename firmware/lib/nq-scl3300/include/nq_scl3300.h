#ifndef NQ_SCL3300_H
#define NQ_SCL3300_H

/*
 * Driver for the Murata SCL3300 3-axis inclinometer (SPI3) — the node's
 * tilt / long-term drift reference. A purpose-built 3-axis inclinometer with far
 * better offset-over-temperature stability than a general accelerometer and a direct,
 * factory-linearised angle output, which suits both the calibration/levelling
 * reference role and slow structural-tilt monitoring (e.g. subsidence over mine
 * shafts).
 *
 * Unlike the seismic sensors it has no external clock input, so it is NOT
 * phase-locked to the master and is read slowly (housekeeping rate, not 125 Hz)
 * — fine for a drift reference.
 *
 * Protocol: every SPI exchange is a fixed 32-bit frame, MSB-first:
 *   [31:24] op/addr (+ RS in a response)   [23:8] 16-bit data   [7:0] CRC-8
 * and it is pipelined — the response to a command arrives in the *next* frame,
 * so a register read sends its command twice and uses the second response.
 * Command constants below carry their CRC baked in (per Murata / the
 * DavidArmstrong reference library). The board must honour the >=10 us minimum
 * between frames and the start-up settling waits.
 */

#include "nic_spi.h"
#include "nic_types.h"
#include <stdint.h>

/* 32-bit command frames (CRC embedded). */
#define NQ_SCL3300_CMD_WHOAMI        0x40000091u
#define NQ_SCL3300_CMD_READ_ACC_X    0x040000F7u
#define NQ_SCL3300_CMD_READ_ACC_Y    0x080000FDu
#define NQ_SCL3300_CMD_READ_ACC_Z    0x0C0000FBu
#define NQ_SCL3300_CMD_READ_ANG_X    0x240000C7u
#define NQ_SCL3300_CMD_READ_ANG_Y    0x280000CDu
#define NQ_SCL3300_CMD_READ_ANG_Z    0x2C0000CBu
#define NQ_SCL3300_CMD_READ_TEMP     0x140000EFu
#define NQ_SCL3300_CMD_READ_STATUS   0x180000E5u
#define NQ_SCL3300_CMD_ENABLE_ANGLE  0xB0001F6Fu

/*
 * Measurement modes (SCL3300-D01 datasheet). CRC-verified command frames.
 *   Mode 1: ±1.2g, ±90° inclination, 40 Hz   — any install orientation
 *   Mode 2: ±2.4g, ±90° inclination, 70 Hz   — any install orientation
 *   Mode 3: ±10° inclination (high-res),  10 Hz — near-level only
 *   Mode 4: ±10° inclination, lowest noise, 10 Hz — near-level only
 * Modes 1/2 measure a full gravity vector at any tilt; Modes 3/4 only within
 * ±10° of level (high resolution for structural-tilt monitoring).
 */
#define NQ_SCL3300_CMD_MODE1         0xB400001Fu
#define NQ_SCL3300_CMD_MODE2         0xB4000102u
#define NQ_SCL3300_CMD_MODE3         0xB4000225u
#define NQ_SCL3300_CMD_MODE4         0xB4000338u

#define NQ_SCL3300_CMD_WAKEUP        0xB400001Fu  /* == Mode 1 (mode reg default) */
#define NQ_SCL3300_CMD_SWRESET       0xB4002098u
#define NQ_SCL3300_CMD_PWRDOWN       0xB400046Bu

#define NQ_SCL3300_WHOAMI_VALUE      0xC1u

/*
 * Probe for the SCL3300. Returns 1 if WHOAMI matches, 0 if a different/no
 * device answered, NIC_ERR on a bus error, NIC_ERR_CRC on a frame CRC failure.
 */
int nq_scl3300_probe(const nic_spi_t *bus);

/*
 * Init for the tilt / drift-reference role: software reset, select `mode_cmd`
 * (NQ_SCL3300_CMD_MODE1..4 — pick ±90° vs ±10° per the install orientation),
 * enable the angle output, clear status. The board inserts the datasheet
 * start-up waits between these. Returns NIC_OK or an error.
 */
int nq_scl3300_init(const nic_spi_t *bus, uint32_t mode_cmd);

/*
 * Auto-select the mode at start-up: configure Mode 1 (±90°), read the gravity
 * vector, and if the install is within ~10° of level re-init in Mode 4 for the
 * best tilt resolution; otherwise stay in Mode 1 so a steeply-mounted node still
 * reads. The level test is trig-free: sin²(tilt) = (x²+y²)/(x²+y²+z²) ≤ sin²10°.
 * If `near_level` is non-NULL it is set to 1 (Mode 4) or 0 (Mode 1). Returns
 * NIC_OK or an error.
 */
int nq_scl3300_init_auto(const nic_spi_t *bus, int *near_level);

/* Raw three-axis acceleration — the gravity vector fed to nq-cal levelling. */
int nq_scl3300_read_acc(const nic_spi_t *bus, nic_sample3_t *out);

/* Raw three-axis inclination angle (counts; 2^14 == 90 deg) for tilt monitoring. */
int nq_scl3300_read_ang(const nic_spi_t *bus, nic_sample3_t *out);

/* Die temperature (counts; degC = -273 + counts / 18.9) for drift housekeeping. */
int nq_scl3300_read_temp(const nic_spi_t *bus, int16_t *out);

#endif /* NQ_SCL3300_H */
