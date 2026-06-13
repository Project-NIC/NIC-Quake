#include "nic_clocksync.h"

int nic_clocksync_to_hse(const nic_clocksync_ops_t *ops) {
    ops->uart2_disable_irq(ops->ctx);   /* 1 */
    ops->uart2_deinit(ops->ctx);        /* 2 */
    ops->remap_rx_to_hse(ops->ctx);     /* 3 */
    ops->select_hse(ops->ctx);          /* 4 */
    return ops->pll_wait_lock(ops->ctx); /* 5 */
}

void nic_clocksync_recover(const nic_clocksync_ops_t *ops) {
    ops->remap_hse_to_rx(ops->ctx);     /* 1 */
    ops->uart2_reinit(ops->ctx);        /* 2 */
    ops->select_rc(ops->ctx);           /* 3 */
}
