#ifndef NQ_BOARD_MASTER_H
#define NQ_BOARD_MASTER_H

/*
 * ESP32 master board support — the hardware-bound layer of the head-end, the
 * counterpart to the node's board.h. It supplies the callbacks the portable
 * nq-master + nq-lora cores need: RS-485 (data + clock pair), the gated
 * 8.192 MHz network clock, RTC, storage (NIC-MLA), and the LoRa / Wi-Fi uplinks.
 *
 * NOT host-built and NOT host-tested (the cores it drives ARE). NOT validated on
 * a board. The uplink split is deliberate (D25): LoRa is a tiny outbound
 * telemetry beacon (its ONLY downlink is "resend"); Wi-Fi is the optional rich
 * channel (bulk upload / NTP / config / master OTA) and the network runs fully
 * without it.
 */

#include "nic_master.h"
#include <stdint.h>
#include <stddef.h>

void board_master_init(void);
void board_master_watchdog_kick(void);

/* RS-485: send a control frame; receive a DATA or CONTROL frame (idle-delimited).
 * recv returns NIC_OK with *out_len = 0 when the window was silent. */
int  board_master_send(const uint8_t *buf, size_t len);
int  board_master_recv(uint8_t *buf, size_t cap, size_t *out_len, uint32_t timeout_ms);

/* Gate the dedicated 8.192 MHz oscillator onto the clock pair (start the network
 * timebase). board_master_clock_suspect() flags that our OWN oscillator looks
 * dead (every node desynced at once). */
void board_master_clock_start(void);
void board_master_clock_suspect(void);

/* Absolute time (NTP over Wi-Fi, or a DS3231), and the master's own housekeeping. */
uint32_t board_master_rtc_now(void);
uint8_t  board_master_supply_dv(void);   /* 0.1 V/LSB */
int8_t   board_master_temp_c(void);

/* Storage glue: a decoded record -> NIC-GLUE-IN -> NIC-DMD -> NIC-MLA on the card.
 * store_ko writes the "node dead" marker (zeros + KO) for a silent slot. */
void board_master_store(const nic_master_record_t *rec, uint32_t sec);
void board_master_store_ko(uint8_t addr, uint32_t sec);

/* KO-on-silence: the address of a slot that produced no data this round (0 = none). */
int  board_master_silent_slot(void);

/* LoRa — outbound telemetry only. The single downlink is a resend request: on a
 * bad / missing frame the receiver asks, and the master re-sends the buffered
 * last one. No config / control over LoRa (D25). */
void board_master_lora_send(const uint8_t *buf, size_t len);
int  board_master_lora_resend_requested(void);

/* Wi-Fi — optional rich uplink, fully decoupled (the network works without it).
 * When up, stream the pending NIC-MLA archive to the server. NTP feeds the RTC;
 * a small config/status API and master-only OTA live in the board layer. */
int  board_master_wifi_up(void);
void board_master_wifi_upload(void);

#endif /* NQ_BOARD_MASTER_H */
