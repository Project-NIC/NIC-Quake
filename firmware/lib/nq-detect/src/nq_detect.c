#include "nq_detect.h"

void nq_detect_init(nq_detect_t *d) {
    d->sta = 0;
    d->lta = 256;          /* scaled (<<8); start non-zero to avoid /0          */
    d->peak = 0;
    d->in_event = 0;
    d->primed = 0;
    d->warmup = (uint16_t)(1u << NQ_DET_LTA_SHIFT);  /* one LTA window before trusting the ratio */
}

nq_detect_result_t nq_detect_push(nq_detect_t *d, int32_t x) {
    nq_detect_result_t r = { 0, 0, 0 };
    int32_t a  = (x < 0) ? -x : x;     /* rectify          */
    int32_t as = a << 8;               /* fixed point (<<8) */

    if (!d->primed) {                  /* seed STA and LTA equal -> ratio starts at 1 */
        d->sta = as;
        d->lta = (as < 256) ? 256 : as;
        d->primed = 1;
    }
    d->sta += (as - d->sta) >> NQ_DET_STA_SHIFT;
    if (!d->in_event) {                /* classic STA/LTA freezes LTA during an event */
        d->lta += (as - d->lta) >> NQ_DET_LTA_SHIFT;
        if (d->lta < 256) d->lta = 256;
    }
    if (d->warmup) { d->warmup--; return r; }   /* hold off until the LTA has settled */

    if (!d->in_event) {
        if (d->sta > d->lta * NQ_DET_ON_RATIO) {
            d->in_event = 1;
            d->peak = a;
        }
    } else {
        if (a > d->peak) d->peak = a;
        if (d->sta < d->lta * NQ_DET_OFF_RATIO) {
            d->in_event = 0;
            r.occurred = 1;
            r.peak     = d->peak;
            r.strong   = (d->peak >= NQ_DET_STRONG_PEAK);
        }
    }
    return r;
}
