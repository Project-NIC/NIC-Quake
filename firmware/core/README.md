★ N.I.C. ★

# NIC Core — the universal platform

This is the **front-agnostic** half of NIC: the libraries and hardware blocks that
every product front (seismo, weather, iono, …) reuses *unchanged*. Core knows nothing
about seismometers, rain gauges or GNSS receivers — it moves bytes, distributes a
clock, keeps time, schedules slots and talks to the master uplink. All product meaning
lives in a front's glue, never in here.

> **Guardrail.** If wiring up a front tempts you to reach into a core library and add
> product-specific meaning, stop: that meaning belongs in the front's glue. The core
> stays generic so a new front costs zero core changes (config is generic — see
> `DESIGN.md` D28).

---

## The load-bearing principle — a node depends only on the NodeBus

**A Node is hardware-independent. It depends on nothing but the NodeBus protocol — so
it runs anywhere, with anything, connected to anything.**

That is the whole reason the platform is universal, and it is stated here **once**:

- A node is not "a seismograph" or "a weather station". It is *a thing that speaks
  NodeBus*. What it senses is a detail of its front glue.
- It makes no assumptions about who its neighbours are, what the master is, what the
  other nodes measure, or what cabling it hangs on. It boots, announces its **number**
  (which box) and **type** (what kind), takes a TDMA slot, and streams frames.
- Therefore any node runs on any NIC bus, mixed freely with any other node types, behind
  any master — because the only contract between them is the NodeBus protocol (the
  `nic-link` frames, the control opcodes, the shared clock and the self-running slots).

Everything else in `core/` exists to make that contract small, robust and portable.

### NodeBus, in one paragraph

**NodeBus** is the RS-485 data + clock backbone the nodes hang on. One pair carries the
half-duplex UART data (self-timed TDMA slots); a second pair carries the master's
8.192 MHz clock, regenerated at each transceiver so it survives long cable. A node's
leaf sensors that happen to be COTS Modbus devices sit on a *separate* listed Modbus
RS-485 from that node — same physics, different role. See `blocks/bus-485-clock.md`
(NodeBus) and `blocks/bus-modbus.md` (the meteo leaf bus).

---

## Libraries (`lib/`)

Every `nic-*` library is **portable C with no MCU dependency**. It reaches hardware only
through callbacks it is handed (SPI transfer, UART send, flash write, clock poke), so it
compiles and unit-tests on a plain PC.

| Library | Role |
|---|---|
| `nic-common` | Shared types, control-plane opcodes (`nic_proto.h`), SPI handle, CRC-16, 3×3 math — header-only |
| `nic-link` | Universal frame bridge — framing, addressing, CRC16; DATA / CONTROL are just frame kinds, semantics live in the app |
| `nic-node` | Node lifecycle state machine + data packer (portable core of any node) |
| `nic-master` | Master orchestration — topology, slot scheduling, DATA ingest, command builder |
| `nic-clocksync` | Clock-pin switch sequencing (RC → HSE) + watchdog recovery |
| `nic-timesync` | PPS-anchored absolute time — anchor + sample-index interpolation (no per-sample timestamps) |
| `nic-lora` | Master uplink: fixed 16-byte LoRa telemetry frame (values, not text) |

The remaining hardware-bound pieces (the board HAL that supplies the callbacks) live in
each **front's** glue (`../node`, `../master`, …), because that is where the
MCU and the sensor wiring are known.

## Hardware blocks (`blocks/`)

Reusable hardware blocks shared by every node and master — copy a block, don't redraw it:

| Block | What |
|---|---|
| `bus-485-clock.md` | The NodeBus: RS-485 data pair + regenerated 8.192 MHz clock pair, termination |
| `bus-modbus.md` | The leaf Modbus RS-485 from a node to its COTS sensors |
| `gps-pps.md` | GPS PPS / time link — commodity module + basic RS-485 wrap (1/2/3 pairs) |
| `datalogger.md` | The master / head-end (ESP32 + TCXO + microSD + uplink) |
| `protection-485.md` | Tiered RS-485 surge protection (SHORT / LONG / ISO), identical on both NodeBus pairs |
| `protection-power.md` | Tiered source-side power protection, isolated DC-DC |

See also `HARDWARE.md` for how the blocks assemble into a station, and `DESIGN.md` for
the universal decision log (D-numbers).

---

## Build & test (host)

```sh
cmake -S core -B core/build
cmake --build core/build
ctest --test-dir core/build --output-on-failure
```

Six suites, mock-backed, run anywhere gcc/clang runs — no MCU, no ARM toolchain. The
fronts build on top (`cmake -S ..`) and pull this core in automatically.

---

## License

Software: MIT. Hardware blocks: CERN-OHL-S v2. Copyright (c) 2026 NIC — Native Intellect Community.
