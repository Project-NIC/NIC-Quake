/*
 * NIC-Quake node application — composes the portable nq-* libraries on the
 * STM32H503 via the board layer. CubeMX owns clock/peripheral init and the real
 * main.c; this file is the application body (call nq_app_run() from main()).
 *
 * Like board.c, this is the hardware-side glue: it is not host-built or
 * host-tested. The libraries it drives ARE (7 suites). NOT validated on a board.
 */

#include "board.h"
#include "nq_icm42688.h"
#include "nq_adxl355.h"
#include "nq_scl3300.h"
#include "nq_sensors.h"
#include "nq_cal.h"
#include "nic_link.h"
#include "nic_clocksync.h"
#include "nic_node.h"
#include "nic_proto.h"        /* shared control-plane opcodes (master + node) */

#include <math.h>
#include <string.h>

/* --- Per-deployment configuration (D16/D17) -------------------------------- */
/* Node identity is the human-set NUMBER ON THE BOX, read from flash at boot via
 * board_hw_addr() — NOT the MCU's 96-bit unique ID (DESIGN D23). An installer
 * remembers "3"; nobody tracks a 96-bit hex string for a box buried in rock. It is
 * a per-box build constant (set it and reflash — the image is a few KB), printed on
 * the enclosure. The master software-remaps it to a slot;
 * on a hard reset the node falls back to this number, so you always know which
 * physical box (hence which location) a stream came from. */
#define CFG_ADXL_RANGE       NQ_ADXL355_RANGE_1              /* ±2g (RANGE_2 = ±4g)       */
#define CFG_ICM_ACCEL        NQ_ICM42688_ACCEL_8G_100HZ      /* high-range / extreme       */
#define CFG_SCL_MODE         NQ_SCL3300_CMD_MODE1            /* ±90°, fixed (D15)          */
#define FW_VERSION           1u                              /* answered only on master query */
/* WHAT this node is (D26) — a per-PRODUCT constant, reported at discovery so the
 * master knows the schema/role. This is the NIC-Quake node, so SEISMO. The box NUMBER
 * (WHICH one) is separate and per-box: board_hw_addr(). A non-Quake line that reuses
 * this firmware (e.g. the Weather "Basic station") changes only CFG_NODE_TYPE + its
 * sensor front — everything else, and thus bus compatibility, stays identical. */
#define CFG_NODE_TYPE        NIC_NODE_TYPE_SEISMO

/* This front's generic config slots (D28): the master sends NIC_OP_CFG(slot) + value; the
 * MEANING lives here, not in the shared protocol. Named so the dispatch stays readable. */
#define SEISMO_CFG_ADXL_RANGE  0u   /* value = NQ_ADXL355_RANGE_1..3   */
#define SEISMO_CFG_ICM_ACCEL   1u   /* value = 2 / 8 / 16 g            */
#define SEISMO_CFG_ICM_GYRO    2u   /* value = 125 / 62 / 31 / 15 dps  */
#define SEISMO_CFG_SCL_MODE    3u   /* value = 1 (±90°) / 4 (±10°)     */

/* D24 status byte (DATA payload, fixed last byte -> offset 27 on a full node). */
#define NQ_ST_SAT_ADXL     0x01u   /* ADXL355 axis saturated this sample          */
#define NQ_ST_SAT_ICM_ACC  0x02u   /* ICM accel saturated                          */
#define NQ_ST_SAT_ICM_GYR  0x04u   /* ICM gyro saturated                           */
#define NQ_ST_SAT_SCL      0x08u   /* SCL3300 saturated                            */
#define NQ_ST_VIN_LOW      0x10u   /* input (bus) supply low                       */
#define NQ_ST_VINT_LOW     0x20u   /* internal 5 V (switcher) low                  */
#define NQ_ST_OVERTEMP     0x40u   /* anything (a die or the MCU) > 50 C           */
#define NQ_ST_SENSOR_FAULT 0x80u   /* a present sensor went silent                 */

/* Status thresholds. Supply in 0.1 V/LSB; saturation guards are per-sensor, set
 * just below each part's full-scale count (datasheet bit depth). */
#define CFG_VIN_LOW_DV      90u    /* input low ~9.0 V — reserve above the 6.5 V switcher floor */
#define CFG_VINT_LOW_DV     48u    /* internal low ~4.8 V — switcher failing / oscillating      */
#define ADXL355_FS      520000     /* 20-bit, +-FS ~ 2^19 (524288); guard near the rail         */
#define ICM_FS           32200     /* 16-bit, +-FS 32768                                        */
#define SCL3300_FS       32200     /* 16-bit ACC output                                         */
/* Housekeeping (per-sensor temps, SCL tilt) cadence: poll every Nth 125 Hz sample → ~10 Hz,
 * not at the CPU loop rate (D17). */
#define CFG_HK_DIV       12u

/* --- State ----------------------------------------------------------------- */
static nq_sensors_buses_t  s_buses;
static nq_sensors_map_t    s_map;
static nq_sensor_id_t      s_seismic;        /* ADXL355 or ICM accel */
static uint8_t             s_addr;            /* current SOFTWARE address; set to the box's hardware number at boot/reset, then remapped by the master */

/* Drivers are stateless (bus-only); we keep just the per-sensor calibration. */
static nq_cal_t  s_cal_adxl, s_cal_iacc, s_cal_igyro;   /* leveled seismic fields */
static int16_t   s_temp_adxl, s_temp_icm, s_temp_scl;   /* latest per-sensor die temps (D18) */
static nic_sample3_t s_scl_tilt;              /* latest SCL3300 raw ACC            */
static uint16_t  s_index;                    /* sub-second sample index           */
static uint8_t   s_hk_div;                   /* #3: housekeeping cadence divider (D17) */
static uint8_t   s_reply_op, s_reply_val;    /* one control reply queued for our next TDMA slot */

/* The latest synchronised sample, packed into a DATA frame on DRDY and held until
 * this node's own clock-derived TDMA slot is due (D24) — the node NEVER transmits
 * asynchronously, and the slots are disjoint, so it cannot collide. What the master
 * then does with it (uplink vs. write to its own MLA on card) is the ESP32 master's
 * job; the STM32 is only the node. */
static uint8_t   s_data_frame[NIC_LINK_DATA_OVERHEAD + 32];
static int       s_data_len;                 /* 0 until the first sample is buffered */

/* --- Helpers --------------------------------------------------------------- */

static nic_sample3_t vec_to_counts(nic_vec3_t v) {
    nic_sample3_t s = {{ (int32_t)lroundf(v.v[0]), (int32_t)lroundf(v.v[1]), (int32_t)lroundf(v.v[2]) }};
    return s;
}

/* D24: a raw axis at/near full scale. VALIDATE ON HW: exact FS per sensor/range. */
static int axis_saturated(const int32_t axis[3], int32_t fs) {
    for (int i = 0; i < 3; i++) {
        int32_t m = axis[i] < 0 ? -axis[i] : axis[i];
        if (m >= fs) return 1;
    }
    return 0;
}

/* D24: bitmask of present sensors (answer to NIC_OP_SENSOR_TEST). */
static uint8_t sensor_present_mask(void) {
    return (uint8_t)((s_map.present[NQ_SENSOR_ADXL355]  ? 0x01u : 0u)
                   | (s_map.present[NQ_SENSOR_ICM42688] ? 0x02u : 0u)
                   | (s_map.present[NQ_SENSOR_SCL3300]  ? 0x04u : 0u));
}

/* The node's fixed DATA payload length — answered to DISCOVER as STATUS so the master
 * can decode this node's frames (no length on the wire; D19). MUST mirror
 * sample_to_buffer's layout: 2 B/axis x 3 axes per field (ADXL 1 field, ICM
 * accel+gyro 2 fields, SCL 1 field), then one die-temp byte per present sensor, then
 * the fixed D24 status byte. Fully populated -> 28 B. */
static uint8_t node_payload_len(void) {
    uint8_t bytes = 0, present = 0;
    if (s_map.present[NQ_SENSOR_ADXL355])  { bytes += 6u;  present++; }
    if (s_map.present[NQ_SENSOR_ICM42688]) { bytes += 12u; present++; }
    if (s_map.present[NQ_SENSOR_SCL3300])  { bytes += 6u;  present++; }
    return (uint8_t)(bytes + present + 1u);   /* + per-sensor temp bytes + status byte */
}

static void configure_sensors(void) {
    if (s_map.present[NQ_SENSOR_ICM42688]) nq_icm42688_init(&s_buses.icm, CFG_ICM_ACCEL);
    if (s_map.present[NQ_SENSOR_ADXL355])  nq_adxl355_init(&s_buses.adxl355, CFG_ADXL_RANGE);
    if (s_map.present[NQ_SENSOR_SCL3300])  nq_scl3300_init(&s_buses.scl3300, CFG_SCL_MODE);
}

/* Persisted calibration: a magic-tagged levelling matrix, so a never-calibrated or
 * just-rebooted node can tell a REAL stored cal from erased / garbage flash. */
#define CAL_MAGIC 0x4E51434Cu   /* "NQCL" */
typedef struct { uint32_t magic; nic_mat3_t r_level; } cal_blob_t;

/* Apply one levelling matrix to all three seismic fields (gyro = pseudovector). */
static void apply_level(const nic_mat3_t *r_level) {
    nic_mat3_t mi = nic_mat3_identity();
    nq_cal_for_sensor(r_level, &mi, 0, &s_cal_adxl);    /* vector */
    nq_cal_for_sensor(r_level, &mi, 0, &s_cal_iacc);    /* vector */
    nq_cal_for_sensor(r_level, &mi, 1, &s_cal_igyro);   /* pseudovector (gyro) */
}

/* #2: at boot default the calibration to IDENTITY (raw passes straight through — never
 * a zero matrix that would silently emit zeros), then restore a stored levelling from
 * flash if a valid one is present (D8/D12). */
static void load_calibration(void) {
    nic_mat3_t ident = nic_mat3_identity();
    s_cal_adxl.r = ident; s_cal_iacc.r = ident; s_cal_igyro.r = ident;
    cal_blob_t blob;
    if (board_flash_load(&blob, sizeof blob) == NIC_OK && blob.magic == CAL_MAGIC) {
        apply_level(&blob.r_level);
    }
}

/* Build the levelling matrices from the gravity vector and persist them (D8/D16). */
static void do_calibration(void) {
    if (s_seismic != NQ_SENSOR_ADXL355 && s_seismic != NQ_SENSOR_ICM42688) return; /* #5: no seismic source */
    nic_sample3_t raw;
    nic_vec3_t g;
    if (s_seismic == NQ_SENSOR_ADXL355) { nq_adxl355_read(&s_buses.adxl355, &raw); }
    else                                { nq_icm42688_read(&s_buses.icm, &raw, NULL); }
    g.v[0] = (float)raw.axis[0]; g.v[1] = (float)raw.axis[1]; g.v[2] = (float)raw.axis[2];

    nic_mat3_t r_level;
    if (nq_cal_level(&g, &r_level) != NIC_OK) return;
    apply_level(&r_level);

    cal_blob_t blob = { CAL_MAGIC, r_level };
    board_flash_store(&blob, sizeof blob);
}

/* One synchronised sample: read present sensors, level the seismic ones, compute
 * the D24 status byte, pack the payload and BUFFER it (stamped with the
 * acquisition-time sample index). The frame is transmitted later, in this node's
 * own clock-derived slot — see send_slot(). DRDY samples; the slot sends. */
static void sample_to_buffer(void) {
    nic_node_field_t fields[4];
    int nf = 0;
    nic_sample3_t a, gyr;
    nic_vec3_t v;
    uint8_t status = 0u;                          /* D24 status byte */

    if (s_map.present[NQ_SENSOR_ADXL355]) {
        nq_adxl355_read(&s_buses.adxl355, &a);
        if (axis_saturated(a.axis, ADXL355_FS)) status |= NQ_ST_SAT_ADXL;
        v = nq_cal_apply_raw(&s_cal_adxl, a.axis);
        fields[nf++] = (nic_node_field_t){ .sample = vec_to_counts(v), .drop_bits = 4, .bytes_per_axis = 2 };
    }
    if (s_map.present[NQ_SENSOR_ICM42688]) {
        nq_icm42688_read(&s_buses.icm, &a, &gyr);
        if (axis_saturated(a.axis,   ICM_FS)) status |= NQ_ST_SAT_ICM_ACC;
        if (axis_saturated(gyr.axis, ICM_FS)) status |= NQ_ST_SAT_ICM_GYR;
        v = nq_cal_apply_raw(&s_cal_iacc, a.axis);
        fields[nf++] = (nic_node_field_t){ .sample = vec_to_counts(v), .drop_bits = 0, .bytes_per_axis = 2 };
        v = nq_cal_apply_raw(&s_cal_igyro, gyr.axis);
        fields[nf++] = (nic_node_field_t){ .sample = vec_to_counts(v), .drop_bits = 0, .bytes_per_axis = 2 };
    }
    if (s_map.present[NQ_SENSOR_SCL3300]) {   /* tilt reference: sent raw, not levelled */
        if (axis_saturated(s_scl_tilt.axis, SCL3300_FS)) status |= NQ_ST_SAT_SCL;
        fields[nf++] = (nic_node_field_t){ .sample = s_scl_tilt, .drop_bits = 0, .bytes_per_axis = 2 };
    }

    /* Rest of the status byte (D24): cheap thresholds on values already in hand —
     * the master just reads the flag, it never scans the data. Over-temp ORs every
     * present die and the MCU; the per-sensor die temps still ship as values too. */
    if (board_supply_input_dv()    < CFG_VIN_LOW_DV)  status |= NQ_ST_VIN_LOW;
    if (board_supply_internal_dv() < CFG_VINT_LOW_DV) status |= NQ_ST_VINT_LOW;
    int over = (board_cpu_temp_c() > 50);
    if (s_map.present[NQ_SENSOR_ADXL355]  && s_temp_adxl > 50) over = 1;
    if (s_map.present[NQ_SENSOR_ICM42688] && s_temp_icm  > 50) over = 1;
    if (s_map.present[NQ_SENSOR_SCL3300]  && s_temp_scl  > 50) over = 1;
    if (over)                 status |= NQ_ST_OVERTEMP;
    if (board_sensor_fault()) status |= NQ_ST_SENSOR_FAULT;

    uint8_t payload[32];
    int plen = nic_node_pack(fields, nf, payload, sizeof payload);
    if (plen < 0) return;
    /* Per-sensor die temperature, one byte each (D18), then the D24 status byte as
     * the fixed last byte — a full node lands at 24 sensor + 3 temp + 1 = 28 B. */
    if (s_map.present[NQ_SENSOR_ADXL355])  payload[plen++] = (uint8_t)(s_temp_adxl & 0xFFu);
    if (s_map.present[NQ_SENSOR_ICM42688]) payload[plen++] = (uint8_t)(s_temp_icm  & 0xFFu);
    if (s_map.present[NQ_SENSOR_SCL3300])  payload[plen++] = (uint8_t)(s_temp_scl  & 0xFFu);
    payload[plen++] = status;                     /* status byte — always present */

    int flen = nic_link_data_encode(s_addr, s_index, payload, (uint8_t)plen,
                                   s_data_frame, sizeof s_data_frame);
    if (flen > 0) s_data_len = flen;
    s_index++;
}

/* Transmit the buffered sample — sent in this node's own clock-derived slot (D24),
 * never asynchronously. Silent until the first sample has buffered. */
static void send_data(void) {
    if (s_data_len > 0) board_link_send(s_data_frame, (size_t)s_data_len);
}

/* Poll the slow housekeeping fields in the background (D17): the SCL3300 tilt and
 * every present sensor's die temperature (each die covaries with its own drift).
 * Read at the slow housekeeping cadence, not the 125 Hz seismic rate. */
static void poll_housekeeping(void) {
    int16_t t;
    if (s_map.present[NQ_SENSOR_ADXL355]  && nq_adxl355_read_temp(&s_buses.adxl355, &t) == NIC_OK) s_temp_adxl = t;
    if (s_map.present[NQ_SENSOR_ICM42688] && nq_icm42688_read_temp(&s_buses.icm, &t)     == NIC_OK) s_temp_icm  = t;
    if (s_map.present[NQ_SENSOR_SCL3300]) {
        nq_scl3300_read_acc(&s_buses.scl3300, &s_scl_tilt);
        if (nq_scl3300_read_temp(&s_buses.scl3300, &t) == NIC_OK) s_temp_scl = t;
    }
}

/* Queue / send a single control reply (ACK or query answer) in our TDMA slot. */
static void queue_reply(uint8_t op, uint8_t val) { s_reply_op = op; s_reply_val = val; }

static void send_reply(void) {
    if (!s_reply_op) return;
    uint8_t f[NIC_LINK_CTRL_LEN];
    int fl = nic_link_ctrl_encode(s_addr, s_reply_op, s_reply_val, f, sizeof f);
    if (fl > 0) board_link_send(f, (size_t)fl);
    s_reply_op = 0;
}

/* Transmit in our TDMA slot: a queued control reply (ACK / query answer) takes the
 * slot, else our buffered data sample (D24). */
static void send_slot(void) {
    if (s_reply_op) send_reply();
    else            send_data();
}

/* Map the master's friendly range value to the sensor's config byte. The full
 * config keeps the ODR field, so the 125 Hz phase-lock survives the change. */
static uint8_t map_icm_accel(uint8_t g) {
    switch (g) {
        case 16: return NQ_ICM42688_ACCEL_16G_100HZ;
        case 8:  return NQ_ICM42688_ACCEL_8G_100HZ;
        default: return NQ_ICM42688_ACCEL_2G_100HZ;          /* 2 g */
    }
}
static uint8_t map_icm_gyro(uint8_t dps) {
    switch (dps) {
        case 125: return NQ_ICM42688_GYRO_125DPS_100HZ;
        case 62:  return NQ_ICM42688_GYRO_62_5DPS_100HZ;
        case 31:  return NQ_ICM42688_GYRO_31_25DPS_100HZ;
        default:  return NQ_ICM42688_GYRO_15_625DPS_100HZ;   /* most sensitive (D16) */
    }
}

/* Apply a generic config-slot write (D28): the seismo front maps slots to sensor range
 * registers. Only the range register is touched, so the 125 Hz phase-lock survives (D21);
 * an unused slot is ignored. The master rolls to a new storage table on the change (D20). */
static void apply_cfg(uint8_t slot, uint8_t value) {
    switch (slot) {
        case SEISMO_CFG_ADXL_RANGE: nq_adxl355_set_range(&s_buses.adxl355, value);                       break;
        case SEISMO_CFG_ICM_ACCEL:  nq_icm42688_set_accel_range(&s_buses.icm, map_icm_accel(value));      break;
        case SEISMO_CFG_ICM_GYRO:   nq_icm42688_set_gyro_range(&s_buses.icm, map_icm_gyro(value));        break;
        case SEISMO_CFG_SCL_MODE:   nq_scl3300_init(&s_buses.scl3300, (value == 4) ? NQ_SCL3300_CMD_MODE4 : NQ_SCL3300_CMD_MODE1); break;
        default: break;   /* slot not used by this front */
    }
}

/* Dispatch a control frame to a lifecycle event / action. */
static nic_node_event_t handle_ctrl(const nic_link_ctrl_t *c, nic_node_state_t st) {
    if (c->station != s_addr && c->station != NIC_LINK_BROADCAST) return (nic_node_event_t)-1;
    /* Generic config slot (D28): apply this front's mapping, ACK with the slot opcode. */
    if (NIC_OP_CFG_IS(c->attribute)) {
        apply_cfg(NIC_OP_CFG_SLOT(c->attribute), c->value);
        queue_reply(NIC_OP_ACK, c->attribute);
        return (nic_node_event_t)-1;
    }
    switch (c->attribute) {
        /* Discovery: announce our DATA payload length (so the master can decode our
         * frames). Sent immediately in LISTEN — the master polls one node and waits. */
        case NIC_OP_DISCOVER:     queue_reply(NIC_OP_STATUS, node_payload_len()); return (nic_node_event_t)-1;
        /* Critical commands -> apply and queue an ACK for our next TDMA slot. */
        case NIC_OP_ASSIGN_ADDR:  s_addr = c->value; queue_reply(NIC_OP_ACK, NIC_OP_ASSIGN_ADDR); return (nic_node_event_t)-1;
        case NIC_OP_CALIBRATE:    return NIC_EV_CALIBRATE_CMD;   /* ACK queued after it runs */
        case NIC_OP_SYNC:         return NIC_EV_SYNC_CMD;
        case NIC_OP_TICK:         s_index = 0; return (nic_node_event_t)-1;  /* per-second anchor */
        /* CRC miss on our last DATA frame: the master addresses us and waits, so
         * resending the buffered frame now is collision-free (the master holds the
         * bus for us). The frame keeps its original sample index. */
        case NIC_OP_RESEND_DATA:  send_data(); return (nic_node_event_t)-1;
        case NIC_OP_HARD_RESET:   return NIC_EV_RESET_CMD;
        /* Queries -> answer in our TDMA slot (collision-free). */
        case NIC_OP_VERSION:      queue_reply(NIC_OP_VERSION, FW_VERSION); return (nic_node_event_t)-1;
        case NIC_OP_NODE_TYPE:    queue_reply(NIC_OP_NODE_TYPE, CFG_NODE_TYPE); return (nic_node_event_t)-1;  /* WHAT I am (D26) */
        case NIC_OP_HEALTH:       queue_reply(NIC_OP_HEALTH, (uint8_t)board_link_errors()); return (nic_node_event_t)-1;
        /* On-demand diagnostics (D24) — answered in our slot. */
        case NIC_OP_GET_VOLTAGE:  queue_reply(NIC_OP_GET_VOLTAGE, board_supply_input_dv());     return (nic_node_event_t)-1;
        case NIC_OP_SENSOR_TEST:  queue_reply(NIC_OP_SENSOR_TEST, sensor_present_mask());       return (nic_node_event_t)-1;
        case NIC_OP_GET_CPUTEMP:  queue_reply(NIC_OP_GET_CPUTEMP, (uint8_t)board_cpu_temp_c()); return (nic_node_event_t)-1;
        case NIC_OP_TOKEN:
            /* Explicit slot grant — repair / out-of-band only (the norm is the
             * self-timed slot, board_slot_due). A desynced node reports in-slot. */
            if (st != NIC_NODE_SYNCED) {
                queue_reply(NIC_OP_ERROR, NIC_ERRC_CLOCK_LOST);
                send_reply();
            } else {
                send_slot();
            }
            return (nic_node_event_t)-1;
        default:                 return (nic_node_event_t)-1;
    }
}

/* --- Main loop ------------------------------------------------------------- */

void nq_app_run(void) {
    board_init();
    board_sensor_buses(&s_buses);

    /* BOOT is handled inside the loop, so a HARD_RESET (-> BOOT) restores the
     * box's hardware number and re-runs detection instead of hanging. */
    nic_node_state_t st = NIC_NODE_BOOT;
    nic_node_step_t step;

    for (;;) {
        board_watchdog_kick();

        nic_node_event_t ev = (nic_node_event_t)-1;
        nic_node_action_t act = NIC_ACT_NONE;

        switch (st) {
            case NIC_NODE_BOOT:
                /* Power-up or hard reset: identify by the hardware number on the
                 * box (flash); the master re-applies any software slot afterwards. */
                s_addr = board_hw_addr();
                ev = NIC_EV_BOOT_DONE; break;     /* -> DETECT */

            case NIC_NODE_DETECT:
                nq_sensors_detect(&s_buses, &s_map);
                s_seismic = nq_sensors_seismic_source(&s_map);
                ev = NIC_EV_DETECTED; break;

            case NIC_NODE_INIT_SENSORS:
                configure_sensors();
                load_calibration();          /* #2: identity default + restore stored levelling */
                ev = NIC_EV_SENSORS_READY; break;

            case NIC_NODE_LISTEN: {
                uint8_t buf[64]; size_t n = 0;
                if (board_link_recv(buf, sizeof buf, &n, 1000u) == NIC_OK && n) {
                    nic_link_ctrl_t c;
                    if (nic_link_ctrl_decode(buf, n, &c) == NIC_OK) {
                        ev = handle_ctrl(&c, st);
                        /* Pre-sync there is no TDMA slot yet, but the master polls one
                         * node at a time and waits — so answer (STATUS / NODE_TYPE /
                         * ACK) right now; it cannot collide. */
                        send_reply();
                    }
                }
                break;
            }

            case NIC_NODE_SYNCED: {
                if (board_clock_lost_take()) { ev = NIC_EV_CLOCK_LOST; break; } /* HSE gone */
                if (board_drdy_take(s_seismic)) {
                    sample_to_buffer();                          /* DRDY: sample -> buffer */
                    if (s_hk_div == 0) poll_housekeeping();       /* #3 / D17: ~10 Hz, first sample + every */
                    if (++s_hk_div >= CFG_HK_DIV) s_hk_div = 0;   /* CFG_HK_DIV-th — not at CPU loop rate    */
                }
                /* Self-running TDMA (D24): transmit in our own clock-derived slot —
                 * fired when the predecessor's frame is overheard or our slot deadline
                 * elapses. No per-sample token; disjoint slots, so it can't collide. */
                if (board_slot_due(s_addr)) send_slot();
                uint8_t buf[64]; size_t n = 0;
                if (board_link_recv(buf, sizeof buf, &n, 0u) == NIC_OK && n) {
                    nic_link_ctrl_t c;
                    if (nic_link_ctrl_decode(buf, n, &c) == NIC_OK) ev = handle_ctrl(&c, st);
                }
                break;
            }

            case NIC_NODE_FAULT:
                ev = NIC_EV_BOOT_DONE; break;   /* recovered -> back to LISTEN */

            default: break;
        }

        if (ev == (nic_node_event_t)-1) continue;
        step = nic_node_step(st, ev);
        st = step.state;
        act = step.action;

        switch (act) {
            case NIC_ACT_SWITCH_HSE:
                if (nic_clocksync_to_hse(board_clocksync_ops()) == NIC_OK) {
                    board_sensor_clocks_start();
                    step = nic_node_step(st, NIC_EV_HSE_OK); st = step.state;
                }
                break;
            case NIC_ACT_CALIBRATE: do_calibration(); queue_reply(NIC_OP_ACK, NIC_OP_CALIBRATE); break;
            case NIC_ACT_RECOVER:
                /* Recover to RC and go back to LISTEN. We do NOT announce the
                 * loss here — transmitting asynchronously would collide with another
                 * node's slot. The master learns instead when our slot stays silent,
                 * or we report ERROR in-slot once we are back in sync. */
                nic_clocksync_recover(board_clocksync_ops());
                break;
            default: break;
        }
    }
}
