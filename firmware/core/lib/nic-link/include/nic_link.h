#ifndef NIC_LINK_H
#define NIC_LINK_H

/*
 * nq-link — the RS-485 framing for NIC-Quake. Two lean, purpose-built frames,
 * each started by its own magic byte (and delimited by the bus idle line); the
 * magic also tells data from control:
 *
 *   DATA  (slave -> master), 125x/s:
 *     [MAGIC_DATA][station][index_hi][index_lo][payload...N][CRC16_hi][CRC16_lo]
 *     - No length field: the payload length is FIXED per node, agreed during the
 *       init handshake, and does not change in operation (absent sensors pad with
 *       zeros). The receiver supplies that expected length to decode.
 *     - index = 2-byte sub-second sample index (maps to NIC-MLA `subsec`).
 *     - CRC16-CCITT over station..payload.
 *
 *   CONTROL (master <-> slave), on demand:
 *     [MAGIC_CTRL][station][attribute][value][CRC16_hi][CRC16_lo]   — 6 bytes
 *     - station: addressed node (0xFF = broadcast), or the sender on a reply.
 *     - attribute: what to do (assign address, set range, calibrate, token...).
 *     - value: the argument.
 *     - CRC16 over station..value (same CRC-16-CCITT as the data frame, so the
 *       STM32 hardware CRC unit is configured once and shared by both).
 *
 * Both directions reuse the control frame; the same code runs on master and
 * node. The library is a dumb transport — it gives no meaning to attribute
 * values; the application does (a runtime setting change is just a control frame
 * the master sends, after which it rolls over to a new storage table).
 */

#include "nic_types.h"
#include <stdint.h>
#include <stddef.h>

#define NIC_LINK_MAGIC_DATA   0xD5u
#define NIC_LINK_MAGIC_CTRL   0xC5u
#define NIC_LINK_BROADCAST    0xFFu

/* ---- Data frame -------------------------------------------------------- */

#define NIC_LINK_DATA_HEADER    4u   /* MAGIC + station + 2B index */
#define NIC_LINK_DATA_OVERHEAD  (NIC_LINK_DATA_HEADER + 2u)   /* + CRC16 */
#define NIC_LINK_MAX_PAYLOAD    255u

typedef struct {
    uint8_t        station;
    uint16_t       index;
    const uint8_t *payload;   /* aliases the source buffer; NULL when len == 0 */
    uint8_t        len;
} nic_link_data_t;

/* Build a data frame. Returns the total frame length, or NIC_ERR on bad args /
 * capacity. */
int nic_link_data_encode(uint8_t station, uint16_t index,
                        const uint8_t *payload, uint8_t len,
                        uint8_t *out, size_t cap);

/* Validate and parse a data frame. `payload_len` is the length the master agreed
 * with this node at init (there is no length field on the wire). Returns NIC_OK,
 * NIC_ERR_FORMAT (bad magic / short buffer), NIC_ERR_CRC, or NIC_ERR. */
int nic_link_data_decode(const uint8_t *buf, size_t len,
                        uint8_t payload_len, nic_link_data_t *out);

/* ---- Control frame ----------------------------------------------------- */

#define NIC_LINK_CTRL_LEN  6u   /* MAGIC + station + attribute + value + CRC16 */

typedef struct {
    uint8_t station;
    uint8_t attribute;
    uint8_t value;
} nic_link_ctrl_t;

/* Build a control frame into `out` (>= NIC_LINK_CTRL_LEN bytes). Returns the
 * frame length, or NIC_ERR. */
int nic_link_ctrl_encode(uint8_t station, uint8_t attribute, uint8_t value,
                        uint8_t *out, size_t cap);

/* Validate and parse a control frame. Returns NIC_OK, NIC_ERR_FORMAT, NIC_ERR_CRC
 * or NIC_ERR. */
int nic_link_ctrl_decode(const uint8_t *buf, size_t len, nic_link_ctrl_t *out);

#endif /* NIC_LINK_H */
