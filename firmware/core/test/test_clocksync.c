/*
 * Host-side test for nq-clocksync: the value here is the *order* of operations,
 * which SOFTWARE.md marks mandatory. A mock ops set logs the call sequence.
 */

#include "nic_clocksync.h"

#include <stdio.h>

enum { DIS_IRQ = 1, DEINIT, REMAP_HSE, SEL_HSE, WAIT_LOCK,
       REMAP_RX, REINIT, SEL_RC };

typedef struct { int seq[16]; int n; int lock_result; } cslog_t;

static void log_push(cslog_t *l, int id) { if (l->n < 16) l->seq[l->n++] = id; }

static void op_dis_irq(void *c)   { log_push(c, DIS_IRQ); }
static void op_deinit(void *c)    { log_push(c, DEINIT); }
static void op_remap_hse(void *c) { log_push(c, REMAP_HSE); }
static void op_sel_hse(void *c)   { log_push(c, SEL_HSE); }
static int  op_wait_lock(void *c) { cslog_t *l = c; log_push(l, WAIT_LOCK); return l->lock_result; }
static void op_remap_rx(void *c)  { log_push(c, REMAP_RX); }
static void op_reinit(void *c)    { log_push(c, REINIT); }
static void op_sel_rc(void *c)    { log_push(c, SEL_RC); }

static int g_failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { printf("  FAIL: %s (line %d)\n", #cond, __LINE__); g_failures++; } \
} while (0)

static nic_clocksync_ops_t make_ops(cslog_t *l) {
    nic_clocksync_ops_t ops = {
        .ctx = l,
        .uart2_disable_irq = op_dis_irq,
        .uart2_deinit      = op_deinit,
        .remap_rx_to_hse   = op_remap_hse,
        .select_hse        = op_sel_hse,
        .pll_wait_lock     = op_wait_lock,
        .remap_hse_to_rx   = op_remap_rx,
        .uart2_reinit      = op_reinit,
        .select_rc         = op_sel_rc,
    };
    return ops;
}

static void test_switch_order(void) {
    printf("test_switch_order\n");
    cslog_t l = { .lock_result = NIC_OK };
    nic_clocksync_ops_t ops = make_ops(&l);

    CHECK(nic_clocksync_to_hse(&ops) == NIC_OK);
    CHECK(l.n == 5);
    CHECK(l.seq[0] == DIS_IRQ);
    CHECK(l.seq[1] == DEINIT);
    CHECK(l.seq[2] == REMAP_HSE);   /* remap only AFTER UART2 is down */
    CHECK(l.seq[3] == SEL_HSE);
    CHECK(l.seq[4] == WAIT_LOCK);
}

static void test_switch_lock_timeout(void) {
    printf("test_switch_lock_timeout\n");
    cslog_t l = { .lock_result = NIC_ERR };
    nic_clocksync_ops_t ops = make_ops(&l);
    CHECK(nic_clocksync_to_hse(&ops) == NIC_ERR);   /* propagate the lock failure */
}

static void test_recover_order(void) {
    printf("test_recover_order\n");
    cslog_t l = {0};
    nic_clocksync_ops_t ops = make_ops(&l);

    nic_clocksync_recover(&ops);
    CHECK(l.n == 3);
    CHECK(l.seq[0] == REMAP_RX);
    CHECK(l.seq[1] == REINIT);
    CHECK(l.seq[2] == SEL_RC);
}

int main(void) {
    test_switch_order();
    test_switch_lock_timeout();
    test_recover_order();

    if (g_failures == 0) {
        printf("OK: all checks passed\n");
        return 0;
    }
    printf("FAILED: %d check(s)\n", g_failures);
    return 1;
}
