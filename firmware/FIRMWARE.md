<p align="center">
  <img src="NIC-Quake.svg" width="200"/>
</p>

★ N.I.C. ★

# NIC-QUAKE — Node Firmware

**Version 0.5.0**

Firmware for the STM32H503 node. Built the same way as the rest of the NIC
ecosystem: a handful of small, independent libraries glued together. Edit one
module, run it through the compiler, recombine — that's the whole idea.

> Status: **portable core complete**. Every hardware-agnostic library — sensor
> drivers (probe/init/read, per-sensor die temperature, runtime range), auto-detection, axis levelling, the universal link
> bridge, the clock-switch sequencing and the node lifecycle/packer — is
> implemented and host-tested (11 suites, all passing). The STM32 board layer
> (`node/`) that wires it all to the chip is **written** — board.c (callbacks)
> + main_app.c (runtime) + CubeMX integration notes — but it is not host-built
> or hardware-validated yet (needs the ARM toolchain, a CubeMX project, and the
> real board with a scope for the clock-switch path).

---

## Design principle — hardware-agnostic libraries + thin glue

Every `nic-*` (universal core, in `core/`) and `nq-*` (seismo) library is
**portable C with no STM32 dependency**. It reaches hardware only through
callbacks it is handed — an SPI transfer, a UART send, a flash write.
Consequences:

- each library compiles on its own,
- it can be **unit-tested on a plain PC** (no MCU, no ARM toolchain),
- the only code tied to the STM32 is the **board layer** in `node/` (board.c).

```
        ┌─────────── portable C (host-testable) ───────────┐
        │  nq-icm42688   nq-adxl355   nq-sensors            │
        │  nic-clocksync  nic-link      nq-cal                │
        └──────────────────────▲───────────────────────────┘
                               │ callbacks (SPI / UART / flash)
        ┌──────────────────────┴───────────────────────────┐
        │  node/ board layer — STM32H503 glue (CubeMX → C)  │
        └───────────────────────────────────────────────────┘
```

## Modules

| Library | Role | Status |
|---|---|---|
| `nic-common` | Shared types, SPI bus abstraction | ✅ |
| `nq-icm42688` | ICM-42688-P IMU driver (SPI1) | ✅ probe + init/read + temp + runtime range |
| `nq-adxl355` | ADXL355 seismic accelerometer driver (SPI2) | ✅ probe + init/read + temp + runtime range |
| `nq-scl3300` | SCL3300 inclinometer driver (SPI3) | ✅ probe + init + acc/ang/temp |
| `nq-sensors` | Startup auto-detection, unified sample API | ✅ detect done |
| `nic-clocksync` | UART2 ↔ HSE_IN switch + watchdog recovery (mandatory ordering) | ✅ sequencing |
| `nic-link` | Universal frame bridge — framing, addressing, CRC16; data/control are just TYPEs, semantics live in the app | ✅ |
| `nq-cal` | Gravity-vector axis levelling + transform, computed & applied on the node | ✅ math done |
| `nic-node` | Node lifecycle state machine + data packer (portable) | ✅ core |
| `nic-master` | Master orchestration — topology, token scheduler, DATA ingest, command builder (portable) | ✅ |
| `node/` (board layer) | STM32H503 HAL glue: callbacks (board.c) + runtime (main_app.c) + CubeMX notes | 🔌 written, not built/validated |

## Hardware bus map (fixed — one sensor per SPI, no sharing)

| Sensor | Bus | Role | ID | Expected |
|---|---|---|---|---|
| ICM-42688-P | SPI1 | IMU | `WHO_AM_I` 0x75 | 0x47 |
| ADXL355 | SPI2 | seismic accel (±4g) | `DEVID_AD`/`PARTID` | 0xAD / 0xED |
| SCL3300 | SPI3 | inclinometer / drift ref (Mode 4) | `WHOAMI` (32-bit frame) | 0xC1 |

Each sensor sits on its own dedicated SPI, so the bus identity is the sensor
identity; auto-detect only confirms presence. The SCL3300 speaks a different
32-bit pipelined framed protocol (CRC-8) from the register-mapped ADXL355/ICM.

## Build & test (host)

```sh
cmake -S quake -B quake/build
cmake --build quake/build
ctest --test-dir quake/build --output-on-failure
```

The tests use mock SPI buses, so they run anywhere gcc/clang runs — no STM32
and no ARM toolchain needed. The `node/` board layer gets its own cross-compiled
build (arm-none-eabi-gcc) and is added when the board lands.

---

## License

MIT License — Copyright (c) 2026 NIC — Native Intellect Community
