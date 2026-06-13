#ifndef NIC_CLOCKSYNC_H
#define NIC_CLOCKSYNC_H

/*
 * nq-clocksync — switch the node between listening on UART2 and running on the
 * hardware HSE clock distributed by the master, and recover when that clock
 * disappears.
 *
 * The low-level register pokes are STM32-specific and live in the glue; this
 * module owns only the *order* in which they run. SOFTWARE.md marks that order
 * mandatory: remapping the pin before de-initialising UART2 would make UART2
 * interpret the incoming clock as data and fire interrupts. Keeping the
 * sequence here — portable and host-testable — is the whole point.
 */

#include "nic_types.h"

typedef struct {
    void *ctx;

    /* Forward switch: listening -> HSE. */
    void (*uart2_disable_irq)(void *ctx);
    void (*uart2_deinit)(void *ctx);
    void (*remap_rx_to_hse)(void *ctx);
    void (*select_hse)(void *ctx);
    int  (*pll_wait_lock)(void *ctx);   /* NIC_OK on lock, NIC_ERR on timeout */

    /* Recovery: HSE lost -> back to internal RC and listening. */
    void (*remap_hse_to_rx)(void *ctx);
    void (*uart2_reinit)(void *ctx);
    void (*select_rc)(void *ctx);
} nic_clocksync_ops_t;

/*
 * Switch to the master's HSE clock. Mandatory order (SOFTWARE.md):
 *   1. disable UART2 interrupts
 *   2. de-initialise UART2
 *   3. remap the RX pin to HSE_IN
 *   4. select HSE as the clock source
 *   5. wait for the PLL to lock
 * Returns NIC_OK on lock, or the error from pll_wait_lock (caller should recover).
 */
int nic_clocksync_to_hse(const nic_clocksync_ops_t *ops);

/*
 * Watchdog recovery after the master clock vanishes. Order (SOFTWARE.md):
 *   1. remap HSE_IN back to the UART2 RX pin
 *   2. re-initialise UART2
 *   3. select the internal RC oscillator
 * (The STM32 clock-security system has usually already kicked to HSI by the
 * time this runs; this makes the fallback deliberate and resumes listening.)
 */
void nic_clocksync_recover(const nic_clocksync_ops_t *ops);

#endif /* NIC_CLOCKSYNC_H */
