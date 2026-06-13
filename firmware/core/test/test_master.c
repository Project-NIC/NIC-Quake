/*
 * Host-side test for nq-master: topology, token scheduler, DATA ingest, and
 * CONTROL command building — all over the shared nq-link transport.
 */

#include "nic_master.h"
#include "nic_link.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { printf("  FAIL: %s (line %d)\n", #cond, __LINE__); g_failures++; } \
} while (0)

static void test_topology(void) {
    printf("test_topology\n");
    nic_master_t m;
    nic_master_init(&m);
    CHECK(nic_master_add_node(&m, 1, 25) == 0);
    CHECK(nic_master_add_node(&m, 2, 13) == 1);
    CHECK(nic_master_add_node(&m, 1, 19) == 0);   /* dedup -> refresh slot 0 */
    CHECK(m.count == 2);
    CHECK(m.nodes[0].payload_len == 19);
}

static void test_token_round_robin(void) {
    printf("test_token_round_robin\n");
    nic_master_t m;
    nic_master_init(&m);
    CHECK(nic_master_next_token(&m) == -1);        /* no nodes yet */
    nic_master_add_node(&m, 4, 25);
    nic_master_add_node(&m, 7, 25);
    nic_master_add_node(&m, 2, 25);
    CHECK(nic_master_next_token(&m) == 4);
    CHECK(nic_master_next_token(&m) == 7);
    CHECK(nic_master_next_token(&m) == 2);
    CHECK(nic_master_next_token(&m) == 4);         /* wraps */
}

static void test_table_full(void) {
    printf("test_table_full\n");
    nic_master_t m;
    nic_master_init(&m);
    for (int i = 0; i < (int)NIC_MASTER_MAX_NODES; i++) {
        CHECK(nic_master_add_node(&m, (uint8_t)(i + 1), 25) >= 0);
    }
    CHECK(nic_master_add_node(&m, 99, 25) == NIC_ERR);   /* full */
}

static void test_ingest(void) {
    printf("test_ingest\n");
    nic_master_t m;
    nic_master_init(&m);
    const uint8_t payload[] = { 0x11, 0x22, 0x33, 0x44 };
    nic_master_add_node(&m, 3, sizeof payload);

    uint8_t frame[32];
    int n = nic_link_data_encode(3, 0x0102, payload, sizeof payload, frame, sizeof frame);

    nic_master_record_t rec;
    CHECK(nic_master_ingest(&m, frame, (size_t)n, &rec) == NIC_OK);
    CHECK(rec.station == 3 && rec.index == 0x0102 && rec.len == sizeof payload);
    CHECK(rec.payload && memcmp(rec.payload, payload, sizeof payload) == 0);

    /* unknown station */
    frame[1] = 9;
    CHECK(nic_master_ingest(&m, frame, (size_t)n, &rec) == NIC_ERR);
    frame[1] = 3;

    /* corrupt -> CRC */
    frame[NIC_LINK_DATA_HEADER] ^= 0x01u;
    CHECK(nic_master_ingest(&m, frame, (size_t)n, &rec) == NIC_ERR_CRC);
}

static void test_clock_loss_tracking(void) {
    printf("test_clock_loss_tracking\n");
    nic_master_t m;
    nic_master_init(&m);
    const uint8_t pl[] = { 1, 2 };
    nic_master_add_node(&m, 1, sizeof pl);
    nic_master_add_node(&m, 2, sizeof pl);

    /* fresh nodes are not yet synced */
    CHECK(m.nodes[0].synced == 0 && m.nodes[1].synced == 0);

    /* a good DATA frame from node 1 marks it synced */
    uint8_t f[16];
    int n = nic_link_data_encode(1, 0, pl, sizeof pl, f, sizeof f);
    nic_master_record_t rec;
    CHECK(nic_master_ingest(&m, f, (size_t)n, &rec) == NIC_OK);
    CHECK(m.nodes[0].synced == 1);

    /* node 1 reports clock loss (or its token times out) -> lost */
    CHECK(nic_master_node_lost(&m, 1) == 0);
    CHECK(m.nodes[0].synced == 0);
    CHECK(nic_master_node_lost(&m, 99) == NIC_ERR);     /* unknown */

    /* not all lost yet (node 2 never answered, but only one is "was-synced");
     * once both are desynced after having been seen, all_lost flags the
     * master's own clock. */
    nic_master_ingest(&m, f, (size_t)n, &rec);          /* node 1 back */
    n = nic_link_data_encode(2, 0, pl, sizeof pl, f, sizeof f);
    nic_master_ingest(&m, f, (size_t)n, &rec);          /* node 2 synced */
    CHECK(m.nodes[0].synced == 1 && m.nodes[1].synced == 1);
    CHECK(nic_master_all_lost(&m) == 0);
    nic_master_node_lost(&m, 1);
    nic_master_node_lost(&m, 2);
    CHECK(nic_master_all_lost(&m) == 1);                /* -> master oscillator suspect */
}

static void test_gap_detection(void) {
    printf("test_gap_detection\n");
    nic_master_t m;
    nic_master_init(&m);
    const uint8_t pl[] = { 0xAA, 0xBB };
    nic_master_add_node(&m, 1, sizeof pl);

    uint8_t f[16];
    nic_master_record_t rec;
    int n;

    n = nic_link_data_encode(1, 0, pl, sizeof pl, f, sizeof f);
    nic_master_ingest(&m, f, (size_t)n, &rec); CHECK(rec.gap == 0);
    n = nic_link_data_encode(1, 1, pl, sizeof pl, f, sizeof f);
    nic_master_ingest(&m, f, (size_t)n, &rec); CHECK(rec.gap == 0);
    n = nic_link_data_encode(1, 3, pl, sizeof pl, f, sizeof f);   /* skipped index 2 */
    nic_master_ingest(&m, f, (size_t)n, &rec); CHECK(rec.gap == 1);

    /* an older index (e.g. a RESEND) reports gap 0 and must NOT drag exp_index back */
    n = nic_link_data_encode(1, 2, pl, sizeof pl, f, sizeof f);   /* out-of-order / resent */
    nic_master_ingest(&m, f, (size_t)n, &rec); CHECK(rec.gap == 0);
    n = nic_link_data_encode(1, 4, pl, sizeof pl, f, sizeof f);   /* exp_index still 4 -> gap 0 */
    nic_master_ingest(&m, f, (size_t)n, &rec); CHECK(rec.gap == 0);

    nic_master_on_tick(&m);                                       /* second boundary */
    n = nic_link_data_encode(1, 0, pl, sizeof pl, f, sizeof f);
    nic_master_ingest(&m, f, (size_t)n, &rec); CHECK(rec.gap == 0);
}

static void test_ack(void) {
    printf("test_ack\n");
    nic_master_t m;
    nic_master_init(&m);
    nic_master_add_node(&m, 1, 2);

    CHECK(nic_master_expect_ack(&m, 1, NIC_OP_CALIBRATE) == 0);
    CHECK(nic_master_pending(&m, 1) == NIC_OP_CALIBRATE);

    nic_link_ctrl_t wrong = { 1, NIC_OP_ACK, NIC_OP_SYNC };
    nic_master_on_ctrl(&m, &wrong);
    CHECK(nic_master_pending(&m, 1) == NIC_OP_CALIBRATE);          /* mismatch -> still pending */

    nic_link_ctrl_t ok = { 1, NIC_OP_ACK, NIC_OP_CALIBRATE };
    nic_master_on_ctrl(&m, &ok);
    CHECK(nic_master_pending(&m, 1) == 0);                        /* matched -> cleared */
}

static void test_on_ctrl_dispatch(void) {
    printf("test_on_ctrl_dispatch\n");
    nic_master_t m;
    nic_master_init(&m);

    nic_link_ctrl_t status = { 5, NIC_OP_STATUS, 25 };            /* discovery reply */
    CHECK(nic_master_on_ctrl(&m, &status) == NIC_OK);
    CHECK(m.count == 1 && m.nodes[0].addr == 5 && m.nodes[0].payload_len == 25);
    CHECK(m.nodes[0].type == NIC_NODE_TYPE_UNKNOWN);             /* not learned until NODE_TYPE */

    nic_link_ctrl_t typ = { 5, NIC_OP_NODE_TYPE, NIC_NODE_TYPE_SEISMO };
    nic_master_on_ctrl(&m, &typ);
    CHECK(m.nodes[0].type == NIC_NODE_TYPE_SEISMO);              /* D26: WHAT it is */

    m.nodes[0].synced = 1;
    nic_link_ctrl_t err = { 5, NIC_OP_ERROR, NIC_ERRC_CLOCK_LOST };
    nic_master_on_ctrl(&m, &err);
    CHECK(m.nodes[0].synced == 0);

    nic_link_ctrl_t hp = { 5, NIC_OP_HEALTH, 7 };
    nic_master_on_ctrl(&m, &hp);
    CHECK(m.nodes[0].link_errors == 7);

    nic_link_ctrl_t ver = { 5, NIC_OP_VERSION, 3 };
    nic_master_on_ctrl(&m, &ver);
    CHECK(m.nodes[0].version == 3);

    nic_link_ctrl_t volt = { 5, NIC_OP_GET_VOLTAGE, 120 };
    nic_master_on_ctrl(&m, &volt);
    CHECK(m.nodes[0].volt_dv == 120);

    nic_link_ctrl_t cput = { 5, NIC_OP_GET_CPUTEMP, (uint8_t)(int8_t)-5 };
    nic_master_on_ctrl(&m, &cput);
    CHECK(m.nodes[0].cpu_temp_c == -5);

    nic_link_ctrl_t stst = { 5, NIC_OP_SENSOR_TEST, 0x07 };
    nic_master_on_ctrl(&m, &stst);
    CHECK(m.nodes[0].sensor_mask == 0x07);

    nic_link_ctrl_t unknown = { 9, NIC_OP_HEALTH, 1 };
    CHECK(nic_master_on_ctrl(&m, &unknown) == NIC_ERR);
}

static void test_cmd_roundtrip(void) {
    printf("test_cmd_roundtrip\n");
    uint8_t frame[NIC_LINK_CTRL_LEN];
    int n = nic_master_cmd(NIC_LINK_BROADCAST, NIC_OP_SYNC, 0, frame, sizeof frame);
    CHECK(n == (int)NIC_LINK_CTRL_LEN);

    nic_link_ctrl_t c;
    CHECK(nic_link_ctrl_decode(frame, (size_t)n, &c) == NIC_OK);
    CHECK(c.station == NIC_LINK_BROADCAST && c.attribute == NIC_OP_SYNC);

    n = nic_master_cmd(5, NIC_OP_ASSIGN_ADDR, 20, frame, sizeof frame);
    CHECK(nic_link_ctrl_decode(frame, (size_t)n, &c) == NIC_OK);
    CHECK(c.station == 5 && c.attribute == NIC_OP_ASSIGN_ADDR && c.value == 20);

    /* per-second tick: broadcast, NIC_OP_TICK */
    n = nic_master_tick(frame, sizeof frame);
    CHECK(nic_link_ctrl_decode(frame, (size_t)n, &c) == NIC_OK);
    CHECK(c.station == NIC_LINK_BROADCAST && c.attribute == NIC_OP_TICK);
}

static void test_cfg_slots(void) {
    printf("test_cfg_slots\n");
    /* D28: config slots ride the attribute byte; the value byte carries the setting. */
    CHECK(NIC_OP_CFG_IS(NIC_OP_CFG(0)) && NIC_OP_CFG_IS(NIC_OP_CFG(7)));
    CHECK(!NIC_OP_CFG_IS(NIC_OP_NODE_TYPE));
    CHECK(NIC_OP_CFG_SLOT(NIC_OP_CFG(3)) == 3);

    uint8_t f[NIC_LINK_CTRL_LEN];
    int n = nic_master_cmd(5, NIC_OP_CFG(2), 15, f, sizeof f);   /* seismo slot 2 = ICM gyro, 15 dps */
    nic_link_ctrl_t c;
    CHECK(nic_link_ctrl_decode(f, (size_t)n, &c) == NIC_OK);
    CHECK(c.station == 5 && c.attribute == NIC_OP_CFG(2) && c.value == 15);

    /* the node ACKs with the slot opcode, which clears the pending command */
    nic_master_t m; nic_master_init(&m);
    nic_master_add_node(&m, 5, 28);
    nic_master_expect_ack(&m, 5, NIC_OP_CFG(2));
    CHECK(nic_master_pending(&m, 5) == (int)NIC_OP_CFG(2));
    nic_link_ctrl_t ack = { 5, NIC_OP_ACK, NIC_OP_CFG(2) };
    nic_master_on_ctrl(&m, &ack);
    CHECK(nic_master_pending(&m, 5) == 0);
}

int main(void) {
    test_topology();
    test_token_round_robin();
    test_table_full();
    test_ingest();
    test_clock_loss_tracking();
    test_gap_detection();
    test_ack();
    test_on_ctrl_dispatch();
    test_cmd_roundtrip();
    test_cfg_slots();

    if (g_failures == 0) {
        printf("OK: all checks passed\n");
        return 0;
    }
    printf("FAILED: %d check(s)\n", g_failures);
    return 1;
}
