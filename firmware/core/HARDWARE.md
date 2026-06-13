★ N.I.C. ★

# NIC Core — Hardware (the universal node + station)

The hardware that is the **same on every front**: the node's MCU and power chain, the
NodeBus front-end, the surge protection, the enclosure, and the data station (master).
A front adds only its **sensors** on top of this — see e.g. `../HARDWARE.md` for
the seismo sensor lineup. The detailed, reusable building blocks live in `blocks/`;
this page is how they assemble into a working node and station.

---

## Node — the universal STM32H503 box

| Item | Part | Why |
|---|---|---|
| MCU | **STM32H503** | Cortex-M33 + FPU (the on-node levelling matrix multiply, CRC unit), plenty of SPI/UART/timers, tiny and cheap; runs one firmware for life (core D12) |
| NodeBus transceiver ×2 | **THVD1450** (TI) | 500 kbps / 50 Mbps selectable, edge-regenerating — one for the data pair, one for the clock pair (`blocks/bus-485-clock.md`) |
| Ultra-low-noise LDO | **LT3042** | Clean analog rail for the sensor front; noise floor matters more than efficiency on a buried node |
| Logic LDO | **MCP1700** | Small housekeeping rail |
| Bus → local step-down | **Recom R-78HE5.0** | 12 V NodeBus → 5 V switcher; node-input protection is per `blocks/protection-power.md` |

The node reaches its sensors only through SPI/UART/GPIO **callbacks** supplied by the
board HAL (the front's glue), so the portable `nic-*` core never touches a register.

## NodeBus — RS-485 data + clock

The backbone every node hangs on. UTP Cat 6, daisy-chain, M12 IP68 in/out per node:

- **Data pair** — THVD1450, half-duplex UART, self-timed TDMA slots.
- **Clock pair** — THVD1450, driven from the master's single 8.192 MHz oscillator; the
  node remaps the clock-pair RX onto **HSE_IN**. The oscillator is wired straight to the
  transceiver (never through the MCU) for lowest jitter; the MCU only gates it with DE.
- **Termination 80 + 10** (10 Ω series at every node, 80 Ω shunt at the two ends =
  100 Ω): the bigger series R buys surge coordination at negligible signal cost on Cat 6.

Full detail: `blocks/bus-485-clock.md`. A node's COTS Modbus sensors ride a *separate*
leaf RS-485 (`blocks/bus-modbus.md`) — same physics, listed (not backbone) role.

## Protection — tiered, identical on both NodeBus pairs

Staged SHORT / LONG / ISO surge protection (SM712 fast clamp + 10 Ω series + 80 Ω end
shunt + GDT crowbar + optional ISO1450 isolated tier), the **same on the data and the
clock pair**, plus source-side power protection with isolated DC-DC. Threat model: we
protect against nearby induction; a direct strike vaporises any node-level SPD — a
cheap, sacrificial node is the accepted trade. See `blocks/protection-485.md` and
`blocks/protection-power.md`.

> **Node-input power** is now drawn as a **concept** in `blocks/protection-power.md`
> (three tiers: NOD SHORT / LONG / LONG ISO; 5 V base + optional 12 V; wide ~9–36 V input
> for long 12 V runs). The surrounding passives (decoupling / filter parts) are still TBD.

## Enclosure

Thick-walled aluminium box; all connectors on one wall; the PCB on a **thin rigid
double-sided** board (a stiff bond for a seismograph — no foam); the top potted with a
re-enterable **cable gel** (the box seals, the gel protects and stays repairable;
neutral-cure, −40 °C, bubble-free).

---

## Data station (master / head-end)

The universal head-end of one NodeBus segment: distributes the clock, starts the
self-running TDMA, timestamps + stores + uplinks. Mains/USB powered — no low-noise
constraints. It is **not** seismo-specific: the same data station heads a seismo,
weather or iono bus, learning each node's type at discovery.

| Item | Part | Note |
|---|---|---|
| Brain | **ESP32** | Wi-Fi/BT + UART; master orchestration + storage |
| Network clock | dedicated **8.192 MHz TCXO** | wired straight to the clock-pair transceiver (lowest jitter), MCU only gates DE; 8.192 = 8 × 1.024 → integer node clocks |
| NodeBus | 2× THVD1450 | data + clock pair |
| Storage | microSD | local NIC-MLA archive |
| Time | RTC (DS3231) + optional **GPS PPS** | RTC second + node index → timestamp + subsec; PPS for absolute/global time (core D27) |
| Uplink | LoRa (out-only) + optional Wi-Fi | telemetry beacon + rich when connected (core D25) |
| Power | mains/USB | injects 12 V onto the NodeBus |

The master is one end of the bus, so it carries an 80 Ω shunt termination. Full detail:
`blocks/datalogger.md`.

---

## License

Hardware: CERN-OHL-S v2. Copyright (c) 2026 NIC — Native Intellect Community.
