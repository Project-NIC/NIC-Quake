/* Host-side test for nic-uplink: the head-end batch spool. Records accumulate
 * and flush on N-or-T; the framed batch round-trips (magic / fields / records /
 * CRC); the three sink modes and the store-fail retain path behave. */

#include "nic_uplink.h"
#include "nic_crc.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { printf("  FAIL: %s (line %d)\n", #cond, __LINE__); g_failures++; } \
} while (0)

/* --- capturing sinks ---------------------------------------------------- */
static uint8_t g_store_buf[1024];
static size_t  g_store_len;
static int     g_store_calls;
static int     g_store_fail;

static uint8_t g_send_buf[1024];
static size_t  g_send_len;
static int     g_send_calls;
static int     g_send_fail;

static int store_sink(void *ctx, const uint8_t *b, size_t n) {
    (void)ctx; g_store_calls++;
    if (g_store_fail) return -1;
    memcpy(g_store_buf, b, n); g_store_len = n; return 0;
}
static int send_sink(void *ctx, const uint8_t *b, size_t n) {
    (void)ctx; g_send_calls++;
    if (g_send_fail) return -1;
    memcpy(g_send_buf, b, n); g_send_len = n; return 0;
}
static void sinks_reset(void) {
    g_store_len = g_send_len = 0;
    g_store_calls = g_send_calls = 0;
    g_store_fail = g_send_fail = 0;
}

/* A well-formed batch: length, header fields, and a valid trailing CRC. */
static int batch_ok(const uint8_t *b, size_t n, uint8_t src, uint8_t rl,
                    uint8_t cnt, uint32_t base) {
    if (n != (size_t)NIC_UPLINK_HEADER + (size_t)cnt * rl + 2u) return 0;
    if (b[0] != NIC_UPLINK_MAGIC || b[1] != src || b[2] != rl || b[3] != cnt) return 0;
    uint32_t u = ((uint32_t)b[4] << 24) | ((uint32_t)b[5] << 16)
               | ((uint32_t)b[6] << 8) |  (uint32_t)b[7];
    if (u != base) return 0;
    uint16_t crc = nic_crc16_ccitt(b, n - 2u);
    if ((uint16_t)(((uint16_t)b[n - 2] << 8) | b[n - 1]) != crc) return 0;
    return 1;
}

static uint8_t g_buf[NIC_UPLINK_HEADER + 255u * 28u + 2u];

int main(void) {
    printf("test_uplink\n");

    const uint32_t BASE = 0x12345678u;

    /* T1 — flush by N, and the framed batch carries the records intact. */
    sinks_reset();
    nic_uplink_t u = { .buf = g_buf, .buf_cap = sizeof g_buf, .source = 7,
                       .record_len = 4, .max_records = 3, .max_ms = 1000,
                       .store = store_sink, .send = send_sink, .ctx = NULL };
    uint8_t r0[4] = { 0xA0, 0xA1, 0xA2, 0xA3 };
    uint8_t r1[4] = { 0xB0, 0xB1, 0xB2, 0xB3 };
    uint8_t r2[4] = { 0xC0, 0xC1, 0xC2, 0xC3 };
    CHECK(nic_uplink_push(&u, r0, BASE, 100) == 0);
    CHECK(nic_uplink_push(&u, r1, BASE + 9, 110) == 0);   /* later stamps ignored */
    CHECK(nic_uplink_push(&u, r2, BASE + 9, 120) == (NIC_UPLINK_STORED | NIC_UPLINK_SENT));
    CHECK(u.count == 0);                                   /* flushed + reset */
    CHECK(g_store_calls == 1 && g_send_calls == 1);
    CHECK(g_store_len == g_send_len && memcmp(g_store_buf, g_send_buf, g_store_len) == 0);
    CHECK(batch_ok(g_store_buf, g_store_len, 7, 4, 3, BASE));
    CHECK(memcmp(&g_store_buf[NIC_UPLINK_HEADER + 0], r0, 4) == 0);
    CHECK(memcmp(&g_store_buf[NIC_UPLINK_HEADER + 4], r1, 4) == 0);
    CHECK(memcmp(&g_store_buf[NIC_UPLINK_HEADER + 8], r2, 4) == 0);

    /* T2 — flush by T (tick), N not reached; elapsed compare is wrap-safe. */
    sinks_reset();
    nic_uplink_t t = { .buf = g_buf, .buf_cap = sizeof g_buf, .source = 1,
                       .record_len = 4, .max_records = 255, .max_ms = 1000,
                       .store = store_sink, .send = NULL, .ctx = NULL };
    CHECK(nic_uplink_push(&t, r0, BASE, 100) == 0);
    CHECK(nic_uplink_push(&t, r1, BASE, 200) == 0);
    CHECK(nic_uplink_tick(&t, 900) == NIC_UPLINK_EMPTY);   /* 800 ms < 1000 */
    CHECK(t.count == 2);
    CHECK(nic_uplink_tick(&t, 1100) == NIC_UPLINK_STORED);  /* 1000 ms >= 1000 */
    CHECK(t.count == 0);
    CHECK(batch_ok(g_store_buf, g_store_len, 1, 4, 2, BASE));
    /* wrap: the batch opens near UINT32_MAX and now_ms wraps past zero — the
     * elapsed compare must still be correct (unsigned subtraction). */
    sinks_reset();
    CHECK(nic_uplink_push(&t, r0, BASE, 0xFFFFFF00u) == 0);
    CHECK(nic_uplink_tick(&t, 0x00000064u) == NIC_UPLINK_EMPTY);   /* +356 ms < 1000 */
    CHECK(t.count == 1);
    CHECK(nic_uplink_tick(&t, 0x000002E8u) == NIC_UPLINK_STORED);  /* +1000 ms */
    CHECK(t.count == 0);
    CHECK(batch_ok(g_store_buf, g_store_len, 1, 4, 1, BASE));

    /* T3 — sink modes: store-only and send-only. */
    sinks_reset();
    nic_uplink_t so = { .buf = g_buf, .buf_cap = sizeof g_buf, .source = 2,
                        .record_len = 4, .max_records = 1, .max_ms = 0,
                        .store = store_sink, .send = NULL, .ctx = NULL };
    CHECK(nic_uplink_push(&so, r0, BASE, 0) == NIC_UPLINK_STORED);
    CHECK(g_store_calls == 1 && g_send_calls == 0);

    sinks_reset();
    nic_uplink_t se = { .buf = g_buf, .buf_cap = sizeof g_buf, .source = 3,
                        .record_len = 4, .max_records = 1, .max_ms = 0,
                        .store = NULL, .send = send_sink, .ctx = NULL };
    CHECK(nic_uplink_push(&se, r0, BASE, 0) == NIC_UPLINK_SENT);
    CHECK(g_store_calls == 0 && g_send_calls == 1);

    /* T4 — durable sink fails: ESTORE, batch retained, live sink not attempted;
     * a later flush (storage recovered) ships the same retained records. */
    sinks_reset();
    g_store_fail = 1;
    nic_uplink_t f = { .buf = g_buf, .buf_cap = sizeof g_buf, .source = 9,
                       .record_len = 4, .max_records = 2, .max_ms = 0,
                       .store = store_sink, .send = send_sink, .ctx = NULL };
    CHECK(nic_uplink_push(&f, r0, BASE, 0) == 0);
    CHECK(nic_uplink_push(&f, r1, BASE, 0) == NIC_UPLINK_ESTORE);
    CHECK(f.count == 2);                 /* retained, not dropped */
    CHECK(g_send_calls == 0);            /* never stream what we could not archive */
    g_store_fail = 0;
    CHECK(nic_uplink_flush(&f) == (NIC_UPLINK_STORED | NIC_UPLINK_SENT));
    CHECK(f.count == 0);
    CHECK(batch_ok(g_store_buf, g_store_len, 9, 4, 2, BASE));

    /* T5 — reset drops the open batch. */
    sinks_reset();
    nic_uplink_t z = { .buf = g_buf, .buf_cap = sizeof g_buf, .source = 4,
                       .record_len = 4, .max_records = 255, .max_ms = 0,
                       .store = store_sink, .send = NULL, .ctx = NULL };
    CHECK(nic_uplink_push(&z, r0, BASE, 0) == 0);
    CHECK(nic_uplink_push(&z, r1, BASE, 0) == 0);
    nic_uplink_reset(&z);
    CHECK(z.count == 0);
    CHECK(nic_uplink_flush(&z) == NIC_UPLINK_EMPTY);
    CHECK(g_store_calls == 0);

    /* T6 — argument / capacity guards. */
    CHECK(nic_uplink_push(&z, NULL, BASE, 0) == NIC_UPLINK_EARG);
    nic_uplink_t bad = z; bad.record_len = 0;
    CHECK(nic_uplink_push(&bad, r0, BASE, 0) == NIC_UPLINK_EARG);
    /* buf too small to hold one record + CRC */
    uint8_t tiny[NIC_UPLINK_HEADER + 2];
    nic_uplink_t ov = { .buf = tiny, .buf_cap = sizeof tiny, .source = 5,
                        .record_len = 4, .max_records = 4, .max_ms = 0,
                        .store = store_sink, .send = NULL, .ctx = NULL };
    CHECK(nic_uplink_push(&ov, r0, BASE, 0) == NIC_UPLINK_EARG);

    if (g_failures == 0) {
        printf("OK: all checks passed\n");
        return 0;
    }
    printf("FAILED: %d check(s)\n", g_failures);
    return 1;
}
