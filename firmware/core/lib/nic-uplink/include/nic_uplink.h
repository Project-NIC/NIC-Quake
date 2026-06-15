#ifndef NIC_UPLINK_H
#define NIC_UPLINK_H

/*
 * nic-uplink — the head-end's record spool / batcher.
 *
 * A node never uplinks (it only speaks the NodeBus); the head-end timestamps,
 * stores and uplinks. Two pressures make per-record I/O wrong here: small SD
 * writes wreck the card (erase-block write amplification + FAT metadata
 * hot-spots), and per-packet IP/Wi-Fi overhead dwarfs a single small record. So
 * finished records are accumulated and flushed as ONE batch on whichever comes
 * first — **N records or T milliseconds** (~1 s).
 *
 * The batch goes to a durable sink (SD / MLA, written FIRST so the card is the
 * source of truth) and/or a live sink (Wi-Fi / Ethernet / SeedLink, best-effort
 * on top). That is store-and-forward: a dropped live link never costs data (the
 * durable copy forwards later), and a crash loses at most the open batch (~1 s).
 * Either sink may be NULL — {store+send, store-only, send-only} are the three
 * modes. Transport- and payload-agnostic per D25; this is its mechanism (D29).
 *
 * Records are fixed-stride and one source per spool — one MLA file per station,
 * never mixing sources. Big-endian, closed with CRC-16-CCITT (same routine as
 * nic-link / nic-lora). Portable C: the sinks are callbacks supplied by the glue.
 *
 *   [MAGIC][source][rec_len][count][base_unix32][record x count][CRC16]
 *      1      1        1       1         4         count*rec_len     2
 */

#include <stdint.h>
#include <stddef.h>

#define NIC_UPLINK_MAGIC        0x42u   /* 'B' — batch frame */
#define NIC_UPLINK_HEADER       8u      /* magic, source, rec_len, count, base_unix32 */
#define NIC_UPLINK_OVERHEAD     (NIC_UPLINK_HEADER + 2u)   /* + CRC16 */
#define NIC_UPLINK_MAX_RECORDS  255u    /* count is one byte */

/* A sink returns 0 on success, <0 on failure (link down / write error). The
 * durable sink failing is data-at-risk; the live sink failing is routine —
 * store-and-forward catches up from the durable copy. */
typedef int (*nic_uplink_sink_fn)(void *ctx, const uint8_t *batch, size_t len);

typedef struct {
    /* --- config: set before the first push, then left alone --- */
    uint8_t  *buf;          /* batch assembly buffer (caller-owned)             */
    size_t    buf_cap;      /* its capacity; >= HEADER + max_records*rec_len + 2 */
    uint8_t   source;       /* station / source id (one spool/file per source)  */
    uint8_t   record_len;   /* fixed record stride (e.g. 16 or 28)              */
    uint8_t   max_records;  /* N flush trigger (1..NIC_UPLINK_MAX_RECORDS)      */
    uint32_t  max_ms;       /* T flush trigger, ms (e.g. 1000); 0 = no timeout  */
    nic_uplink_sink_fn store; /* durable sink (SD/MLA); NULL = no archive       */
    nic_uplink_sink_fn send;  /* live sink (Wi-Fi/SeedLink); NULL = no stream   */
    void     *ctx;          /* opaque, passed to both sinks                     */
    /* --- state: owned by the library --- */
    uint8_t   count;        /* records in the open batch                        */
    uint32_t  open_ms;      /* now_ms when the batch opened                     */
    uint32_t  base_unix_s;  /* wall-clock second stamped on the open batch      */
} nic_uplink_t;

/* flush results: >= 0 success (bitwise), < 0 error */
#define NIC_UPLINK_EMPTY    0     /* nothing was pending                        */
#define NIC_UPLINK_STORED   0x1   /* durable sink succeeded                     */
#define NIC_UPLINK_SENT     0x2   /* live sink succeeded                        */
#define NIC_UPLINK_ESTORE   (-1)  /* durable sink present and failed; batch kept */
#define NIC_UPLINK_EARG     (-2)  /* bad arguments / would overflow buf         */

/* Drop the open batch without flushing (count -> 0). */
void nic_uplink_reset(nic_uplink_t *u);

/* Append one record (record_len bytes), stamping the batch with unix_s / now_ms
 * when it is the batch's first record. If the batch then reaches max_records it
 * is flushed and that flush result is returned; otherwise 0. NIC_UPLINK_EARG on
 * bad args or if the record would not fit buf. */
int nic_uplink_push(nic_uplink_t *u, const uint8_t *record,
                    uint32_t unix_s, uint32_t now_ms);

/* Time-driven flush — call periodically. Flushes the open batch when max_ms has
 * elapsed since it opened. Returns the flush result, or NIC_UPLINK_EMPTY. */
int nic_uplink_tick(nic_uplink_t *u, uint32_t now_ms);

/* Frame the open batch (header + records + CRC16) and hand it to store (durable,
 * first) then send (live, best-effort). Durable-first means a send failure never
 * costs data. Returns NIC_UPLINK_EMPTY if nothing pending; the OR of
 * NIC_UPLINK_STORED / NIC_UPLINK_SENT for the sink(s) that succeeded; or
 * NIC_UPLINK_ESTORE if the durable sink was present and failed (the batch is
 * retained for retry, not dropped). */
int nic_uplink_flush(nic_uplink_t *u);

#endif /* NIC_UPLINK_H */
