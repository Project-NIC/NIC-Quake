/*
 * Host-side test for nq-node: the lifecycle state machine and the data packer.
 */

#include "nic_node.h"

#include <stdio.h>

static int g_failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { printf("  FAIL: %s (line %d)\n", #cond, __LINE__); g_failures++; } \
} while (0)

static void test_happy_path(void) {
    printf("test_happy_path\n");
    nic_node_step_t s;

    s = nic_node_step(NIC_NODE_BOOT, NIC_EV_BOOT_DONE);
    CHECK(s.state == NIC_NODE_DETECT && s.action == NIC_ACT_DETECT);

    s = nic_node_step(NIC_NODE_DETECT, NIC_EV_DETECTED);
    CHECK(s.state == NIC_NODE_INIT_SENSORS && s.action == NIC_ACT_INIT_SENSORS);

    s = nic_node_step(NIC_NODE_INIT_SENSORS, NIC_EV_SENSORS_READY);
    CHECK(s.state == NIC_NODE_LISTEN && s.action == NIC_ACT_LISTEN);

    s = nic_node_step(NIC_NODE_LISTEN, NIC_EV_SYNC_CMD);
    CHECK(s.state == NIC_NODE_LISTEN && s.action == NIC_ACT_SWITCH_HSE);

    s = nic_node_step(NIC_NODE_LISTEN, NIC_EV_HSE_OK);
    CHECK(s.state == NIC_NODE_SYNCED && s.action == NIC_ACT_START_SAMPLING);

    s = nic_node_step(NIC_NODE_SYNCED, NIC_EV_CALIBRATE_CMD);
    CHECK(s.state == NIC_NODE_SYNCED && s.action == NIC_ACT_CALIBRATE);
}

static void test_clock_loss_recovers_from_anywhere(void) {
    printf("test_clock_loss_recovers_from_anywhere\n");
    nic_node_step_t s = nic_node_step(NIC_NODE_SYNCED, NIC_EV_CLOCK_LOST);
    CHECK(s.state == NIC_NODE_FAULT && s.action == NIC_ACT_RECOVER);

    /* After recovery the node is back listening on RC. */
    s = nic_node_step(NIC_NODE_FAULT, NIC_EV_BOOT_DONE);
    CHECK(s.state == NIC_NODE_LISTEN && s.action == NIC_ACT_LISTEN);
}

static void test_reset_returns_to_boot(void) {
    printf("test_reset_returns_to_boot\n");
    nic_node_step_t s = nic_node_step(NIC_NODE_SYNCED, NIC_EV_RESET_CMD);
    CHECK(s.state == NIC_NODE_BOOT && s.action == NIC_ACT_NONE);
}

static void test_unhandled_event_is_noop(void) {
    printf("test_unhandled_event_is_noop\n");
    nic_node_step_t s = nic_node_step(NIC_NODE_BOOT, NIC_EV_HSE_OK);
    CHECK(s.state == NIC_NODE_BOOT && s.action == NIC_ACT_NONE);
}

static void test_pack_layout(void) {
    printf("test_pack_layout\n");
    nic_node_field_t fields[2] = {
        { .sample = {{ 0x12345, -65536, 1 }}, .bytes_per_axis = 3 },  /* ADXL, 20-bit */
        { .sample = {{ 0x1234, -1, -32768 }}, .bytes_per_axis = 2 },  /* ICM, 16-bit  */
    };
    uint8_t out[64];
    int n = nic_node_pack(fields, 2, out, sizeof out);
    CHECK(n == 9 + 6);

    /* field 0, 3 bytes/axis, big-endian */
    CHECK(out[0] == 0x01 && out[1] == 0x23 && out[2] == 0x45);   /* 0x12345 */
    CHECK(out[3] == 0xFF && out[4] == 0x00 && out[5] == 0x00);   /* -65536  */
    CHECK(out[6] == 0x00 && out[7] == 0x00 && out[8] == 0x01);   /* 1       */
    /* field 1, 2 bytes/axis */
    CHECK(out[9] == 0x12 && out[10] == 0x34);                    /* 0x1234  */
    CHECK(out[11] == 0xFF && out[12] == 0xFF);                   /* -1      */
    CHECK(out[13] == 0x80 && out[14] == 0x00);                   /* -32768  */
}

static void test_pack_enob_reduction(void) {
    printf("test_pack_enob_reduction\n");
    nic_node_field_t fields[2] = {
        { .sample = {{ 0x12345, -0x12345, 0x40000 }}, .drop_bits = 4, .bytes_per_axis = 2 }, /* ADXL 20->16 */
        { .sample = {{ 0x1234, -1, -32768 }},         .drop_bits = 0, .bytes_per_axis = 2 }, /* ICM 16-bit  */
    };
    uint8_t out[32];
    int n = nic_node_pack(fields, 2, out, sizeof out);
    CHECK(n == 6 + 6);
    CHECK(out[0] == 0x12 && out[1] == 0x34);    /* 0x12345 >> 4 = 0x1234   */
    CHECK(out[2] == 0xED && out[3] == 0xCC);    /* -0x12345 >> 4 = -4660   */
    CHECK(out[4] == 0x40 && out[5] == 0x00);    /* 0x40000 >> 4 = 0x4000   */
    CHECK(out[6] == 0x12 && out[7] == 0x34);    /* ICM passes through      */
    CHECK(out[8] == 0xFF && out[9] == 0xFF);
    CHECK(out[10] == 0x80 && out[11] == 0x00);
}

static void test_pack_saturation(void) {
    printf("test_pack_saturation\n");
    nic_node_field_t f = { .sample = {{ 5000, -5000, 100 }}, .drop_bits = 0, .bytes_per_axis = 1 };
    uint8_t out[16];
    int n = nic_node_pack(&f, 1, out, sizeof out);
    CHECK(n == 3);
    CHECK(out[0] == 0x7F);    /* +5000 clips to +127 */
    CHECK(out[1] == 0x80);    /* -5000 clips to -128 */
    CHECK(out[2] == 100);     /* in range            */
}

static void test_pack_guards(void) {
    printf("test_pack_guards\n");
    nic_node_field_t f = { .sample = {{ 1, 2, 3 }}, .bytes_per_axis = 3 };
    uint8_t out[4];
    CHECK(nic_node_pack(&f, 1, out, sizeof out) == NIC_ERR);   /* too small */

    nic_node_field_t bad = { .sample = {{ 0, 0, 0 }}, .bytes_per_axis = 5 };
    uint8_t big[64];
    CHECK(nic_node_pack(&bad, 1, big, sizeof big) == NIC_ERR); /* bad width */

    CHECK(nic_node_pack(NULL, 0, big, sizeof big) == 0);      /* no fields -> empty */
}

int main(void) {
    test_happy_path();
    test_clock_loss_recovers_from_anywhere();
    test_reset_returns_to_boot();
    test_unhandled_event_is_noop();
    test_pack_layout();
    test_pack_enob_reduction();
    test_pack_saturation();
    test_pack_guards();

    if (g_failures == 0) {
        printf("OK: all checks passed\n");
        return 0;
    }
    printf("FAILED: %d check(s)\n", g_failures);
    return 1;
}
