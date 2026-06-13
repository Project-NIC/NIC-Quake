#include "nic_node.h"

static nic_node_step_t step(nic_node_state_t s, nic_node_action_t a) {
    nic_node_step_t r = { s, a };
    return r;
}

nic_node_step_t nic_node_step(nic_node_state_t state, nic_node_event_t event) {
    /* A hard reset from the master returns to BOOT from anywhere. */
    if (event == NIC_EV_RESET_CMD) {
        return step(NIC_NODE_BOOT, NIC_ACT_NONE);
    }
    /* Losing the master clock drops to FAULT and recovers from anywhere on HSE. */
    if (event == NIC_EV_CLOCK_LOST) {
        return step(NIC_NODE_FAULT, NIC_ACT_RECOVER);
    }

    switch (state) {
        case NIC_NODE_BOOT:
            if (event == NIC_EV_BOOT_DONE)     return step(NIC_NODE_DETECT, NIC_ACT_DETECT);
            break;

        case NIC_NODE_DETECT:
            if (event == NIC_EV_DETECTED)      return step(NIC_NODE_INIT_SENSORS, NIC_ACT_INIT_SENSORS);
            break;

        case NIC_NODE_INIT_SENSORS:
            if (event == NIC_EV_SENSORS_READY) return step(NIC_NODE_LISTEN, NIC_ACT_LISTEN);
            break;

        case NIC_NODE_LISTEN:
            /* Master broadcasts the sync command -> run the clock switch. */
            if (event == NIC_EV_SYNC_CMD)      return step(NIC_NODE_LISTEN, NIC_ACT_SWITCH_HSE);
            /* PLL locked -> sensors share the master clock, start sampling. */
            if (event == NIC_EV_HSE_OK)        return step(NIC_NODE_SYNCED, NIC_ACT_START_SAMPLING);
            break;

        case NIC_NODE_SYNCED:
            if (event == NIC_EV_CALIBRATE_CMD) return step(NIC_NODE_SYNCED, NIC_ACT_CALIBRATE);
            break;

        case NIC_NODE_FAULT:
            /* After recovery the node is back on RC, listening again. */
            if (event == NIC_EV_BOOT_DONE)     return step(NIC_NODE_LISTEN, NIC_ACT_LISTEN);
            break;
    }

    /* Unhandled: stay put, do nothing. */
    return step(state, NIC_ACT_NONE);
}

int nic_node_pack(const nic_node_field_t *fields, int n_fields,
                 uint8_t *out, size_t cap) {
    if (out == NULL || (n_fields > 0 && fields == NULL) || n_fields < 0) {
        return NIC_ERR;
    }

    size_t need = 0u;
    for (int i = 0; i < n_fields; i++) {
        uint8_t w = fields[i].bytes_per_axis;
        if (w < 1u || w > 4u || fields[i].drop_bits > 31u) {
            return NIC_ERR;
        }
        need += (size_t)w * 3u;
    }
    if (cap < need) {
        return NIC_ERR;
    }

    size_t p = 0;
    for (int i = 0; i < n_fields; i++) {
        uint8_t  w     = fields[i].bytes_per_axis;
        uint8_t  drop  = fields[i].drop_bits;
        int64_t  vmax  = ((int64_t)1 << (8 * w - 1)) - 1;
        int64_t  vmin  = -((int64_t)1 << (8 * w - 1));
        for (int ax = 0; ax < 3; ax++) {
            /* Discard sub-ENOB low bits (toward zero), then saturate to width. */
            int64_t v = (int64_t)fields[i].sample.axis[ax] / ((int64_t)1 << drop);
            if (v > vmax) v = vmax;
            else if (v < vmin) v = vmin;
            uint64_t u = (uint64_t)v;
            for (int b = 0; b < w; b++) {
                out[p++] = (uint8_t)(u >> (8 * (w - 1 - b)));    /* big-endian */
            }
        }
    }
    return (int)p;
}
