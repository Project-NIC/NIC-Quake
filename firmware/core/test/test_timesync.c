/*
 * Host-side test for nq-timesync: PPS-anchored interpolation of a free-running
 * sample clock onto GPS-true absolute time, and the measured crystal drift (D27).
 */

#include "nic_timesync.h"

#include <stdio.h>

static int g_failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { printf("  FAIL: %s (line %d)\n", #cond, __LINE__); g_failures++; } \
} while (0)

/* 1 MHz nominal makes the ppm arithmetic exact and easy to read. */
#define HZ 1000000

static void test_no_anchor(void) {
    printf("test_no_anchor\n");
    nic_timesync_t t;
    nic_timesync_init(&t, HZ);
    CHECK(nic_timesync_resolve(&t, 123) == 0);     /* time unknown until anchored */
    CHECK(nic_timesync_drift_ppb(&t) == 0);
}

static void test_one_anchor_nominal(void) {
    printf("test_one_anchor_nominal\n");
    nic_timesync_t t;
    nic_timesync_init(&t, HZ);
    nic_timesync_anchor(&t, 0, 0);                 /* one anchor -> nominal-rate fallback */
    CHECK(nic_timesync_resolve(&t, 500000) == 500000000);   /* 0.5 s at 1 MHz */
    CHECK(nic_timesync_drift_ppb(&t) == 0);
}

static void test_two_anchors_exact(void) {
    printf("test_two_anchors_exact\n");
    nic_timesync_t t;
    nic_timesync_init(&t, HZ);
    nic_timesync_anchor(&t, 0, 0);
    nic_timesync_anchor(&t, 1000000, 1000000000);  /* exactly 1e6 ticks in 1 s */
    CHECK(nic_timesync_resolve(&t, 0)       == 0);
    CHECK(nic_timesync_resolve(&t, 500000)  == 500000000);   /* interpolate */
    CHECK(nic_timesync_resolve(&t, 2000000) == 2000000000);  /* extrapolate past the anchor */
    CHECK(nic_timesync_drift_ppb(&t) == 0);
}

static void test_drift_fast(void) {
    printf("test_drift_fast\n");
    nic_timesync_t t;
    nic_timesync_init(&t, HZ);
    nic_timesync_anchor(&t, 0, 0);
    nic_timesync_anchor(&t, 1000050, 1000000000); /* 1000050 ticks in 1 s -> +50 ppm */
    CHECK(nic_timesync_drift_ppb(&t) == 50000);   /* 50 ppm = 50000 ppb */
    /* 500025 / 1000050 = exactly 0.5 -> the slope is absorbed */
    CHECK(nic_timesync_resolve(&t, 500025) == 500000000);
    /* The "variable-length second": 1e6 ticks (a nominal second of counting) actually
     * spans < 1e9 ns because the clock ran fast — proof the ppm is measured, not assumed. */
    CHECK(nic_timesync_resolve(&t, 1000000) == 999950003);
    CHECK(nic_timesync_resolve(&t, 1000000) < 1000000000);
}

static void test_drift_slow(void) {
    printf("test_drift_slow\n");
    nic_timesync_t t;
    nic_timesync_init(&t, HZ);
    nic_timesync_anchor(&t, 0, 0);
    nic_timesync_anchor(&t, 999900, 1000000000);  /* 999900 ticks in 1 s -> -100 ppm */
    CHECK(nic_timesync_drift_ppb(&t) == -100000);
}

static void test_keeps_last_two(void) {
    printf("test_keeps_last_two\n");
    nic_timesync_t t;
    nic_timesync_init(&t, HZ);
    nic_timesync_anchor(&t, 0, 0);
    nic_timesync_anchor(&t, 1000000, 1000000000);
    nic_timesync_anchor(&t, 2000000, 2000000000);  /* third PPS — older pair dropped */
    CHECK(t.anchors == 2);
    CHECK(nic_timesync_resolve(&t, 1500000) == 1500000000);
    CHECK(nic_timesync_drift_ppb(&t) == 0);
}

int main(void) {
    test_no_anchor();
    test_one_anchor_nominal();
    test_two_anchors_exact();
    test_drift_fast();
    test_drift_slow();
    test_keeps_last_two();

    if (g_failures == 0) {
        printf("OK: all checks passed\n");
        return 0;
    }
    printf("FAILED: %d check(s)\n", g_failures);
    return 1;
}
