<p align="center">
  <img src="NIC-Quake.svg" width="200"/>
</p>

★ N.I.C. ★

# NIC-QUAKE — Software & Communication Architecture

## Communication Protocol, Synchronisation, and Data Stack

[![License: MIT](https://img.shields.io/badge/License-MIT-red.svg)](https://opensource.org/licenses/MIT)

---

## Node Addressing

Each node has a hardware base address — stored in memory from the time of programming. The address can be software-remapped to any number for token ring communication purposes. A node with hardware address 4 can communicate under address 20 — the hardware address serves as the default fallback after a hard reset.

A hard reset can be requested via a broadcast packet from the master unit. It does not occur on power loss.

---

## Modularity — Build Your Node Your Way

This document describes the **complete reference configuration** of a node with all three sensors. Each builder can adapt the design as needed — omit sensors they do not need, add their own, or use a different MCU.

Each sensor uses **one SPI interface** from the MCU with its own Chip Select pin:

| Sensor | SPI | Function |
|---|---|---|
| ICM-42688-P | SPI1 | 6-axis IMU — gyroscope + accelerometer (also the high-range / fallback seismic accel) |
| ADXL355 | SPI2 | Precision seismic accelerometer |
| SCL3300-D01 | SPI3 | Inclinometer — tilt / long-term drift reference |

A universal PCB accommodates all three positions. An unneeded sensor is simply **not populated** — the corresponding LDO and passive components are left empty, the CS pin remains inactive.

### Sensor Auto-Detection in Firmware

At startup, the MCU probes all SPI buses and attempts to read the identification register of each sensor. A sensor that responds — is present and will be used. A sensor that does not respond — is marked as absent and skipped by firmware. The data packet sent to the master contains only data from present sensors — the master knows from the schema what to expect from a given node.

The same firmware binary therefore runs on a node with one, two, or three sensors — no recompilation, no configuration switches. Where a precision seismic accelerometer is needed, the node prefers the **ADXL355** and falls back to the **ICM-42688's accelerometer** if the ADXL355 is not populated.

---

## Physical Layer — UTP Cat 6, Four Pairs

| Pair | Function | Direction |
|---|---|---|
| Pair 1 | RS-485 data bus — token ring | Bidirectional |
| Pair 2 | Clock signal from master | Unidirectional (master → nodes) |
| Pairs 3+4 | 24 V power | — |

---

## UART Wiring — Direct, No Crossover

STM32H503 has native RS-485 mode in the UART peripheral — automatic DE pin control of the transceiver, separate TX/RX interrupt flagging, and hardware support for receiving one's own echo. This eliminates the need for crossover wiring and the connection is straightforward:

```
UART1 ──▶ THVD1450 #1 — pair 1 (data bus, bidirectional)
UART2 ──▶ THVD1450 #2 — pair 2 (clock signal, master → nodes)
```

The DE pin of each transceiver is controlled directly by UART hardware — it activates automatically during transmission and releases the bus when done.

### Echo Detection and Bus Diagnostics

STM32H503 UART in RS-485 mode can receive its own echo — what the node transmitted is returned to it and captured by the UART without software intervention. The node compares transmitted data with the echo and detects:

- **Bus error** — echo does not match transmitted data
- **Collision** — two nodes transmitted simultaneously (should not occur in token ring, but detection works)
- **Cable degradation** — repeated errors even under light traffic

For a node buried in rock without direct access, this runtime diagnostics is critical — the node reports its own status.

---

## UART Blocking During Clock Signal Reception — Critical Detail

After switching to hardware clock sync, the node must receive the clock signal from pair 2 directly as the MCU HSE input — via the HSE_IN pin, or via internal pin remapping.

**Critical switching sequence:**

```
1. UART2 disables interrupts
2. UART2 is de-initialised
3. UART2 RX pin is remapped to HSE_IN alternative function
4. MCU switches clock source to external HSE input
5. PLL synchronises to incoming clock from master
```

The order is mandatory. If the pin were remapped to HSE_IN before de-initialising UART2, the UART2 peripheral would interpret the clock signal as data bits and generate interrupts. STM32H503 handles remapping atomically.

---

## Start Sequence

### 1. Boot — Internal RC Oscillator

On power-up, all nodes start on the **internal RC oscillator** of STM32H503. Each node listens on the data pair and waits for activity from the master unit.

### 2. Initialisation Communication with Master

The master communicates with nodes sequentially over the data pair — discovers who is present, assigns token ring order, synchronises addresses. Each node confirms its hardware address, sensor auto-detection results, and accepts any software address remapping.

After completion, the master knows the exact network topology — how many nodes, in what order, with what addresses and sensor configurations.

### 3. Switch to Hardware Clock Sync

The master sends a **broadcast synchronisation packet** on the data pair. All nodes receive the packet and execute the switching sequence described above. The master begins transmitting the clock signal on pair 2, node PLLs synchronise.

After stabilisation, the master sends a broadcast packet to initiate synchronised sampling.

### 4. Synchronous Sampling

The broadcast packet from the master triggers sensor acquisition on **all nodes simultaneously** — at the same moment, on the same clock. Each node stores the measured data in its local buffer.

Sampling runs at **125 Hz** — comfortably above the ~20 Hz seismic band, yet low enough that the core can sleep between samples. The ADXL355 and ICM-42688 free-run **phase-locked** to the master clock (their EXT_CLK / CLKIN inputs are driven from the synchronised node clock), so the sample index alone fixes each sample in network-wide time. The SCL3300 inclinometer has no external clock — it free-runs and is polled slowly in the background.

While sensors are collecting data, the bus is silent — no TX, no current through termination resistors. The node detects silence on the bus and switches the MCU to low-power mode. It is woken by a timer (measurement time / ready pulse from sensor) or bus activity from the master.

### 5. Token Ring Operation

After the acquisition period, the network switches to **token ring** mode. The master passes the token to the first node, which sends data from its buffer and passes the token to the next. All data carries the same timestamp from the moment of synchronous sampling — even though they are transmitted sequentially.

---

## Clock Signal Integrity

The master carries a single **8.192 MHz crystal** — the only oscillator in the network. The value is deliberate: 8.192 = 8 × 1.024 MHz, so every clock a node needs is an integer division of it (ADXL355 EXT_CLK 1.024 MHz, ICM-42688 CLKIN ≈ 40.96 kHz, all generated by the STM32 timers off the synchronised HSE). Swapping this one crystal retunes the whole network.

The clock travels on pair 2; the THVD1450 transceiver **regenerates the edges** at the far end, so it arrives as a clean digital square wave and feeds the STM32H503 HSE input directly.

If a long cable ever skews the duty cycle outside the PLL's input window, a single **toggle flip-flop** can be dropped onto the incoming clock — reacting only to the rising edge it outputs exactly 50/50 regardless of input duty (and halves 8.192 → 4.096 MHz, still a valid HSE). This is a field-test fallback, not a default part.

---

## Watchdog — Fault Recovery

Each node runs a watchdog with a **1 second** timeout. If the clock signal from the master disappears (cable failure, master restart), the watchdog after 1 second:

```
1. Remaps HSE_IN pin back to UART2 RX
2. Re-initialises UART2
3. Switches MCU to internal RC oscillator
4. Returns to initialisation state
5. Waits for new broadcast from master
```

Detection has two layers. The STM32 **Clock Security System (CSS)** catches HSE loss in hardware almost immediately — it fires an NMI and auto-switches the MCU to its internal RC — which triggers the orderly recovery above without waiting for a timeout. The **1 second IWDG** is the backstop: if anything genuinely hangs, it resets the MCU, which reboots on the RC oscillator and re-listens.

A node never locks up — it always recovers on its own.

### How the master finds out

A recovered node is back on its own RC oscillator, so the data pair still works (the UART needs only a clock, not *the* master clock) — **but it must never transmit on its own**, or it would collide with whichever node currently holds the token. So it reports only **in the slot the master grants it**: when the master next hands it the token, instead of a data frame it returns a control frame `ERROR(clock lost)` with its own address. The bus stays fully arbitrated by the master — no collision. If the node is so far gone it does not answer the token at all, the master catches it by the **token timeout**. Either way the master marks the node desynced and re-issues the sync sequence to bring it back into the phase-locked ring.

The pattern tells the cause: **one** node dropping is a cable fault to that node; **all** nodes dropping at once means the master's own 8.192 MHz oscillator died.

---

## Reliability & Diagnostics

Beyond clock recovery, the master keeps the network honest and visible — all of it riding the same two frames, all node→master traffic confined to the node's own token slot (no collisions):

- **Command acknowledgement.** Critical commands — calibrate, address assignment, range change — are not fire-and-forget. After applying one, the node confirms with an `ACK` in its next token slot; the master re-sends if no ACK arrives before its timeout. On a buried node you *know* the calibration took — you don't hope.
- **Missing-sample detection & resend.** The master tracks each node's sample index (re-based to 0 at each per-second tick) and records any gap into the NIC-MLA archive — so "no data" is never silently mistaken for "the ground was quiet". On a CRC miss the master can also ask the node to **resend** its buffered last frame (`RESEND_DATA`), answered in-slot like an extra token.
- **Link health.** Each node keeps a rolling count of the RS-485 echo/CRC errors it sees and reports it on a `HEALTH` query. A slowly rising count is early warning of a cable degrading years before it fails.
- **Version on demand.** The node runs one fixed firmware for its whole life, so there is no version field in the data — but the master can **ask** (`VERSION` query) and the node answers, for diagnostics or a mixed-batch sanity check.
- **Failure alert.** When a node goes silent (token timeout) or reports a fault, the master raises an immediate alert over its **LoRa / Wi-Fi** channel — the operator hears "node 5 is down" at once, instead of finding a hole in the data weeks later.

---

## Power Line Monitoring

The node can continuously monitor the 24 V power line state via a simple resistor divider connected to an ADC pin of the STM32H503:

```
24V input ──▶ R1 ──┬──▶ ADC pin MCU (max 3.3 V)
                   │
                  R2
                   │
                  GND
```

Divider ratio: R1/R2 = 6.3 — for example R1 = 620 kΩ, R2 = 100 kΩ.

By periodically reading the ADC, the node detects:

- **Line OK** — voltage within expected range
- **Undervoltage** — cable under load or weak source
- **Line dead** — source failure or cable break

Line state is included in the status packet sent via the LoRa alert channel.

---

## Data Flow — Two Frames

Two lean, purpose-built frames share the data pair, told apart by a leading magic byte and the bus idle line. Both close with a **CRC-16-CCITT** (the STM32 hardware CRC unit is configured once and shared). See `firmware/DATAFLOW.md` for the exact byte layout.

**Data frame** — node → master, one per sample:

```
[MAGIC_D][1B station][2B sub-second index][ sensor data — fixed length ][2B CRC16]
```

The payload has **no length field**: its length is fixed per node (its sensor schema) and agreed with the master during initialisation; absent sensors pad zeros. Every axis is **16 bits** — the sensors' effective resolution is ~14–15 bit, so 16 carries all the signal with headroom. The slow fields — the SCL3300 tilt and a **per-sensor die temperature** (one byte per present sensor, each die drifting on its own) — ride in the same payload, carrying their latest value; the redundancy is squeezed to nothing at storage, so the node never bothers suppressing repeats. A fully-populated node is **6 B frame overhead + 27 B payload = 33 B** (see `firmware/DATAFLOW.md` for the byte-by-byte layout).

**Control frame** — master ↔ node, on demand:

```
[MAGIC_C][1B station][1B attribute][1B value][2B CRC16]      (6 bytes)
```

`attribute` is the command (assign address, set a sensor's range/sensitivity, calibrate, pass token, report status…) — range/sensitivity is the only runtime-tunable setting, one per sensor (ADXL355, ICM accel, ICM gyro, SCL3300 mode); `value` is its argument. The same frame serves both directions. Changing a setting at runtime is just a control frame — the master then closes the current NIC-MLA table and opens a new one stamped with the new settings.

The 2-byte index is **not** an absolute time — it is the sub-second / sample index within the synchronised acquisition window (all nodes share one phase-locked clock, so the index alone pins the sample in time). The absolute second comes from the master's RTC.

The index is anchored to that second: **once per second the master broadcasts a `TICK` that resets every node's index to 0**, aligned to the RTC second boundary. So the index always means "sample number within the current second" (→ NIC-MLA `subsec`), and a node that re-joins mid-stream — after a clock-loss recovery — simply falls back into lock-step on the next tick, with no global re-sync and no disruption to the healthy nodes.

The master receives the packet, automatically checks ID and CRC, and writes to the NIC-MLA archive according to the schema and sensor configuration of the given node. It maps the two time parts straight onto NIC-MLA's v1.1 log record: the RTC whole second → `timestamp` (u32 Unix seconds), the node's 2-byte sample index → `subsec` (u16). That `subsec` field is exactly what NIC-MLA v1.1 added so the container handles sampling far above 1 Hz — Quake is its driver. Nodes are dumb, the master is smart.

---

## Data Stack

| Layer | Library | Role |
|---|---|---|
| Compression | [NIC-DMD](https://github.com/Project-NIC/NIC-DMD) | Adaptive compression, no lookup tables |
| Storage | [NIC-MLA](https://github.com/Project-NIC/NIC-MLA) | Single-file container, crash-safe |
| Transport encryption | [NIC-KSF](https://github.com/Project-NIC/NIC-KSF) | SPECK-128 CTR, in-place, no malloc |
| Write glue | [NIC-GLUE-IN](https://github.com/Project-NIC/NIC-GLUE-IN) | Data → DMD → MLA |
| Read/export | [NIC-GLUE-OUT](https://github.com/Project-NIC/NIC-GLUE-OUT) | MLA → CSV / SQLite |
| Desktop viewer | [NIC-VDE](https://github.com/Project-NIC/NIC-VDE) | MLA as directory, export CSV/SQL |

### Alternative Data Formats

- **MiniSEED with Steim1/2** — seismological community standard, compatible with ObsPy, SeisComp, SWARM. Record length is a power of two; Steim-1/2 lossless integer compression. The NIC-MLA archive is converted to miniSEED by [NIC-MSEED](https://github.com/Project-NIC/NIC-MSEED) (`.mla → miniSEED`; it decompresses NIC-DMD on the way and maps the MLA `timestamp`+`subsec`, SCHEMA channels and STATION codes onto the SEED headers). Whoever needs SEED uses it; whoever doesn't, doesn't.
- **Raw delta stream** — minimal overhead, custom protocol, for fully custom pipelines.

---

## Calibration

After completing initialisation and initial setup after start:

1. Wait for thermal stabilisation (minimum 30 minutes)
2. Send calibration command from master
3. Node averages the current gravity vector (from the precision ADXL355) as its reference
4. The rotation matrix is built **directly from that gravity vector** with plain vector algebra — normalise gravity → the vertical Z axis (toward Earth's core), then two cross products give the horizontal X/Y axes. No Euler angles, no trigonometry, no gyro integration.
5. The matrix is stored in flash and applied on the node to **every** sensor's data — accelerometer, inclinometer and gyroscope alike. Angular velocity is a vector too, so the same matrix rotates it; the gyro inherits its alignment from the shared board orientation, since at rest it has no gravity reference of its own.

Because of this virtual levelling, no precise mechanical levelling is needed at install — seating the node with a **bubble level** (±1–2°) is plenty; the matrix corrects the small residual. Recalibration is needed only if the node physically moved.

*For those interested in more advanced rotation mathematics: the transformation matrix can be replaced by quaternions or a rotation vector (axis + angle). For a sensor permanently buried in rock that does not move after installation, a transformation matrix computed once at calibration is entirely sufficient.*

---

## License

MIT License — Copyright (c) 2026 NIC — Native Intellect Community

---

## Acknowledgments

Brother for advice during the development of this project.
For technical assistance with code optimisation, to AI assistants Claude (Anthropic) and Gemini (Google).

★ Viva La Resistánce ★
