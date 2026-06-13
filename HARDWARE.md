<p align="center">
  <img src="NIC-Quake.svg" width="200"/>
</p>

★ N.I.C. ★

# NIC-QUAKE — Hardware

## Component Selection and Reference Design

[![License: CERN-OHL-S v2](https://img.shields.io/badge/License-CERN--OHL--S%20v2-red.svg)](https://ohwr.org/cern_ohl_s_v2.txt)

---

This document describes the reference component selection with rationale. This is not a mandatory bill of materials — it is a starting point. Each component is described by the parameters that matter for this application, not just by brand name.

---

## Node MCU — STM32H503

**ARM Cortex-M33, up to 250 MHz, 128 KB Flash, 32 KB SRAM**

Chosen for the combination of performance, peripherals, and price. Key features for QUAKE:

- DSP instructions and FPU — rotation matrices, quaternions, compression run in hardware
- Single-cycle hardware multiplier — critical for NIC-DMD compression in real-time
- Barrel shifter — bit operations for compression algorithm without software loops
- 3× SPI, I2C/I3C, 3× UART — enough peripherals for all sensors and communication
- GPIO remapping at runtime — required for switching UART RX pin to HSE clock input during synchronisation
- Packages: UFQFPN32, LQFP48, LQFP64 — choose based on PCB space

---

## RS-485 Transceiver — THVD1450 (Texas Instruments)

**2 units per node — one for the data pair, one for the clock pair**

- Supply 3–5.5 V
- Data rate 500 kbps or 50 Mbps (selectable)
- **Integrated ±18 kV IEC ESD protection** — eliminates need for external TVS diodes on bus pins
- IEC EFT (electrical fast transient) immunity — no external protection components needed
- Common-mode range ±15 V — no issues on long cable runs
- Temperature range −40 to +125 °C
- Packages: SOIC-8, VSON-8, VSSOP-8

*Note: THVD1450 regenerates signal edges — 4–8 MHz clock distribution passes through even long cables without issue.*

---

## 6-Axis IMU — ICM-42688-P (TDK InvenSense)

**Primary motion sensor of the node**

Chosen for the lowest gyroscope noise density in its class and as the only device in its price range with a hardware external clock sync input.

| Parameter | Value |
|---|---|
| Gyroscope — noise | 2.8 mdps/√Hz |
| Gyroscope — range | ±15.6 to ±2000 dps (8 steps) |
| Gyroscope — drift vs temperature | ±5 mdps/°C |
| Accelerometer — noise | 65–70 µg/√Hz |
| Accelerometer — range | ±2 / 4 / 8 / 16 g |
| ODR | up to 32 kHz |
| **External clock sync** | **31–50 kHz input — hardware node synchronisation** |
| Interface | SPI 24 MHz / I²C / I³C |
| FIFO | 2 KB |
| Supply | 1.71–3.6 V, 0.88 mA (6-axis HP mode) |
| Package | LGA-14, 2.5 × 3.0 × 0.91 mm |

*Why not ISM330DHCX (ST): better bias stability and temperature range, but ODR only 6.7 kHz (vs 32 kHz), no external clock sync, Machine Learning Core adds latency unacceptable for real-time seismic recording.*

---

## Precision Accelerometer — ADXL355 (Analog Devices)

**Primary seismic sensor**

| Parameter | Value |
|---|---|
| Noise density | 25 µg/√Hz |
| Range | ±2 / ±4 / ±8 g |
| Package | **LCC-14, 6.0 × 6.0 × 2.1 mm, hermetically sealed** |
| Output | Digital SPI / I²C |
| Temperature stability | Minimal zero-point drift |
| Cross-axis error | 1% |

The hermetic LCC package is critical — it guarantees long-term stability in rock installation conditions. Analogue variant ADXL354 (same package and footprint) for applications requiring analogue output.

*Comparison: ADXL345 (commonly used in hobby projects) has noise density of 150 µg/√Hz — 6× worse. The difference cannot be compensated in software.*

---

## Inclinometer — SCL3300-D01 (Murata)

**Long-term drift reference and recalibration baseline**

Dedicated 3-axis MEMS inclinometer, specifically designed for industrial inclination measurement and structural monitoring. Unlike general-purpose accelerometers, the SCL3300-D01 integrates on-chip signal processing that outputs inclination angle directly with long-term stability as a primary specification.

| Parameter | Value |
|---|---|
| Measurement modes | Mode 1: ±1.2 g (±90° range) / Mode 2: ±2.4 g (±90° range) / Mode 3 & 4: ±10° range (high-resolution, near-level) |
| Noise density | 0.6 mg/√Hz (Mode 1, 10 Hz bandwidth) |
| Long-term stability | < 0.05° over operational lifetime |
| Output | Digital SPI (16 MHz) — angle + acceleration + temperature |
| Supply | 3.0 – 3.6 V |
| Temperature range | −40 to +85 °C |
| Package | LGA-12, 8.6 × 7.6 × 3.3 mm |

During large seismic events (where the inclinometer would saturate), its output is excluded from the calculation — it serves exclusively for slow drift monitoring and recalibration under quiet conditions. The integrated temperature output enables thermal drift compensation.

*Note: Package footprint differs from ADXL355 — separate PCB area required.*

---

## Ultra-Low-Noise LDO — LT3042 (Analog Devices)

**3 units — powering analogue sensor sections**

Any noise on the analogue sensor power rail directly appears in the measurement. This is the most important component for signal quality.

| Parameter | Value |
|---|---|
| Output noise | **0.8 µVRMS (10 Hz – 100 kHz)** |
| PSRR | 79 dB @ 1 MHz |
| Output current | 200 mA (units can be paralleled for higher current and lower noise) |
| Dropout | 350 mV typical |
| Input range | 1.8 – 20 V |
| Output range | 0 – 15 V (programmable with single resistor) |
| Packages | MSOP-10, DFN 3×3 mm |

*Note: LT3045 is the 500 mA version of the same architecture for higher-current applications.*

---

## Standard LDO — MCP1700 (Microchip)

**5 units — powering digital sections**

Each digital circuit (MCU, RS-485 transceivers, other peripherals) has its own dedicated LDO. Current spikes from the RS-485 bus do not propagate into the shared supply — cleaner and cheaper than filters.

| Parameter | Value |
|---|---|
| Output current | 250 mA |
| Dropout | 178 mV @ 250 mA |
| Quiescent current | Very low — suitable for battery applications |
| Package | SOT-23, TO-92 |

---

## Step-Down Module — Recom R-78HE5.0-0.3

**1 unit — input DC-DC conversion**

Integrated module (SIP-3 package, drop-in replacement for 78xx linear regulator), no external components except input capacitor.

| Parameter | Value |
|---|---|
| Input range | **6.5 – 72 V** |
| Output | 5 V / 300 mA |
| Spike tolerance | 100 V — no external protection components needed |
| Efficiency | up to 83% |
| Temperature range | −40 to +105 °C |
| MTBF | 15,000,000 hours |
| Package | SIP-3, 8.5 × 11.5 × 12.5 mm |

The 5 V output feeds the LDO regulators — the switcher is physically and electrically separated from sensitive analogue sections. Switching noise is filtered by LDO PSRR.

---

## Passive Components

### Capacitors — signal path and power bypass

For all small blocking capacitors in analogue and signal paths exclusively **NP0 / C0G**. Capacitance does not change with temperature or voltage — critical for measurement accuracy.

### Note on X7R vs X8R

X7R (−55 to +125 °C, ±15%) is sufficient for this application — operating temperature of a node in rock is a stable 10–12 °C. X8R (up to +150 °C) only makes sense for installations in extremely hot environments. Due to voltage drift, use capacitors rated for more than double the operating voltage. Hybrid polymer electrolytic capacitors may be used in combination with NP0/C0G; for input bulk, use minimum 100 V rating with at least 105 °C temperature rating.

---

## Cable and Connectors

**Cable:** UTP Cat 6 (outdoor version with UV-stable jacket). Rated for ~100 V isolation. Four pairs — RS-485 data bus, RS-485 clock bus, and two pairs for 24 V power.

**Node connectors:** 2× M12, IP68, 4–5 pins (power + both RS-485 pairs). Daisy-chain — one input, one output. Termination resistors at line ends can be integrated into a blank M12 plug — clean solution without extra components on the board.

---

## Optional: LoRa Module

For alert channel and remote diagnostics. Key selection parameters:

- Sub-GHz band (868 MHz in Europe) for penetration through rock and soil
- Sleep mode current below 1 µA

Specific type depends on required range and availability at target location.

---

## Optional: Input Protection Module

For longer cable runs or areas with thunderstorm risk, an input protection module is recommended — a small board inserted between the cable and the main node board.

**Power line (24 V):**
```
Cable → Fuse → Varistor V20E30P → TVS 5KP30CA → Connector
```

**RS-485 data pairs:**
```
Cable → Series resistors (impedance matching) → CDSOT23-SM712 → Connector
```

The CDSOT23-SM712 (Bourns, SOT-23) is a dual TVS array specifically designed for RS-485 port protection — ±30 kV ESD, 400 W/17 A pulse capability, 75 pF junction capacitance. Transparent at 500 kbps.

**Series resistors — values and rationale**

Each RS-485 conductor (A and B) carries a dedicated **10 Ω series resistor** between the cable connector and the THVD1450 pins. Two separate resistors — one per conductor rather than a single resistor in the common path — preserve differential symmetry and provide balanced protection against ground faults and transients on either line independently. The series resistors also limit peak current through the TVS clamps during transient events.

At line termination nodes (physical ends of the daisy-chain), an **80 Ω resistor** is placed across the A/B pins of the transceiver.

Impedance seen from the cable:

```
10 Ω (conductor A) + 80 Ω (shunt) + 10 Ω (conductor B) = 100 Ω
```

This matches the 100 Ω characteristic impedance of UTP Cat 6 exactly — correct termination, no reflections.

Impedance seen by the THVD1450 at its A/B pins:

```
80 Ω (local shunt) ∥ (100 Ω cable + 20 Ω series) = 80 ∥ 120 = 48 Ω
```

Middle nodes (not at line ends) carry only the 10 Ω series resistors — no shunt termination.

---

## Power Architecture Overview

```
Input (e.g. 12–48 V)
    │
    ▼
R-78HE (step-down 5 V)
    │
    ├──▶ LDO #1 (MCP1700) ──▶ STM32H503
    ├──▶ LDO #2 (MCP1700) ──▶ THVD1450 #1 (data pair) + THVD1450 #2 (clock pair)
    ├──▶ LDO #3 (MCP1700) ──▶ ICM-42688-P (IMU) — digital
    ├──▶ LDO #4 (MCP1700) ──▶ ADXL355 (accelerometer) — digital
    ├──▶ LDO #5 (MCP1700) ──▶ SCL3300-D01 (inclinometer) — digital
    ├──▶ LDO #6 (LT3042)  ──▶ ICM-42688-P (IMU) — analogue
    ├──▶ LDO #7 (LT3042)  ──▶ ADXL355 (accelerometer) — analogue
    └──▶ LDO #8 (LT3042)  ──▶ SCL3300-D01 (inclinometer) — analogue
```

*Each analogue sensor has its own LT3042. Both RS-485 transceivers share one MCP1700 — noise on the supply is common-mode and rejected by the differential RS-485 receiver. Current spikes from one circuit do not propagate into others.*

*(The tree above is the **node's** power. The master is mains/USB-powered — see below.)*

---

## Master Unit

The master is the head end of one RS-485 segment: it distributes the clock, runs the token-ring orchestration, timestamps the data, and stores or uplinks it. Unlike a node it is **not** buried — it lives in an enclosure with power, so its design is far simpler.

### Brain — ESP32 (or a clone module)

**Dual-core ~240 MHz, Wi-Fi + Bluetooth, UART — runs the `nq-master` orchestration plus storage/uplink.**

- Wi-Fi / BT on-chip → cloud upload, NTP time, and remote configuration with no extra parts
- UART drives the RS-485 data pair; ample RAM/flash for the NIC-MLA archive
- Cheap and ubiquitous; clones are fine

*Any MCU works — the firmware core is portable C and only its thin board layer is processor-specific. The ESP32 is simply the easiest path (Wi-Fi + UART built in). Add a board with RS-485 and the clock oscillator and that's it.*

### Network clock — 8.192 MHz oscillator

**The single timebase the whole network is phase-locked to.**

- A **dedicated 8.192 MHz canned crystal oscillator** generates the distributed clock. **Do not** derive it from the ESP32's own 40 MHz crystal — that one carries the Wi-Fi/RF and must not be touched. Use a separate oscillator (the "external crystal").
- 8.192 = 8 × 1.024 MHz, so every node clock (ADXL355 EXT_CLK 1.024 MHz, ICM-42688 CLKIN ≈ 40.96 kHz, the STM32 HSE) is an **integer division** of it. Swap this one part and the whole network retunes.
- Gated by an ESP32 GPIO and fed to the clock-pair transceiver. **Low jitter matters** — the network's timing rides on this part.

| Parameter | Value |
|---|---|
| Frequency | 8.192 MHz |
| Type | canned crystal oscillator (CMOS output); TCXO for temperature stability |
| Supply | 3.3 V |
| Role | the only oscillator that defines network time |

### RS-485 — two pairs (same THVD1450 as the node)

| Transceiver | Pair | Direction | Driven by |
|---|---|---|---|
| THVD1450 #1 | data pair | bidirectional | ESP32 UART (TX/RX + DE) |
| THVD1450 #2 | clock pair | master → nodes | the 8.192 MHz oscillator |

- The data-pair **DE** (driver-enable) is controlled from a GPIO (or the UART's RS-485 mode) — it releases the bus for node replies during the token ring.
- The clock-pair transceiver buffers the oscillator onto the differential bus; the THVD1450 regenerates clean edges over long cable.
- **Termination** resistors at the master end (it is one end of the bus).

### Time, storage, power

- **RTC / absolute second:** NTP over Wi-Fi, or an external RTC (e.g. DS3231) for offline accuracy. The master maps `RTC second + node sample index` → NIC-MLA `timestamp` + `subsec`.
- **Storage / uplink:** microSD (SPI/SDMMC) for the local NIC-MLA archive and/or streaming over Wi-Fi to a server.
- **Power:** mains/USB-powered (not buried), so no ultra-low-noise constraints. The master — or a co-located PSU — injects the **24 V** onto cable pairs 3+4 to power the nodes.

---

## License

Hardware: CERN-OHL-S v2 — Copyright (c) 2026 NIC — Native Intellect Community

Software: MIT License — Copyright (c) 2026 NIC — Native Intellect Community

---

## Acknowledgments

Brother for advice during the development of this project.
For technical assistance with code optimisation, to AI assistants Claude (Anthropic) and Gemini (Google).

★ Viva La Resistánce ★
