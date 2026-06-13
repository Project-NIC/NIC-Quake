#ifndef NIC_MASTER_H
#define NIC_MASTER_H

/*
 * nq-master — the master-side orchestration core, the symmetric counterpart to
 * nq-node. Portable C, host-testable, driving the SAME nq-link transport (the
 * master just uses the opposite directions: it sends CONTROL frames and decodes
 * DATA frames).
 *
 * It owns the topology table (who is present and each node's DATA payload
 * length, learned at discovery), the slot-order scheduler, and DATA-frame
 * ingest. The I/O (RS-485 send/recv, RTC, storage) is the master board glue —
 * on the reference design an ESP32 (UART + WiFi/BT, external 8.192 MHz clock).
 */

#include "nic_link.h"
#include "nic_proto.h"
#include "nic_types.h"
#include <stdint.h>
#include <stddef.h>

#define NIC_MASTER_MAX_NODES  8u   /* up to 8 nodes per segment (README) */

typedef struct {
    uint8_t  addr;          /* assigned address                                 */
    uint8_t  payload_len;   /* its DATA payload length (from STATUS)            */
    uint8_t  type;          /* WHAT it is (NIC_NODE_TYPE_*, from NODE_TYPE; D26) */
    int      present;
    int      synced;        /* 1 = phase-locked & answering; 0 = desynced/lost  */
    uint16_t exp_index;     /* next expected sample index (gap detection)        */
    uint8_t  pending_op;    /* a critical command awaiting its ACK (0 = none)    */
    uint16_t link_errors;   /* last reported rolling echo/CRC error count        */
    uint8_t  version;       /* firmware/protocol version (0 = unknown; queried)  */
    uint8_t  volt_dv;       /* last GET_VOLTAGE reply (0.1 V/LSB)                */
    int8_t   cpu_temp_c;    /* last GET_CPUTEMP reply (deg C)                    */
    uint8_t  sensor_mask;   /* last SENSOR_TEST reply (which sensors respond)    */
} nic_master_node_t;

typedef struct {
    nic_master_node_t nodes[NIC_MASTER_MAX_NODES];
    int count;
    int token_idx;         /* round-robin cursor */
} nic_master_t;

/* A decoded data record, ready to map onto NIC-MLA (timestamp = RTC second,
 * subsec = index) and store. `payload` aliases the source buffer. */
typedef struct {
    uint8_t        station;
    uint16_t       index;
    const uint8_t *payload;
    uint8_t        len;
    uint16_t       gap;     /* samples missed since this node's previous frame */
} nic_master_record_t;

void nic_master_init(nic_master_t *m);

/*
 * Register or refresh a node from its STATUS reply. Returns the slot index, or
 * NIC_ERR if the table is full.
 */
int nic_master_add_node(nic_master_t *m, uint8_t addr, uint8_t payload_len);

/*
 * Next node to grant the token (round-robin over present nodes); advances the
 * cursor. Returns the node address, or -1 when there are no nodes.
 */
int nic_master_next_token(nic_master_t *m);

/*
 * Ingest a received DATA frame: read the sender from the frame, look up its
 * payload length in the topology, decode, and fill *rec. A good frame also marks
 * that node `synced` (it answered, it is alive and locked). Returns NIC_OK,
 * NIC_ERR_FORMAT / NIC_ERR_CRC, or NIC_ERR for an unknown station / bad args.
 */
int nic_master_ingest(nic_master_t *m, const uint8_t *buf, size_t len,
                     nic_master_record_t *rec);

/*
 * Mark a node as lost / desynced — call when it reports NIC_ERRC_CLOCK_LOST or
 * when its token reply times out. Returns the node index, or NIC_ERR if unknown.
 */
int nic_master_node_lost(nic_master_t *m, uint8_t addr);

/*
 * 1 if every present node is desynced at once — the strong signal that the
 * master's OWN clock source died (a single lost node is just a cable fault).
 * Meaningful during the sampling phase (nodes start unsynced).
 */
int nic_master_all_lost(const nic_master_t *m);

/*
 * Call when the master sends the per-second TICK: reset every node's expected
 * sample index to 0, so gap detection re-bases at the second boundary.
 */
void nic_master_on_tick(nic_master_t *m);

/*
 * Handle a CONTROL frame received FROM a node and update the topology:
 *   STATUS  -> register/refresh the node (value = its DATA payload length)
 *   NODE_TYPE -> store WHAT kind of node it is (value = NIC_NODE_TYPE_*; D26)
 *   ACK     -> clear the pending command if value matches
 *   ERROR   -> mark the node desynced/lost (value = NIC_ERRC_*)
 *   HEALTH  -> store the node's reported link-error count
 *   VERSION -> store the node's firmware/protocol version
 *   GET_VOLTAGE / GET_CPUTEMP / SENSOR_TEST -> store the queried value
 * Returns NIC_OK, or NIC_ERR for an unknown station (except STATUS, which adds it).
 */
int nic_master_on_ctrl(nic_master_t *m, const nic_link_ctrl_t *c);

/*
 * Record that a critical command `op` was sent to `addr` and now awaits its ACK.
 * The runtime re-sends if no ACK arrives before its timeout. Returns the node
 * index or NIC_ERR.
 */
int nic_master_expect_ack(nic_master_t *m, uint8_t addr, uint8_t op);

/* The op a node still owes an ACK for (0 = nothing pending), or 0 if unknown. */
int nic_master_pending(const nic_master_t *m, uint8_t addr);

/*
 * Build a CONTROL frame (op + value addressed to `station`, 0xFF = broadcast).
 * Thin wrapper over nic_link_ctrl_encode; returns the frame length or NIC_ERR.
 */
int nic_master_cmd(uint8_t station, uint8_t op, uint8_t value,
                  uint8_t *out, size_t cap);

/*
 * Build the once-per-second broadcast TICK that resets every node's sub-second
 * sample index to 0 (anchored to the RTC second). It keeps the index meaning
 * "sub-second sample number" and lets a re-synced node fall back into lock-step
 * on the next tick. Send it each RTC second. Returns the frame length or NIC_ERR.
 */
int nic_master_tick(uint8_t *out, size_t cap);

#endif /* NIC_MASTER_H */
