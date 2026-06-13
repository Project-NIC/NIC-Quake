# NIC-Quake — Master (ESP32 head-end)

The master is the head end of one RS-485 segment: it distributes the 8.192 MHz
clock, assigns slots, starts the network, then listens, stores and uplinks
(hardware: block `datalogger`).

## Layout
- `main_app.c` — orchestration: composes the portable cores + the board glue.
  **NOT host-built** (ESP32 glue), but it uses only portable calls.
- `board_master.h / .c` — the ESP32 board layer (RS-485, gated clock, RTC,
  microSD, LoRa, Wi-Fi). **Design-stage stubs — VALIDATE ON HW.**
- Portable cores (host-tested, in `../lib/`): `nq-master` (topology / ingest / gap
  / control), `nq-lora` (the 16-byte telemetry frame), `nq-link` (frames + CRC).

## Flow
1. **Discover** by hardware number (poll 1..N → `STATUS` = payload length), then ask
   each its **type** (`NODE_TYPE` → seismo / Basic / iono / starDust). The number is
   *which box*, the type is *what kind* (D23 / D26); register each node.
2. **Assign** each a contiguous slot = software address (`ASSIGN_ADDR`; D23/D24 —
   no separate set-token-order).
3. **Start**: gate the clock, broadcast `SYNC` (nodes switch to HSE + sample).
4. **Run**: a `TICK` each RTC second; `ingest` DATA → NIC-MLA; `RESEND_DATA` on a
   CRC miss; KO-on-silence (a silent slot → zeros + dead marker); ask on demand
   (`GET_VOLTAGE` / `SENSOR_TEST` / `GET_CPUTEMP`); periodic LoRa telemetry.

The seismic **event count** and **peak** come from an STA/LTA trigger (`nq-detect`)
on the primary node: a **strong** event is sent over LoRa immediately (rate-limited),
**weak** ones are summed into the periodic frame.

## Why a dual-core ESP32

Two cores, split by responsibility so the slow, stally work can never disturb the
capture path:
- **Core A — capture & store (the datalogger):** receive the bus over **UART DMA into
  a RAM ring buffer**, decode to NIC-MLA, and drain it to the microSD card. The RX is
  DMA-backed and the buffer absorbs the card's stalls (a cheap SD can pause tens of ms
  for wear-levelling), so a slow write never drops a frame — it just catches up. The
  STA/LTA detector can run here, on the data as it arrives.
- **Core B — communication:** Wi-Fi and LoRa (uplink, NTP, the resend channel). This is
  the worst staller and it sits fully off the capture core — which is also how ESP-IDF
  wants it, since the Wi-Fi/BT stack pins to its own core anyway.

This is comfortable precisely because the clock runs itself (D24): the master no longer
hands out a token every sample — it is mostly a **listener** plus a once-a-second
`TICK`, so the hard real-time burden on the bus side is light. Capture+store on one core,
comms on the other; each owns a coherent job and they hand off through the buffer.

## Uplink (D25)
- **LoRa = outbound telemetry only** — a 16-byte *values* frame (`nq-lora`). A
  **strong** seismic event is sent immediately (rate-limited so aftershocks don't
  flood); **weak** events are summed into a **periodic ~15-min** frame (duty cycle).
  The single downlink is a **resend** on bad reception — no config/control. It
  mirrors RS-485 `RESEND_DATA`, one tier up.
- **Wi-Fi = optional rich channel** — bulk NIC-MLA upload, NTP, a small
  config/status API, master-only OTA. Fully decoupled: the network records and
  runs **without** it (SD store-and-forward).

## Storage
A decoded `nic_master_record_t` maps straight onto NIC-MLA: RTC second →
`timestamp`, the 2-byte sample index → `subsec`, via NIC-GLUE-IN → NIC-DMD →
NIC-MLA. Nodes are dumb; the master is smart.
