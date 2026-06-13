# NIC HW Block — Datalogger (master)

*Reusable hardware block — the head-end / master.*

Head-end of one RS-485 segment: distributes the clock, starts the self-running TDMA,
timestamps + stores + uplinks. Mains/USB powered (not buried) — no low-noise
constraints.

| Item | Part | Note |
|---|---|---|
| Brain | **ESP32** (or clone) | Wi-Fi/BT + UART; master orchestration + storage |
| Network clock | dedicated **8.192 MHz TCXO** | separate from the ESP32's 40 MHz Wi-Fi crystal; **wired straight to the clock-pair transceiver input** (never through the MCU — lowest jitter), MCU only drives the transceiver **DE** to gate it onto the bus. 8.192 = 8×1.024 → integer node clocks |
| RS-485 | 2× THVD1450 | data + clock pair (see block `bus-485-clock`) |
| Storage | microSD | local NIC-MLA archive |
| Time | NTP (Wi-Fi) or RTC DS3231 | RTC second + node index → MLA timestamp + subsec |
| Uplink | Wi-Fi / LoRa | stream + failure alerts |
| Power | mains/USB | injects 12 V bus power onto the cable (see block `protection-power`) |

The master is one end of the bus → it carries the 80 Ω shunt termination
(see block `bus-485-clock`).
