#include "nic_link.h"
#include "nic_crc.h"

/* ---- Data frame -------------------------------------------------------- */

int nic_link_data_encode(uint8_t station, uint16_t index,
                        const uint8_t *payload, uint8_t len,
                        uint8_t *out, size_t cap) {
    if (out == NULL || (len > 0 && payload == NULL)) {
        return NIC_ERR;
    }
    size_t total = (size_t)NIC_LINK_DATA_OVERHEAD + (size_t)len;
    if (cap < total) {
        return NIC_ERR;
    }

    out[0] = NIC_LINK_MAGIC_DATA;
    out[1] = station;
    out[2] = (uint8_t)(index >> 8);
    out[3] = (uint8_t)(index & 0xFFu);
    for (uint8_t i = 0; i < len; i++) {
        out[NIC_LINK_DATA_HEADER + i] = payload[i];
    }

    /* CRC over station..payload (everything but the magic and the CRC). */
    uint16_t crc = nic_crc16_ccitt(&out[1], (size_t)(NIC_LINK_DATA_HEADER - 1u) + len);
    out[NIC_LINK_DATA_HEADER + len]     = (uint8_t)(crc >> 8);
    out[NIC_LINK_DATA_HEADER + len + 1] = (uint8_t)(crc & 0xFFu);
    return (int)total;
}

int nic_link_data_decode(const uint8_t *buf, size_t len,
                        uint8_t payload_len, nic_link_data_t *out) {
    if (buf == NULL || out == NULL) {
        return NIC_ERR;
    }
    size_t total = (size_t)NIC_LINK_DATA_OVERHEAD + (size_t)payload_len;
    if (len < total || buf[0] != NIC_LINK_MAGIC_DATA) {
        return NIC_ERR_FORMAT;
    }

    uint16_t want = (uint16_t)(((uint16_t)buf[NIC_LINK_DATA_HEADER + payload_len] << 8)
                             |  (uint16_t)buf[NIC_LINK_DATA_HEADER + payload_len + 1]);
    uint16_t got = nic_crc16_ccitt(&buf[1], (size_t)(NIC_LINK_DATA_HEADER - 1u) + payload_len);
    if (want != got) {
        return NIC_ERR_CRC;
    }

    out->station = buf[1];
    out->index   = (uint16_t)(((uint16_t)buf[2] << 8) | buf[3]);
    out->len     = payload_len;
    out->payload = (payload_len > 0) ? &buf[NIC_LINK_DATA_HEADER] : NULL;
    return NIC_OK;
}

/* ---- Control frame ----------------------------------------------------- */

int nic_link_ctrl_encode(uint8_t station, uint8_t attribute, uint8_t value,
                        uint8_t *out, size_t cap) {
    if (out == NULL || cap < NIC_LINK_CTRL_LEN) {
        return NIC_ERR;
    }
    out[0] = NIC_LINK_MAGIC_CTRL;
    out[1] = station;
    out[2] = attribute;
    out[3] = value;
    uint16_t crc = nic_crc16_ccitt(&out[1], 3);   /* over station, attribute, value */
    out[4] = (uint8_t)(crc >> 8);
    out[5] = (uint8_t)(crc & 0xFFu);
    return (int)NIC_LINK_CTRL_LEN;
}

int nic_link_ctrl_decode(const uint8_t *buf, size_t len, nic_link_ctrl_t *out) {
    if (buf == NULL || out == NULL) {
        return NIC_ERR;
    }
    if (len < NIC_LINK_CTRL_LEN || buf[0] != NIC_LINK_MAGIC_CTRL) {
        return NIC_ERR_FORMAT;
    }
    uint16_t want = (uint16_t)(((uint16_t)buf[4] << 8) | buf[5]);
    if (nic_crc16_ccitt(&buf[1], 3) != want) {
        return NIC_ERR_CRC;
    }
    out->station   = buf[1];
    out->attribute = buf[2];
    out->value     = buf[3];
    return NIC_OK;
}
