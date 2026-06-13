/* Host-side test for nq-detect: a quiet baseline produces no trigger; a strong
 * burst yields one strong event; a small bump yields one weak event. */

#include "nq_detect.h"
#include <stdio.h>

static int g_failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { printf("  FAIL: %s (line %d)\n", #cond, __LINE__); g_failures++; } \
} while (0)

/* feed `n` samples of +-amp; return the last event (occurred/peak/strong). */
static nq_detect_result_t burst(nq_detect_t *d, int n, int32_t amp) {
    nq_detect_result_t last = { 0, 0, 0 };
    for (int i = 0; i < n; i++) {
        nq_detect_result_t r = nq_detect_push(d, (i & 1) ? amp : -amp);
        if (r.occurred) last = r;
    }
    return last;
}

int main(void) {
    printf("test_detect\n");

    /* quiet baseline -> never triggers */
    nq_detect_t d; nq_detect_init(&d);
    nq_detect_result_t q = burst(&d, 4000, 50);
    CHECK(!q.occurred);

    /* strong burst, then quiet -> exactly one strong event with peak ~20000 */
    (void)burst(&d, 200, 20000);
    nq_detect_result_t e = burst(&d, 4000, 50);
    CHECK(e.occurred == 1);
    CHECK(e.strong == 1);
    CHECK(e.peak >= 20000);

    /* a weak bump -> an event, but not strong */
    nq_detect_t d2; nq_detect_init(&d2);
    (void)burst(&d2, 4000, 50);
    (void)burst(&d2, 200, 1000);
    nq_detect_result_t w = burst(&d2, 4000, 50);
    CHECK(w.occurred == 1);
    CHECK(w.strong == 0);

    if (g_failures == 0) {
        printf("OK: all checks passed\n");
        return 0;
    }
    printf("FAILED: %d check(s)\n", g_failures);
    return 1;
}
