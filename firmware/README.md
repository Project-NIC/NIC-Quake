<p align="center">
  <img src="NIC-Quake.svg" width="200"/>
</p>

*[English](README.md) · [Čeština](README_cs.md) · [Русский](README_ru.md)*

★ N.I.C. ★

# NIC-Quake — the seismograph (and the universal NIC core)

[![License: MIT](https://img.shields.io/badge/License-MIT-red.svg)](https://opensource.org/licenses/MIT)
[![License: CERN-OHL-S v2](https://img.shields.io/badge/License-CERN--OHL--S%20v2-red.svg)](https://ohwr.org/cern_ohl_s_v2.txt)

Quake is the **trunk of the NIC platform**: an open-hardware reference design for a
distributed seismograph network, built on the universal NIC node core that lives right
here in [`core/`](core). A Quake node is that universal [NIC node](core/README.md)
populated with seismic sensors and a thin seismo glue — that glue is the *only* thing
that makes it a Quake node. Everything generic (the NodeBus, the link/clock/timing/uplink
libraries) is the core in [`core/`](core), which the other NIC instruments (NIC-Station's
weather / iono / stardust fronts) reuse unchanged.

Each node is a sealed, self-contained unit designed to be installed permanently in
rock, building foundations, etc. — potted in casting compound, connected by a
waterproof M12 connector, and left alone for years.

---

## Why MEMS (not a geophone, not capacitive)

A geophone costs as much as a whole Quake node, has poor low-frequency response, needs
three units for three axes and precise levelling. Capacitive sensing drifts with
temperature, humidity and enclosure geometry. **MEMS** has a microscopic proof mass —
resonance in the kHz range, seismics below 20 Hz, so they never interfere — hermetically
sealed on one die, factory-calibrated, digital, three axes per package. One chip
replaces three geophones at a fraction of the cost.

## How a Quake node works

- **Virtual levelling — install it any way you like.** No bubble levels, no adjustment
  screws. Drop the unit in a hole, send a calibration command, and the node records the
  current state as the reference zero. Gravity defines absolute orientation; a rotation
  matrix (`nq-cal`) handles the rest, applied on-node to every sample.
- **Hardware clock synchronisation.** All nodes share one physical clock on the NodeBus
  clock pair — a real clock signal on a wire, not NTP. Phase-locked to nanoseconds. (The
  clock distribution itself is universal core, [`core/DESIGN.md`](core/DESIGN.md) D6.)
- **Auto-detected sensors.** Each sensor sits on its own SPI; firmware probes at boot and
  packs whichever are present. Unused positions are simply not populated, no recompile.

## Seismo sensor lineup

| Sensor | Bus | Role | Rate |
|---|---|---|---|
| ICM-42688-P | SPI1 | IMU (accel + gyro), extended-range headroom | 125 Hz, phase-locked |
| ADXL355 | SPI2 | precision seismic accel (±2g default) | 125 Hz, phase-locked |
| SCL3300 | SPI3 | inclinometer / drift & structural-tilt reference | slow (housekeeping) |

The full rationale (ranges, gyro, dual-range stitching, the SCL3300 modes, the 125 Hz
clock-tuning) is the seismo decision log — [`DESIGN.md`](DESIGN.md) D2–D5, D8, D11,
D14–D18, D21, D22 — built on the universal decisions in
[`core/DESIGN.md`](core/DESIGN.md).

---

## Documentation

| Document | Description |
|---|---|
| [HARDWARE.md](HARDWARE.md) | Seismo component selection (sensors) — universal HW is in [`core/HARDWARE.md`](core/HARDWARE.md) |
| [SOFTWARE.md](SOFTWARE.md) | Comms architecture, sync protocol, data stack |
| [DATAFLOW.md](DATAFLOW.md) | The seismo DATA payload on the wire |
| [DESIGN.md](DESIGN.md) | Seismo design decisions |

## Firmware layout

```
firmware/
  core/     the universal, front-agnostic NIC platform (nic-* libraries):
            nic-link, nic-node, nic-master, nic-clocksync, nic-timesync, nic-lora,
            nic-common — plus the shared HW blocks (NodeBus, datalogger, protection).
            This is the trunk; NIC-Station vendors it for its other fronts.
  lib/      nq-* seismo libraries (portable, host-tested):
            nq-icm42688, nq-adxl355, nq-scl3300, nq-sensors, nq-cal, nq-detect
  node/     STM32H503 node glue — board HAL (board.c) + runtime (main_app.c) + CubeMX notes
  master/   ESP32 data-station glue — board HAL + main_app
  test/     host tests for the seismo libraries (core has its own under core/test)
```

The portable seismo libraries reuse the universal core (`NIC_OK`, `nic_sample3_t`,
`nic_spi_t`, the link/clock/timing libraries) from [`core/`](core).

## Build & test (host)

```sh
cmake -S firmware -B firmware/build      # builds core/ and the seismo front together
cmake --build firmware/build
ctest --test-dir firmware/build --output-on-failure
```

Eleven suites (six core + five seismo), mock-backed — no MCU, no ARM toolchain. The
`node/` and `master/` glue is syntax-checked (`gcc -fsyntax-only -Wall -Wextra`); it is
built and validated on the real STM32 / ESP32 hardware, which is the next step.

---

## Data stack

Quake uses the NIC software ecosystem for storage and transport: **NIC-DMD**
(compression) → **NIC-MLA** (container), **NIC-KSF** (encryption), **NIC-GLUE-IN/OUT**
(write/read), **NIC-VDE** (viewer). Separate repos.

**Seismology interop:** **NIC-MSEED** exports a NIC-MLA log to **miniSEED** (Steim-1/2)
so it drops straight into ObsPy / SeisComP / the FDSN toolchain — it is the seismology
export for Quake and NIC-Station.

## Status

**Firmware: portable core complete and host-tested** — seismo drivers (with per-sensor
die temperature and runtime range control), auto-detection, axis calibration, plus the
universal link / clock / node state machine reused from core; all unit-tested off-target.
The STM32/ESP32 board layers are written and await the boards. **Hardware:** reference
design defined, schematics in progress.

---

## License

Hardware: CERN-OHL-S v2 — Copyright (c) 2026 NIC — Native Intellect Community
Software: MIT — Copyright (c) 2026 NIC — Native Intellect Community

★ Viva La Resistánce ★
