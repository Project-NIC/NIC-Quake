/*
 * ESP32 master board support — design-stage stubs.
 *
 * NOT host-buildable and NOT host-tested — this is the hardware layer (ESP-IDF /
 * Arduino + a LoRa radio + microSD + RTC). Everything here is to bring up on the
 * real board; the portable cores it serves (nq-master, nq-lora) ARE host-tested.
 */

#include "board_master.h"

void     board_master_init(void)                       {}
void     board_master_watchdog_kick(void)              {}

int      board_master_send(const uint8_t *b, size_t n) { (void)b; (void)n; return NIC_OK; }
int      board_master_recv(uint8_t *b, size_t c, size_t *o, uint32_t t) {
    (void)b; (void)c; (void)t; if (o) *o = 0; return NIC_OK;   /* silent window */
}

void     board_master_clock_start(void)                {}
void     board_master_clock_suspect(void)              {}

uint32_t board_master_rtc_now(void)                    { return 0u; }
uint8_t  board_master_supply_dv(void)                  { return 120u; }  /* ~12.0 V */
int8_t   board_master_temp_c(void)                     { return 25; }

void     board_master_store(const nic_master_record_t *r, uint32_t s) { (void)r; (void)s; }
void     board_master_store_ko(uint8_t a, uint32_t s)  { (void)a; (void)s; }

int      board_master_silent_slot(void)                { return 0; }

void     board_master_lora_send(const uint8_t *b, size_t n) { (void)b; (void)n; }
int      board_master_lora_resend_requested(void)      { return 0; }

int      board_master_wifi_up(void)                    { return 0; }
void     board_master_wifi_upload(void)                {}
