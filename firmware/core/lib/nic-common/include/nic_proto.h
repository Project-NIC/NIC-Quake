#ifndef NIC_PROTO_H
#define NIC_PROTO_H

/*
 * Shared control-plane opcodes — the `attribute` byte of an nq-link CONTROL
 * frame. Both the node and the master include this so they agree on meaning;
 * nq-link itself stays a dumb transport (D13/D19) and gives these no semantics.
 *
 * Directions:
 *   master -> node : DISCOVER, NODE_TYPE, ASSIGN_ADDR, SYNC, CALIBRATE, TOKEN,
 *                    HARD_RESET, and the generic CFG_* config slots (the only
 *                    runtime-tunable settings — sensor sensitivity; D21/D28)
 *   node  -> master: STATUS (reply to DISCOVER; value = its DATA payload length,
 *                    so the master can decode that node's data frames),
 *                    NODE_TYPE (reply; value = what KIND of node it is, D26), ERROR
 */

/*
 * Addressing: a node identifies by its permanent HARDWARE NUMBER — a small
 * human-set number on the box (a per-box build constant; NOT the MCU's 96-bit UID; DESIGN
 * D23). It reports that number at boot and after a hard reset. ASSIGN_ADDR then
 * gives it a SOFTWARE address = its TDMA slot for this deployment. Identity
 * (hardware) and role (software) stay separate, so a box is portable.
 */

enum {
    NIC_OP_DISCOVER    = 1,   /* master->node: present? -> reply STATUS            */
    NIC_OP_ASSIGN_ADDR = 2,   /* master->node: adopt address `value`              */
    NIC_OP_SYNC        = 3,   /* broadcast: switch to HSE clock / start sampling  */
    NIC_OP_CALIBRATE   = 4,   /* record reference zero                            */
    NIC_OP_TOKEN       = 5,   /* master->node: explicit slot grant (repair/out-of-band);
                             * normal data is self-timed in the node's TDMA slot (D24) */
    /* 6 freed — sensor range setting is now the generic CFG block below (D28) */
    NIC_OP_HARD_RESET  = 7,   /* broadcast: reset to hardware base address        */
    NIC_OP_STATUS      = 8,   /* node->master: value = DATA payload length        */
    NIC_OP_ERROR       = 9,   /* node->master: value = error code (NIC_ERRC_*)     */
    NIC_OP_TICK        = 10,  /* broadcast, once/sec: reset sub-second index -> 0 */
    NIC_OP_ACK         = 11,  /* node->master: value = the op being acknowledged  */
    NIC_OP_VERSION     = 12,  /* master->node query; node reply value = version   */
    NIC_OP_HEALTH      = 13,  /* node->master: value = rolling link-error count    */
    /* 14-16 freed — per-sensor range setting is now the generic CFG block below (D28) */
    NIC_OP_RESEND_DATA   = 17, /* master->node: CRC miss on your last DATA frame — resend
                              * the buffered frame, in the node's slot                   */
    /* On-demand diagnostics (D24) — the master "asks" only when it wants detail, so
     * the per-sample DATA status byte stays lean. Node replies value in its slot. */
    NIC_OP_GET_VOLTAGE   = 18, /* query; reply value = supply voltage (0.1 V/LSB)          */
    NIC_OP_SENSOR_TEST   = 19, /* query; reply value = bitmask of sensors that respond     */
    NIC_OP_GET_CPUTEMP   = 20, /* query; reply value = MCU temperature (deg C)             */
    NIC_OP_NODE_TYPE     = 21  /* query; reply value = node TYPE (NIC_NODE_TYPE_*; D26)     */
};

/*
 * Generic per-front config slots (D28). The protocol reserves a block of opcode values as
 * numbered "set config slot N" commands; the frame's VALUE byte carries the setting. What
 * each slot MEANS is defined by the node's FRONT glue (e.g. seismo: slot 0 = ADXL range),
 * NOT here — so the shared protocol stays front-agnostic and does not grow a fresh opcode
 * per sensor as fronts (iono, starDust, …) are added. The slot rides in the ATTRIBUTE byte
 * because the lean frame's single value byte can't carry both slot and value. Restricted to
 * sensor sensitivity/range (D21) — not a backdoor to making the node smart.
 */
#define NIC_OP_CFG_BASE     32u                  /* opcodes 32..39 = config slots 0..7     */
#define NIC_OP_CFG_SLOTS     8u
#define NIC_OP_CFG(slot)    ((uint8_t)(NIC_OP_CFG_BASE + (slot)))
#define NIC_OP_CFG_IS(op)   ((op) >= NIC_OP_CFG_BASE && (op) < NIC_OP_CFG_BASE + NIC_OP_CFG_SLOTS)
#define NIC_OP_CFG_SLOT(op) ((uint8_t)((op) - NIC_OP_CFG_BASE))

/*
 * Node TYPE — WHAT a node is (its product line / sensor front), as distinct from its
 * NUMBER (WHICH box; D23). It is a per-PRODUCT build constant the node reports at
 * discovery in an NIC_OP_NODE_TYPE reply; the master stores it next to the box number.
 * Kept OFF the high-rate DATA frame (D26): the station number in each frame already
 * keys back to the type via the master's table, so a per-sample type byte would just
 * be 125x/s of a constant.
 *
 * Payload length is NOT a reliable type tag — a full seismo node and a Basic weather
 * node are both 28 B — which is exactly why type gets its own field.
 */
enum {
    NIC_NODE_TYPE_UNKNOWN  = 0,  /* not reported yet                                       */
    NIC_NODE_TYPE_SEISMO   = 1,  /* NIC-Quake seismograph node                             */
    NIC_NODE_TYPE_BASIC    = 2,  /* NIC-Weather "Basic station" — the meteo base node      */
    NIC_NODE_TYPE_IONO     = 3,  /* NIC-Iono GNSS / ionospheric node                       */
    NIC_NODE_TYPE_STARDUST = 4   /* starDust air-quality node (parked extension)           */
};

/* Human-readable node type — for the master's discovery log (header-only, no stdio). */
static inline const char *nic_node_type_name(uint8_t t) {
    switch (t) {
        case NIC_NODE_TYPE_SEISMO:   return "seismo";
        case NIC_NODE_TYPE_BASIC:    return "basic";
        case NIC_NODE_TYPE_IONO:     return "iono";
        case NIC_NODE_TYPE_STARDUST: return "stardust";
        default:                    return "unknown";
    }
}

/* Error codes carried in an NIC_OP_ERROR control frame's `value`. */
enum {
    NIC_ERRC_CLOCK_LOST = 1,  /* node fell back to RC — master clock gone         */
    NIC_ERRC_BUS        = 2,  /* RS-485 echo mismatch / bus error                 */
    NIC_ERRC_SENSOR     = 3   /* a sensor stopped responding                      */
};

#endif /* NIC_PROTO_H */
