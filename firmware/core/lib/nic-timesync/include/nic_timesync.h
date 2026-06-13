#ifndef NIC_TIMESYNC_H
#define NIC_TIMESYNC_H

/*
 * nq-timesync — turn a free-running sample clock into GPS-true absolute time at
 * write time (DESIGN D27). The sampling oscillator drifts a few ppm, so the number
 * of ticks (hence samples) in a real second wobbles — "the second is a different
 * length over time". Instead of fighting that, the master latches the **fast clock
 * counter** at each GPS **PPS** edge and pairs it with the PPS's absolute time; the
 * absolute time of any sample is then a linear interpolation between two PPS anchors.
 *
 * The slope of that interpolation IS the measured true rate, so the ppm drift is
 * absorbed, not approximated — and the accuracy is set by the fast-clock capture
 * (~one tick), NOT by the sample rate. A mediocre but *measured* crystal beats a
 * precise but unmeasured one (see nic_timesync_drift_ppb).
 *
 * Portable + host-tested. The hardware half — timer input-capture of PPS against the
 * 8.192 MHz network clock, and reading GPS absolute time over UART — is master glue.
 *
 * Units: `ticks` is a monotonic fast-clock count (e.g. the 8.192 MHz network clock);
 * `ns` is absolute time in nanoseconds (GPS/Unix epoch, fits int64 until year 2262).
 */

#include <stdint.h>

typedef struct {
    int64_t a_ticks, a_ns;   /* older anchor: fast-clock count <-> absolute ns */
    int64_t b_ticks, b_ns;   /* newer anchor                                   */
    int64_t nominal_hz;      /* fast-clock nominal rate — fallback before 2 anchors exist */
    int     anchors;         /* 0, 1, or 2 (saturates at 2; only the last two are kept)   */
} nic_timesync_t;

/* Start empty; `nominal_hz` is the oscillator's nominal tick rate (e.g. 8192000). */
void nic_timesync_init(nic_timesync_t *t, int64_t nominal_hz);

/* Record a PPS edge: the fast-clock counter read `ticks` at absolute time `gps_ns`. */
void nic_timesync_anchor(nic_timesync_t *t, int64_t ticks, int64_t gps_ns);

/*
 * Absolute time (ns) of an arbitrary fast-clock count, interpolated/extrapolated
 * along the latest measured slope (the true, drift-corrected rate). Before two
 * anchors exist it falls back to `nominal_hz`; with none it returns 0. Intended for
 * a tick within ~1 s of the latest anchor (the real-time write path).
 */
int64_t nic_timesync_resolve(const nic_timesync_t *t, int64_t ticks);

/*
 * The crystal's measured error vs nominal, in parts-per-billion (this is exactly the
 * "oscillator wire into the MCU measures the crystal against GPS" reading). Positive
 * = running fast. 0 until two anchors exist.
 */
int64_t nic_timesync_drift_ppb(const nic_timesync_t *t);

#endif /* NIC_TIMESYNC_H */
