#include "nic_lora.h"
#include "nic_crc.h"

int nic_lora_pack(const nic_lora_status_t *s, uint8_t *out, size_t cap) {
    if (s == NULL || out == NULL || cap < NIC_LORA_LEN) {
        return -1;
    }
    out[0]  = NIC_LORA_MAGIC;
    out[1]  = s->master;
    out[2]  = (uint8_t)(s->unix_s >> 24);
    out[3]  = (uint8_t)(s->unix_s >> 16);
    out[4]  = (uint8_t)(s->unix_s >> 8);
    out[5]  = (uint8_t)(s->unix_s);
    out[6]  = s->volt_dv;
    out[7]  = (uint8_t)s->temp_c;
    out[8]  = (uint8_t)(s->events >> 8);
    out[9]  = (uint8_t)(s->events);
    out[10] = s->alive;
    out[11] = s->status;
    out[12] = (uint8_t)(s->peak >> 8);
    out[13] = (uint8_t)(s->peak);
    uint16_t crc = nic_crc16_ccitt(out, 14);
    out[14] = (uint8_t)(crc >> 8);
    out[15] = (uint8_t)(crc);
    return (int)NIC_LORA_LEN;
}

int nic_lora_unpack(const uint8_t *buf, size_t len, nic_lora_status_t *out) {
    if (buf == NULL || out == NULL || len < NIC_LORA_LEN || buf[0] != NIC_LORA_MAGIC) {
        return -1;
    }
    uint16_t want = (uint16_t)(((uint16_t)buf[14] << 8) | buf[15]);
    if (nic_crc16_ccitt(buf, 14) != want) {
        return -1;
    }
    out->master  = buf[1];
    out->unix_s  = ((uint32_t)buf[2] << 24) | ((uint32_t)buf[3] << 16)
                 | ((uint32_t)buf[4] << 8)  |  (uint32_t)buf[5];
    out->volt_dv = buf[6];
    out->temp_c  = (int8_t)buf[7];
    out->events  = (uint16_t)(((uint16_t)buf[8] << 8) | buf[9]);
    out->alive   = buf[10];
    out->status  = buf[11];
    out->peak    = (uint16_t)(((uint16_t)buf[12] << 8) | buf[13]);
    return (int)NIC_LORA_LEN;
}
