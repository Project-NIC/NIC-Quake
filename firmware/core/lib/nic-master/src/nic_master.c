#include "nic_master.h"

void nic_master_init(nic_master_t *m) {
    m->count = 0;
    m->token_idx = 0;
    for (int i = 0; i < (int)NIC_MASTER_MAX_NODES; i++) {
        m->nodes[i].present = 0;
    }
}

static int find_node(const nic_master_t *m, uint8_t addr) {
    for (int i = 0; i < m->count; i++) {
        if (m->nodes[i].present && m->nodes[i].addr == addr) return i;
    }
    return -1;
}

int nic_master_add_node(nic_master_t *m, uint8_t addr, uint8_t payload_len) {
    int i = find_node(m, addr);
    if (i >= 0) {                                  /* refresh existing */
        m->nodes[i].payload_len = payload_len;
        return i;
    }
    if (m->count >= (int)NIC_MASTER_MAX_NODES) {
        return NIC_ERR;
    }
    i = m->count++;
    m->nodes[i].addr        = addr;
    m->nodes[i].payload_len = payload_len;
    m->nodes[i].type        = NIC_NODE_TYPE_UNKNOWN;   /* learned via NODE_TYPE (D26) */
    m->nodes[i].present     = 1;
    m->nodes[i].synced      = 0;   /* not locked until it first answers */
    m->nodes[i].exp_index   = 0;
    m->nodes[i].pending_op  = 0;
    m->nodes[i].link_errors = 0;
    m->nodes[i].version     = 0;
    m->nodes[i].volt_dv     = 0;
    m->nodes[i].cpu_temp_c  = 0;
    m->nodes[i].sensor_mask = 0;
    return i;
}

/* The ring/slot order: yields node addresses in turn. In the self-running TDMA
 * model (D24) this is the SLOT ORDER the master assigns at init (= address order)
 * and uses for discovery / repair — NOT a per-sample token grant; nodes transmit
 * on their own clock-derived slots. */
int nic_master_next_token(nic_master_t *m) {
    if (m->count <= 0) {
        return -1;
    }
    if (m->token_idx >= m->count) {
        m->token_idx = 0;
    }
    int addr = m->nodes[m->token_idx].addr;
    m->token_idx = (m->token_idx + 1) % m->count;
    return addr;
}

int nic_master_ingest(nic_master_t *m, const uint8_t *buf, size_t len,
                     nic_master_record_t *rec) {
    if (m == NULL || buf == NULL || rec == NULL) {
        return NIC_ERR;
    }
    /* Peek the sender (frame: [MAGIC_D][station]...) to find its payload length. */
    if (len < NIC_LINK_DATA_OVERHEAD || buf[0] != NIC_LINK_MAGIC_DATA) {
        return NIC_ERR_FORMAT;
    }
    int i = find_node(m, buf[1]);
    if (i < 0) {
        return NIC_ERR;                              /* unknown station */
    }

    nic_link_data_t d;
    int r = nic_link_data_decode(buf, len, m->nodes[i].payload_len, &d);
    if (r != NIC_OK) {
        return r;                                   /* FORMAT or CRC */
    }
    m->nodes[i].synced = 1;                         /* it answered -> alive & locked */

    /* Gap detection: how many samples were missed since the last frame. The
     * expected index re-bases to 0 at each per-second tick (nic_master_on_tick). */
    uint16_t gap = 0u;
    if (d.index >= m->nodes[i].exp_index) {
        gap = (uint16_t)(d.index - m->nodes[i].exp_index);
        m->nodes[i].exp_index = (uint16_t)(d.index + 1u);     /* only ever advance forward */
    }   /* an older / duplicate index (e.g. a RESEND) reports gap 0 and leaves exp_index put */

    rec->station = d.station;
    rec->index   = d.index;
    rec->payload = d.payload;
    rec->len     = d.len;
    rec->gap     = gap;
    return NIC_OK;
}

void nic_master_on_tick(nic_master_t *m) {
    for (int i = 0; i < m->count; i++) {
        m->nodes[i].exp_index = 0;
    }
}

int nic_master_on_ctrl(nic_master_t *m, const nic_link_ctrl_t *c) {
    if (m == NULL || c == NULL) {
        return NIC_ERR;
    }
    if (c->attribute == NIC_OP_STATUS) {             /* discovery reply: (re)register */
        return (nic_master_add_node(m, c->station, c->value) >= 0) ? NIC_OK : NIC_ERR;
    }
    int i = find_node(m, c->station);
    if (i < 0) {
        return NIC_ERR;
    }
    switch (c->attribute) {
        case NIC_OP_ACK:     if (m->nodes[i].pending_op == c->value) m->nodes[i].pending_op = 0; break;
        case NIC_OP_ERROR:   m->nodes[i].synced = 0; break;          /* lost / desynced */
        case NIC_OP_HEALTH:  m->nodes[i].link_errors = c->value; break;
        case NIC_OP_VERSION: m->nodes[i].version = c->value; break;
        case NIC_OP_NODE_TYPE: m->nodes[i].type  = c->value; break;     /* D26 */
        case NIC_OP_GET_VOLTAGE: m->nodes[i].volt_dv     = c->value;         break;
        case NIC_OP_GET_CPUTEMP: m->nodes[i].cpu_temp_c  = (int8_t)c->value; break;
        case NIC_OP_SENSOR_TEST: m->nodes[i].sensor_mask = c->value;         break;
        default:            break;
    }
    return NIC_OK;
}

int nic_master_expect_ack(nic_master_t *m, uint8_t addr, uint8_t op) {
    int i = find_node(m, addr);
    if (i < 0) return NIC_ERR;
    m->nodes[i].pending_op = op;
    return i;
}

int nic_master_pending(const nic_master_t *m, uint8_t addr) {
    int i = find_node(m, addr);
    return (i < 0) ? 0 : m->nodes[i].pending_op;
}

int nic_master_node_lost(nic_master_t *m, uint8_t addr) {
    int i = find_node(m, addr);
    if (i < 0) return NIC_ERR;
    m->nodes[i].synced = 0;
    return i;
}

int nic_master_all_lost(const nic_master_t *m) {
    if (m->count <= 0) return 0;
    for (int i = 0; i < m->count; i++) {
        if (m->nodes[i].present && m->nodes[i].synced) return 0;
    }
    return 1;
}

int nic_master_cmd(uint8_t station, uint8_t op, uint8_t value,
                  uint8_t *out, size_t cap) {
    return nic_link_ctrl_encode(station, op, value, out, cap);
}

int nic_master_tick(uint8_t *out, size_t cap) {
    return nic_link_ctrl_encode(NIC_LINK_BROADCAST, NIC_OP_TICK, 0, out, cap);
}
