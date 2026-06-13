#ifndef NQ_DETECT_H
#define NQ_DETECT_H

/*
 * nq-detect — a compact STA/LTA seismic trigger for the master. It runs on the
 * ingested seismic magnitude and reports an event (with its peak amplitude and a
 * strong/weak class) when the short-term / long-term energy ratio crosses back
 * down. The master sends a strong event over LoRa immediately and folds weak ones
 * into the periodic telemetry count. Pure scaled-integer math — host-testable.
 *
 * Tuning (counts / 125 Hz): STA ~0.5 s, LTA ~16 s, trigger at 4x, release at 2x.
 * These are deployment knobs — VALIDATE against field data.
 */

#include <stdint.h>

#define NQ_DET_STA_SHIFT    6     /* STA EMA: ~2^6 = 64 samples (~0.5 s @125 Hz) */
#define NQ_DET_LTA_SHIFT   11     /* LTA EMA: ~2^11 = 2048 samples (~16 s)        */
#define NQ_DET_ON_RATIO     4     /* trigger when STA > ON  * LTA                 */
#define NQ_DET_OFF_RATIO    2     /* release when STA < OFF * LTA                 */
#define NQ_DET_STRONG_PEAK  8000  /* peak >= this (counts) => strong event        */

typedef struct {
    int32_t  sta, lta;    /* running averages of |sample|, fixed point (<<8) */
    int32_t  peak;        /* peak |sample| during the current event           */
    uint8_t  in_event;
    uint8_t  primed;      /* seeded from the first sample?                    */
    uint16_t warmup;      /* samples left before the ratio is trusted (LTA settling) */
} nq_detect_t;

typedef struct {
    int     occurred;     /* 1 if an event just ended on this sample */
    int32_t peak;         /* its peak amplitude                      */
    int     strong;       /* 1 if peak >= NQ_DET_STRONG_PEAK         */
} nq_detect_result_t;

void nq_detect_init(nq_detect_t *d);

/* Push one seismic magnitude sample; returns an event when one just ended. */
nq_detect_result_t nq_detect_push(nq_detect_t *d, int32_t sample);

#endif /* NQ_DETECT_H */
