#ifndef NIC_LORA_H
#define NIC_LORA_H

/*
 * nq-lora — the master's compact LoRa telemetry frame: a fixed 16-byte packet of
 * VALUES (not text warnings), broadcast periodically. A "warning" is just a value
 * crossing a threshold, so the receiver decides — the link stays tiny. Big-endian,
 * closed with CRC-16-CCITT (the same routine as nq-link, configured once).
 *
 *   [MAGIC][master][unix32][volt][temp][events16][alive][status][peak16][CRC16]
 *      1      1       4      1     1       2         1      1       2       2   = 16
 *
 *   master  — this head-end's id
 *   unix32  — RTC second (also "some time")
 *   volt    — master supply, 0.1 V/LSB
 *   temp    — enclosure / master temperature, deg C
 *   events  — seismic event count ("pocet kmitu")
 *   alive   — bit k = node k present & synced
 *   status  — OR of the nodes' D24 status flags across the network
 *   peak    — last event peak amplitude (proxy)
 */

#include <stdint.h>
#include <stddef.h>

#define NIC_LORA_MAGIC  0x4Cu   /* 'L' */
#define NIC_LORA_LEN    16u

typedef struct {
    uint8_t  master;
    uint32_t unix_s;
    uint8_t  volt_dv;
    int8_t   temp_c;
    uint16_t events;
    uint8_t  alive;
    uint8_t  status;
    uint16_t peak;
} nic_lora_status_t;

/* Pack into out[NIC_LORA_LEN]. Returns NIC_LORA_LEN, or -1 on bad args / capacity. */
int nic_lora_pack(const nic_lora_status_t *s, uint8_t *out, size_t cap);

/* Validate (magic + CRC) and unpack. Returns NIC_LORA_LEN, or -1 on bad magic /
 * CRC / short buffer. */
int nic_lora_unpack(const uint8_t *buf, size_t len, nic_lora_status_t *out);

#endif /* NIC_LORA_H */
