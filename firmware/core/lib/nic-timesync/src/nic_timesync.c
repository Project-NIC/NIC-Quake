#include "nic_timesync.h"

void nic_timesync_init(nic_timesync_t *t, int64_t nominal_hz) {
    t->a_ticks = t->a_ns = 0;
    t->b_ticks = t->b_ns = 0;
    t->nominal_hz = (nominal_hz > 0) ? nominal_hz : 1;
    t->anchors = 0;
}

void nic_timesync_anchor(nic_timesync_t *t, int64_t ticks, int64_t gps_ns) {
    /* Shift the newer anchor into the older slot; keep only the last two. */
    t->a_ticks = t->b_ticks;
    t->a_ns    = t->b_ns;
    t->b_ticks = ticks;
    t->b_ns    = gps_ns;
    if (t->anchors < 2) t->anchors++;
}

int64_t nic_timesync_resolve(const nic_timesync_t *t, int64_t ticks) {
    if (t->anchors >= 2) {
        int64_t dticks = t->b_ticks - t->a_ticks;
        if (dticks == 0) return t->b_ns;               /* degenerate: two anchors, same tick */
        /* Interpolate/extrapolate from the newer anchor along the measured slope.
         * (ticks - b_ticks) is bounded to ~1 s of fast-clock counts on the write
         * path, so the int64 product stays well clear of overflow. */
        return t->b_ns + (ticks - t->b_ticks) * (t->b_ns - t->a_ns) / dticks;
    }
    if (t->anchors == 1) {                              /* one anchor: assume nominal rate */
        return t->b_ns + (ticks - t->b_ticks) * 1000000000 / t->nominal_hz;
    }
    return 0;                                           /* no anchor yet — time unknown */
}

int64_t nic_timesync_drift_ppb(const nic_timesync_t *t) {
    if (t->anchors < 2) return 0;
    int64_t dns = t->b_ns - t->a_ns;
    if (dns <= 0) return 0;
    int64_t measured_hz = (t->b_ticks - t->a_ticks) * 1000000000 / dns;
    return (measured_hz - t->nominal_hz) * 1000000000 / t->nominal_hz;
}
