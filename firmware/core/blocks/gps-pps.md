# NIC HW Block — Time: GPS PPS / time link

*Shared block. On any station that needs an absolute-time anchor. The GPS is a plain
commodity module (NMEA/UBX over UART + a 1PPS pin); the RS-485 wrap is ours. Keep in
sync across projects.*

## Module — pick per station
- **Iono station: nothing extra** — the UM980 already on the node gives a timing-grade
  PPS; tap it.
- **Sub-ms absolute (precise seismic): a u-blox M8/M9** — UBX, reports the sawtooth so
  the PPS corrects to ns, configurable; the timing part (M8T) adds a stationary /
  survey-in mode for a steadier fixed-station PPS.
- **Millisecond / meteo: a low-cost module** (e.g. ATGM336H) for NMEA + PPS, **or NTP
  over the ESP32 Wi-Fi**, or no GPS at all.
- Relative timing is the shared clock (sub-µs) regardless; the GPS only sets the
  absolute anchor — see D27.

## Link — basic RS-485, light protection
- PPS and UART leave the module as plain 3.3 V CMOS (not made to drive a cable), so a
  transceiver sits at **each** end (THVD1450, CMOS ↔ differential).
- Low speed and a short run (~5 m, in-enclosure) mean the **basic wiring is enough**:
  the transceivers plus **light ESD protection** (a small TVS, or the transceiver's own
  ESD rating; optional series R). **Not** the bus surge tier (SM712 / GDT / ISO of
  `protection-485`) — the threat is modest and the cheap module brings nothing to match.
- 120 Ω termination is fine but not critical at this length and speed.
- The transceiver adds a small fixed delay (low jitter) that calibrates out like cable
  delay (~5 ns/m).

## Pairs — one piece of UTP
- **PPS only → 1 pair** (the integer second comes from RTC / NTP / LoRa).
- **Data-to-us + PPS → 2 pairs** (receive NMEA for the second + PPS; no talk-back).
- **Two-way comms + PPS → 3 pairs** (RX + TX + PPS, to also configure the module —
  full-duplex UART, no DE timing).
- One GPS per station; more receivers buy nothing — see D27.
