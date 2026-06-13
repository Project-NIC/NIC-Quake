/*
 * NIC-Quake MASTER application — the ESP32 head-end. Composes the portable
 * nq-master + nq-lora cores via the board_master layer. Like the node's
 * main_app.c, this is the hardware-side glue: NOT host-built, NOT host-tested
 * (the cores it drives ARE). NOT validated on a board.
 *
 * Flow (SOFTWARE.md / D23 / D24 / D25):
 *   discover by hardware number -> assign slots -> start clock + SYNC -> run:
 *   TICK each second, ingest DATA -> NIC-MLA, ask on demand, KO-on-silence,
 *   periodic 16-byte LoRa telemetry (only downlink = resend), optional Wi-Fi
 *   upload.
 */

#include "board_master.h"
#include "nic_master.h"
#include "nic_lora.h"
#include "nq_detect.h"
#include "nic_proto.h"
#include "nic_link.h"

#define CFG_MASTER_ID       1u
#define LORA_PERIOD_S     900u    /* periodic telemetry every ~15 min (duty cycle) */
#define STRONG_COOLDOWN_S  60u    /* min gap between immediate strong-event frames  */

static nic_master_t s_m;
static nq_detect_t s_det;               /* STA/LTA seismic trigger (primary node)        */
static uint16_t    s_events;            /* event count since last frame ("pocet kmitu")  */
static uint16_t    s_peak;              /* max event peak since last frame               */
static uint8_t     s_net_status;        /* OR of node D24 status since last frame         */
static uint32_t    s_last_strong;       /* last immediate strong-event frame (cooldown)  */
static uint8_t     s_lora_frame[NIC_LORA_LEN];   /* last telemetry, buffered for resend */
static int         s_lora_len;

/* Send a control frame to a node (0xFF = broadcast). */
static void cmd(uint8_t station, uint8_t op, uint8_t value) {
    uint8_t f[NIC_LINK_CTRL_LEN];
    int n = nic_master_cmd(station, op, value, f, sizeof f);
    if (n > 0) board_master_send(f, (size_t)n);
}

/* The seismic magnitude the trigger runs on: |x|+|y|+|z| of the precise (ADXL355)
 * triple — the first 6 payload bytes on a node (3x int16, big-endian). */
static int32_t seismic_mag(const nic_master_record_t *rec) {
    if (rec->len < 6 || rec->payload == NULL) return 0;
    const uint8_t *p = rec->payload;
    int32_t x = (int16_t)(((uint16_t)p[0] << 8) | p[1]);
    int32_t y = (int16_t)(((uint16_t)p[2] << 8) | p[3]);
    int32_t z = (int16_t)(((uint16_t)p[4] << 8) | p[5]);
    int32_t ax = x < 0 ? -x : x, ay = y < 0 ? -y : y, az = z < 0 ? -z : z;
    return ax + ay + az;
}

/* Receive one control reply and, if its attribute matches `want`, fold it into the
 * topology. Returns 1 on a matching reply. (DISCOVER's reply attribute is STATUS;
 * NODE_TYPE's is NODE_TYPE — so the caller passes the attribute it expects back.) */
static int await_ctrl(uint8_t want) {
    uint8_t buf[NIC_LINK_CTRL_LEN]; size_t n = 0;
    nic_link_ctrl_t c;
    if (board_master_recv(buf, sizeof buf, &n, 50u) == NIC_OK && n
        && nic_link_ctrl_decode(buf, n, &c) == NIC_OK && c.attribute == want) {
        nic_master_on_ctrl(&s_m, &c);
        return 1;
    }
    return 0;
}

/* 1. Discovery: poll hardware numbers. A present node answers DISCOVER with STATUS
 * (= its DATA payload length, so we can decode its frames); we then ask NODE_TYPE —
 * the number tells WHICH box, the type tells WHAT KIND (D26). Finally assign slots
 * (= software addresses; D23/D24 — no separate set-token-order). */
static void discover_and_assign(void) {
    nic_master_init(&s_m);
    for (uint8_t hw = 1; hw <= NIC_MASTER_MAX_NODES; hw++) {
        cmd(hw, NIC_OP_DISCOVER, 0);
        if (await_ctrl(NIC_OP_STATUS)) {            /* present -> registered by hardware number */
            cmd(hw, NIC_OP_NODE_TYPE, 0);
            await_ctrl(NIC_OP_NODE_TYPE);           /* record WHAT it is (best-effort) */
        }
    }
    for (int i = 0; i < s_m.count; i++) {               /* contiguous slot = software addr */
        uint8_t slot = (uint8_t)(i + 1);
        cmd(s_m.nodes[i].addr, NIC_OP_ASSIGN_ADDR, slot);
        s_m.nodes[i].addr = slot;
    }
}

/* 2. Start: gate the clock onto the bus and switch everyone to it. */
static void start_network(void) {
    board_master_clock_start();                         /* 8.192 MHz onto the clock pair */
    cmd(NIC_LINK_BROADCAST, NIC_OP_SYNC, 0);              /* nodes -> HSE + start sampling */
}

/* The 16-byte LoRa telemetry frame: values, not warnings (D25). Buffered so a
 * bad-reception receiver can ask for a resend — the ONLY LoRa downlink. */
static void lora_telemetry(uint32_t now) {
    nic_lora_status_t st = { 0 };
    st.master  = CFG_MASTER_ID;
    st.unix_s  = now;
    st.volt_dv = board_master_supply_dv();
    st.temp_c  = board_master_temp_c();
    st.events  = s_events;
    st.peak    = s_peak;
    st.status  = s_net_status;
    for (int i = 0; i < s_m.count && i < 8; i++) {
        if (s_m.nodes[i].synced) st.alive |= (uint8_t)(1u << i);
    }
    s_lora_len = nic_lora_pack(&st, s_lora_frame, sizeof s_lora_frame);
    if (s_lora_len == (int)NIC_LORA_LEN) board_master_lora_send(s_lora_frame, NIC_LORA_LEN);
    s_net_status = 0u;                                  /* reset the per-period accumulators */
    s_events     = 0u;
    s_peak       = 0u;
}

/* 3. Run loop. */
void nq_master_app_run(void) {
    board_master_init();
    discover_and_assign();
    start_network();
    nq_detect_init(&s_det);

    uint32_t last_sec  = board_master_rtc_now();
    uint32_t last_lora = last_sec;

    for (;;) {
        board_master_watchdog_kick();
        uint32_t now = board_master_rtc_now();

        /* once-per-second TICK re-anchors every node's sub-second index */
        if (now != last_sec) {
            uint8_t f[NIC_LINK_CTRL_LEN];
            int n = nic_master_tick(f, sizeof f);
            if (n > 0) board_master_send(f, (size_t)n);
            nic_master_on_tick(&s_m);
            last_sec = now;
            if (now - last_lora >= LORA_PERIOD_S) { lora_telemetry(now); last_lora = now; }
        }

        /* whatever is on the bus this slot window */
        uint8_t buf[64]; size_t n = 0;
        if (board_master_recv(buf, sizeof buf, &n, 1u) == NIC_OK && n) {
            if (buf[0] == NIC_LINK_MAGIC_DATA) {
                nic_master_record_t rec;
                int r = nic_master_ingest(&s_m, buf, n, &rec);
                if (r == NIC_OK) {
                    board_master_store(&rec, now);                  /* -> NIC-GLUE-IN -> DMD -> MLA */
                    if (rec.len) s_net_status |= rec.payload[rec.len - 1];  /* OR the D24 status byte */
                    /* Seismic trigger on the primary node (per-node detection is the
                     * fuller design; here the LoRa summary follows node 0). */
                    if (s_m.count && rec.station == s_m.nodes[0].addr) {
                        nq_detect_result_t ev = nq_detect_push(&s_det, seismic_mag(&rec));
                        if (ev.occurred) {
                            s_events++;
                            if (ev.peak > (int32_t)s_peak)
                                s_peak = (ev.peak > 0xFFFF) ? 0xFFFFu : (uint16_t)ev.peak;
                            /* strong -> LoRa now, rate-limited so aftershocks don't flood */
                            if (ev.strong && (uint32_t)(now - s_last_strong) >= STRONG_COOLDOWN_S) {
                                lora_telemetry(now);
                                last_lora = now;
                                s_last_strong = now;
                            }
                        }
                    }
                } else if (r == NIC_ERR_CRC) {
                    cmd(buf[1], NIC_OP_RESEND_DATA, 0);              /* ask the node to resend, in-slot */
                }
            } else if (buf[0] == NIC_LINK_MAGIC_CTRL) {
                nic_link_ctrl_t c;
                if (nic_link_ctrl_decode(buf, n, &c) == NIC_OK) nic_master_on_ctrl(&s_m, &c);
            }
        }

        /* KO-on-silence: a slot that produced no data this round -> node lost
         * (master writes zeros + a KO marker; D24). */
        int dead = board_master_silent_slot();
        if (dead > 0) {
            nic_master_node_lost(&s_m, (uint8_t)dead);
            board_master_store_ko((uint8_t)dead, now);
        }
        if (nic_master_all_lost(&s_m)) board_master_clock_suspect();  /* our OWN oscillator? */

        /* LoRa: the only downlink is "resend the last telemetry" on bad reception. */
        if (board_master_lora_resend_requested() && s_lora_len == (int)NIC_LORA_LEN) {
            board_master_lora_send(s_lora_frame, NIC_LORA_LEN);
        }

        /* Wi-Fi: optional, decoupled — when connected, stream the pending archive. */
        if (board_master_wifi_up()) board_master_wifi_upload();
    }
}
