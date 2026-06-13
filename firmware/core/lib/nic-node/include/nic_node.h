#ifndef NIC_NODE_H
#define NIC_NODE_H

/*
 * nq-node — the glue: node lifecycle state machine and the data-plane packer.
 *
 * The STM32-specific board layer (SPI/UART/flash/clock HAL, CubeMX init) is the
 * remaining hardware-bound piece and lives alongside this in the firmware
 * project. The two portable, host-testable cores live here:
 *
 *   1. the lifecycle state machine (pure (state, event) -> (state, action)),
 *      mirroring the start sequence in SOFTWARE.md / D5, and
 *   2. the sample packer that builds the data-plane payload carried by nq-link.
 *
 * The runtime loop the board layer runs is, in effect:
 *     act = nic_node_step(state, event).action; state = ...;
 *     perform `act` against the board HAL (detect, init, switch clock, sample).
 */

#include "nic_types.h"
#include <stdint.h>
#include <stddef.h>

/* ---- Lifecycle state machine ------------------------------------------ */

typedef enum {
    NIC_NODE_BOOT,          /* powered up on the internal RC oscillator      */
    NIC_NODE_DETECT,        /* probing the SPI buses for sensors             */
    NIC_NODE_INIT_SENSORS,  /* configuring whichever sensors answered        */
    NIC_NODE_LISTEN,        /* on RC, waiting for the master on the data pair */
    NIC_NODE_SYNCED,        /* on HSE, sensors free-running, sampling         */
    NIC_NODE_FAULT,         /* clock lost / error, recovering                 */
} nic_node_state_t;

typedef enum {
    NIC_EV_BOOT_DONE,       /* clocks/peripherals up                         */
    NIC_EV_DETECTED,        /* sensor probe finished                         */
    NIC_EV_SENSORS_READY,   /* sensor init finished                          */
    NIC_EV_SYNC_CMD,        /* master broadcast: switch to HSE clock         */
    NIC_EV_HSE_OK,          /* PLL locked onto the master clock              */
    NIC_EV_CALIBRATE_CMD,   /* master command: record reference zero         */
    NIC_EV_CLOCK_LOST,      /* watchdog: master clock gone                   */
    NIC_EV_RESET_CMD,       /* master broadcast: hard reset                  */
} nic_node_event_t;

typedef enum {
    NIC_ACT_NONE,
    NIC_ACT_DETECT,         /* probe the sensors                             */
    NIC_ACT_INIT_SENSORS,   /* configure present sensors for 125 Hz          */
    NIC_ACT_LISTEN,         /* listen for the master on the data pair        */
    NIC_ACT_SWITCH_HSE,     /* run the nq-clocksync switch sequence          */
    NIC_ACT_START_SAMPLING, /* arm the DRDY-driven sampling loop             */
    NIC_ACT_CALIBRATE,      /* average gravity, compute & store the matrix   */
    NIC_ACT_RECOVER,        /* run the nq-clocksync recovery sequence        */
} nic_node_action_t;

typedef struct {
    nic_node_state_t  state;
    nic_node_action_t action;
} nic_node_step_t;

/*
 * Pure lifecycle transition: given the current state and an event, return the
 * next state and the action the runtime should perform. Unhandled (state,event)
 * pairs leave the state unchanged with NIC_ACT_NONE.
 */
nic_node_step_t nic_node_step(nic_node_state_t state, nic_node_event_t event);

/* ---- Data-plane packer ------------------------------------------------- */

/*
 * One sensor's contribution to a data packet: its three-axis sample, how many
 * low bits to discard, and how many bytes of each axis to send.
 *
 * Effective resolution (ENOB) at 125 Hz is ~14-15 bits for every sensor, so
 * 16 bits (`bytes_per_axis = 2`) carries all the signal with headroom. The
 * ADXL355 reads 20-bit but its low ~4 bits are below the noise floor, so it
 * sends 16 bits with `drop_bits = 4` (keep the MSBs, discard sub-ENOB noise);
 * the 16-bit ICM uses `drop_bits = 0`. Levelling runs at full precision first;
 * the reduction happens only here, at the wire.
 */
typedef struct {
    nic_sample3_t sample;
    uint8_t      drop_bits;        /* low bits discarded before serialising (0..31) */
    uint8_t      bytes_per_axis;   /* 1..4 */
} nic_node_field_t;

/*
 * Pack the sensor payload of a data frame: per field, 3 axes x bytes_per_axis,
 * big-endian. Each axis is divided by 2^`drop_bits` (toward zero — well-defined for
 * negatives, unlike a signed right shift), saturated to the signed
 * range of `bytes_per_axis`, then emitted (two's complement). Returns the payload
 * length, or NIC_ERR on bad args / capacity.
 *
 * Station and sample index live in the nq-link DATA frame header, not here. A
 * scalar like temperature is appended by the caller after this payload; the whole
 * buffer is then handed to nic_link_data_encode(). The payload length is fixed per
 * node (its sensor schema) and agreed with the master at init.
 */
int nic_node_pack(const nic_node_field_t *fields, int n_fields,
                 uint8_t *out, size_t cap);

#endif /* NIC_NODE_H */
