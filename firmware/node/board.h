#ifndef NQ_BOARD_H
#define NQ_BOARD_H

/*
 * STM32H503 board support — the only hardware-bound layer of the firmware.
 *
 * It implements the callbacks the portable nq-* libraries expect, using the ST
 * HAL. The peripheral handles (hspi1..3, huart1/2, timers, IWDG) come from the
 * CubeMX-generated init; see node/CUBEMX.md for the required configuration.
 *
 * This file does NOT build on a host PC and is NOT covered by the host test
 * suites. The routine peripherals (SPI/UART/flash/timers/IWDG) are standard ST
 * HAL usage; the clock-switch path (UART2 -> HSE_IN remap, CLKIN routing) is
 * chip-specific and MUST be validated on the real board with a scope.
 */

#include "nic_spi.h"
#include "nic_clocksync.h"
#include "nq_sensors.h"
#include <stdint.h>
#include <stddef.h>

/* Bring up the board peripherals beyond CubeMX init (CS pins high, DWT µs timer,
 * DRDY interrupts). Call once after the CubeMX MX_*_Init(). */
void board_init(void);

/* Fill the three dedicated sensor buses: SPI1=ICM, SPI2=ADXL355, SPI3=SCL3300. */
void board_sensor_buses(nq_sensors_buses_t *buses);

/* The nq-clocksync ops vtable (UART2 <-> HSE switch + recovery). */
const nic_clocksync_ops_t *board_clocksync_ops(void);

/* Start the phase-locked sensor clocks (1.024 MHz EXT_CLK, 40.96 kHz CLKIN)
 * once the PLL is running on the master's HSE. */
void board_sensor_clocks_start(void);

/* RS-485 data link (UART1, hardware DE). Idle-line delimits a received frame. */
int board_link_send(const uint8_t *buf, size_t len);
int board_link_recv(uint8_t *buf, size_t cap, size_t *out_len, uint32_t timeout_ms);

/* Calibration-matrix persistence in a reserved flash page. */
int board_flash_store(const void *data, size_t len);
int board_flash_load(void *data, size_t len);

/* This unit's permanent HARDWARE NUMBER — the small number printed on the box
 * (NOT the MCU's 96-bit unique ID; see DESIGN D23 / SOFTWARE "Node Addressing").
 * It is a per-box build constant (set it and reflash — the image is a few KB),
 * simpler than a flash cell + provisioning at this scale; board_hw_addr() keeps the
 * seam so it can become flash-backed later. The node reports it at boot / after a
 * hard reset, and the master software-remaps it to a slot. */
uint8_t board_hw_addr(void);

/* DRDY edge latch per sensor (set in EXTI ISR, cleared on read). */
void board_drdy_isr(nq_sensor_id_t id);   /* call from the CubeMX EXTI line ISR */
int  board_drdy_take(nq_sensor_id_t id);

/* Master-clock-loss latch: the STM32 Clock Security System (CSS) fires when the
 * HSE disappears, auto-switches SYSCLK to HSI, and sets this. Returns 1 once per
 * loss; the runtime then emits NIC_EV_CLOCK_LOST and runs nic_clocksync_recover. */
int board_clock_lost_take(void);

/* Rolling RS-485 link-error count (echo mismatch / CRC fails the node sees),
 * reported to the master on a HEALTH query for link diagnostics. */
unsigned board_link_errors(void);
void     board_link_error_bump(void);   /* called by the UART echo check on a mismatch */

void board_delay_us(uint32_t us);
void board_watchdog_kick(void);

/* Self-running TDMA slot timer (D24): returns 1 once when this node's transmit slot
 * is due — the predecessor's frame was overheard (RS-485 idle line after a frame),
 * or the slot deadline (round start + (addr-1)*slot) elapsed. VALIDATE ON HW. */
int board_slot_due(uint8_t addr);

/* Diagnostics for the D24 status byte + master queries. Supply in 0.1 V/LSB:
 * `input` = the bus feed, `internal` = the switcher's 5 V output (sensed at the
 * switcher, NOT the low-noise LDO rails). board_sensor_fault() latches a
 * present-but-silent sensor; board_cpu_temp_c() feeds the >50 C over-temp OR and
 * the GET_CPUTEMP query. VALIDATE ON HW. */
uint8_t board_supply_input_dv(void);
uint8_t board_supply_internal_dv(void);
int8_t  board_cpu_temp_c(void);
int     board_sensor_fault(void);

#endif /* NQ_BOARD_H */
