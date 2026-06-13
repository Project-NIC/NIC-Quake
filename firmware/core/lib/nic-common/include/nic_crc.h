#ifndef NIC_CRC_H
#define NIC_CRC_H

/*
 * CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, xorout 0.
 *
 * Software reference implementation. On the STM32H503 the same result can be
 * produced by the hardware CRC unit configured with this polynomial — the bytes
 * on the wire are identical either way, so a frame built on the host and one
 * built on the MCU verify against each other.
 */

#include <stdint.h>
#include <stddef.h>

static inline uint16_t nic_crc16_ccitt(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (int b = 0; b < 8; b++) {
            if (crc & 0x8000u) {
                crc = (uint16_t)((uint16_t)(crc << 1) ^ 0x1021u);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

#endif /* NIC_CRC_H */
