#include "nic_uplink.h"
#include "nic_crc.h"

void nic_uplink_reset(nic_uplink_t *u) {
    if (u != NULL) {
        u->count = 0;
    }
}

int nic_uplink_flush(nic_uplink_t *u) {
    if (u == NULL || u->buf == NULL) {
        return NIC_UPLINK_EARG;
    }
    if (u->count == 0) {
        return NIC_UPLINK_EMPTY;
    }

    /* Records are already laid down in buf after the header (see push). Write
     * the header over the front, then the CRC over [magic .. last record]. */
    size_t n = (size_t)NIC_UPLINK_HEADER + (size_t)u->count * u->record_len;
    if (n + 2u > u->buf_cap) {
        return NIC_UPLINK_EARG;
    }
    u->buf[0] = NIC_UPLINK_MAGIC;
    u->buf[1] = u->source;
    u->buf[2] = u->record_len;
    u->buf[3] = u->count;
    u->buf[4] = (uint8_t)(u->base_unix_s >> 24);
    u->buf[5] = (uint8_t)(u->base_unix_s >> 16);
    u->buf[6] = (uint8_t)(u->base_unix_s >> 8);
    u->buf[7] = (uint8_t)(u->base_unix_s);

    uint16_t crc = nic_crc16_ccitt(u->buf, n);
    u->buf[n]     = (uint8_t)(crc >> 8);
    u->buf[n + 1] = (uint8_t)(crc);
    size_t total = n + 2u;

    /* Durable first: the card is the source of truth. If it is present and
     * fails, the data is at risk — keep the batch for the caller to retry, do
     * not stream a copy we could not archive. */
    int result = 0;
    if (u->store != NULL) {
        if (u->store(u->ctx, u->buf, total) == 0) {
            result |= NIC_UPLINK_STORED;
        } else {
            return NIC_UPLINK_ESTORE;
        }
    }
    /* Live best-effort. A failure here is routine (link down): when a durable
     * copy exists it forwards later; in send-only mode the batch is dropped,
     * which is that mode's accepted cost. */
    if (u->send != NULL) {
        if (u->send(u->ctx, u->buf, total) == 0) {
            result |= NIC_UPLINK_SENT;
        }
    }

    u->count = 0;
    return result;
}

int nic_uplink_push(nic_uplink_t *u, const uint8_t *record,
                    uint32_t unix_s, uint32_t now_ms) {
    if (u == NULL || u->buf == NULL || record == NULL ||
        u->record_len == 0u || u->max_records == 0u) {
        return NIC_UPLINK_EARG;
    }
    /* The record must fit after the current contents, leaving room for the CRC. */
    size_t off = (size_t)NIC_UPLINK_HEADER + (size_t)u->count * u->record_len;
    if (off + u->record_len + 2u > u->buf_cap) {
        return NIC_UPLINK_EARG;
    }

    if (u->count == 0) {
        u->base_unix_s = unix_s;
        u->open_ms = now_ms;
    }
    for (uint8_t i = 0; i < u->record_len; i++) {
        u->buf[off + i] = record[i];
    }
    u->count++;

    if (u->count >= u->max_records) {
        return nic_uplink_flush(u);
    }
    return 0;
}

int nic_uplink_tick(nic_uplink_t *u, uint32_t now_ms) {
    if (u == NULL) {
        return NIC_UPLINK_EARG;
    }
    if (u->count == 0 || u->max_ms == 0u) {
        return NIC_UPLINK_EMPTY;
    }
    /* Unsigned wrap-safe elapsed compare. */
    if ((uint32_t)(now_ms - u->open_ms) >= u->max_ms) {
        return nic_uplink_flush(u);
    }
    return NIC_UPLINK_EMPTY;
}
