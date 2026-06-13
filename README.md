<p align="center">
  <img src="NIC-Quake.svg" width="200"/>
</p>

*[English](README.md) · [Čeština](README_cs.md) · [Русский](README_ru.md)*

★ N.I.C. ★

# NIC-QUAKE

## Distributed Seismic Monitoring Network — Embedded Hardware Reference Design

[![License: MIT](https://img.shields.io/badge/License-MIT-red.svg)](https://opensource.org/licenses/MIT)
[![License: CERN-OHL-S v2](https://img.shields.io/badge/License-CERN--OHL--S%20v2-red.svg)](https://ohwr.org/cern_ohl_s_v2.txt)

---

## What is QUAKE?

QUAKE is an open hardware reference design for a distributed seismograph network. Each node is a sealed, self-contained unit designed to be installed permanently in rock, building foundations, etc. — potted in polyurethane casting compound, connected by a waterproof M12 connector, and left alone for years.

The project grew out of a simple frustration: existing approaches either cost a fortune (geophones), require precise mechanical installation, or compensate for cheap noisy sensors with mountains of software. QUAKE takes the opposite approach — spend more on hardware, get clean data from the start, keep the software simple.

---

## Why not a geophone?

- Costs as much as the entire QUAKE node
- Poor low-frequency response — steep gain drop below resonance, requiring complex correction amplifiers with phase errors
- Three separate units needed for three axes
- Requires precise levelling and mechanical installation

## Why not capacitive sensing?

- The proof mass must be heavy for sensitivity, but heavy means low resonant frequency and easy excitation
- Parasitic capacitance to enclosure walls changes with temperature, humidity, and geometry
- Result: unstable signal that reflects enclosure conditions, not seismic activity

## Why MEMS?

MEMS proof mass is microscopic — resonance is in the kHz range, seismics are below 20 Hz. They never interfere. The sensor is hermetically sealed on a single silicon die, factory calibrated, digital output, three axes in one package. One chip replaces three geophones at a fraction of the cost.

---

## How it works

### Virtual levelling — install it any way you like

No bubble levels. No adjustment screws. No precise mechanical alignment.

Drop the unit into a hole in the rock, send a calibration command, and the processor records the current sensor state as the reference zero. The gravitational vector automatically defines absolute orientation — Z axis perpendicular to Earth's core, regardless of physical orientation. A transformation matrix handles the rest.

### Hardware clock synchronisation

All nodes share a single physical clock distributed by the master over the second RS-485 pair. This is not software timestamping or NTP — it is a real clock signal on a wire. All nodes are phase-locked to the master. Timing uncertainty is in nanoseconds, not milliseconds.

### Network topology

Standard UTP Cat 6 cable, four pairs — two for RS-485 data and clock, two for power. Daisy-chain topology, each node has two M12 IP68 connectors (in and out), termination resistors only at each end of the line. Up to 8 nodes per segment.

### Modular design

Each sensor uses one SPI interface from the MCU with its own Chip Select pin. A universal PCB accommodates all three sensor positions — unused sensors are simply not populated. Firmware auto-detects which sensors are present at startup via SPI probing. No recompilation needed.

---

## Documentation

| Document | Description |
|---|---|
| [HARDWARE.md](HARDWARE.md) | Component selection with rationale |
| [SOFTWARE.md](SOFTWARE.md) | Communication architecture, sync protocol, data stack |
| [LICENSE](LICENSE) | MIT — software |
| [LICENSE-HW](LICENSE-HW) | CERN-OHL-S v2 — hardware |

---

## Data stack

QUAKE uses the NIC software ecosystem for data storage and transport:

| Layer | Library | Role |
|---|---|---|
| Compression | [NIC-DMD](https://github.com/Project-NIC/NIC-DMD) | Adaptive compression, no lookup tables, runs on ATmega328 |
| Storage | [NIC-MLA](https://github.com/Project-NIC/NIC-MLA) | Single-file container, crash-safe, self-describing |
| Transport encryption | [NIC-KSF](https://github.com/Project-NIC/NIC-KSF) | SPECK-128 CTR, in-place, no malloc |
| Write glue | [NIC-GLUE-IN](https://github.com/Project-NIC/NIC-GLUE-IN) | Sensor data → DMD → MLA |
| Read/export | [NIC-GLUE-OUT](https://github.com/Project-NIC/NIC-GLUE-OUT) | MLA → CSV / SQLite |
| Desktop viewer | [NIC-VDE](https://github.com/Project-NIC/NIC-VDE) | Browse MLA files like a directory, export to CSV/SQL |

---

## Status

**Firmware: portable core complete and host-tested (v0.5.0)** — sensor drivers (with per-sensor die temperature and runtime range control), auto-detection, axis calibration, the RS-485 link and the node state machine, all unit-tested off-target; the STM32 board layer is written and awaits the board. **Hardware:** reference design defined, schematics in progress. PCB and field testing are next.

Contributions welcome — hardware, firmware, or software.

---

## License

Hardware: CERN-OHL-S v2 — Copyright (c) 2026 NIC — Native Intellect Community

Software: MIT License — Copyright (c) 2026 NIC — Native Intellect Community

---

## Acknowledgments

Brother for advice during the development of this project.
For technical assistance with code optimisation, to AI assistants Claude (Anthropic) and Gemini (Google).

★ Viva La Resistánce ★
