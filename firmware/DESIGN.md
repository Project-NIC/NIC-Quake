<p align="center">
  <img src="NIC-Quake.svg" width="200"/>
</p>

★ N.I.C. ★

# NIC-Quake — Seismograph Front Design Decisions

The seismo-specific half of the decision log: the sensor lineup, the 125 Hz
clock-tuned sampling, on-node levelling, the seismic wire payload and the ranges.
These are what make a NIC node a *Quake* node.

The universal platform decisions it builds on (architecture, NodeBus, clock,
protocols, addressing, TDMA, timing, uplink, node type, config slots) live in
`core/DESIGN.md` — original D-numbers kept so cross-references resolve.

---

## D2 — One sensor per SPI, no bus sharing

- ICM-42688-P → SPI1
- ADXL355 → SPI2
- SCL3300 → SPI3

No chip-select multiplexing, all three readable in parallel. Each sensor's
identity is fixed by its dedicated bus — the probe only has to confirm the
expected part answered there.

## D3 — Free-running sampling (not per-sample triggered)

Sensors are configured once at boot and then run continuously on their own
phase-locked external clocks. The MCU reads each on its DRDY line. No per-sample
SYNC pulsing. Fire-and-forget; intended to run for years without a restart.

- **ICM-42688:** CLKIN ≈ 40.96 kHz from the node's synchronized timebase (see D4).
- **ADXL355:** EXT_CLK 1.024 MHz from the same timebase; EXT_SYNC = internal.

Settings may still be changed by the MCU at runtime — the driver keeps its
`init`/config API — but no human and no host interaction is required in
operation.

## D4 — Common ODR = 125 Hz, set by clock tuning (not the nominal grid)

Because the sensors run on external clocks, the real ODR = clock / divider — so
the achievable rate is continuous, not limited to the nominal grid. The grids
(ADXL: …500, 250, 125… ; ICM: …500, 200, 100…) only hold at the nominal clock,
and they share no value below 500 Hz. Instead of settling for 500 Hz, we tune
the clock so both land on a common low rate.

Target: **125 Hz on all three sensors.**

- **ADXL355:** EXT_CLK at its nominal 1.024 MHz, ODR register = 125 Hz
  (divider 8192; LPF corner 31.25 Hz). Fully in spec — the Analog Devices parts
  are *not* moved off their nominal clock; they are finicky there and ADI does
  not guarantee specs off 1.024 MHz.
- **ICM-42688:** CLKIN tuned to ≈40.96 kHz (1.25 × 32.768 kHz) so its 100 Hz
  setting lands at 125 Hz. Within the ICM's designed 31–50 kHz CLKIN range — it
  is built to be tuned, so it does the bending. (Exact CLKIN scaling factor to
  confirm from the datasheet when the driver lands.)

Rationale: 125 Hz is 6× above the ~20 Hz seismic band (no aliasing), ~4× less
bus traffic and TX power than 500 Hz, and the tighter sensor LPF lowers noise —
all three goals at once.

Supersedes the earlier "500 Hz + in-node decimation" idea: sampling directly at
125 Hz means the node sends **raw 1:1** (1 sample = 1 packet), no averaging
math. The node stays truly dumb.

## D5 — Timing: phase from a common start, time from the sample index

In free running, each sensor latches its sampling phase at start-up. Therefore:

1. Nodes start sampling **only after the HSE clock is locked** and on the
   master's broadcast — so every node shares one sampling phase. The 2-byte
   sample index then pins each sample in time network-wide (master RTC supplies
   the absolute second; see SOFTWARE.md "Data Flow").
2. The offset between *different sensor types* on one node (ICM vs ADXL) is a
   fixed constant for the run — characterize once, ignore thereafter. Negligible
   for a <20 Hz band.
3. After a watchdog clock-loss recovery, sensors are **restarted** so the phase
   re-locks. This keeps the node self-healing.

## D8 — Calibration is computed and applied on the slave node

The node does its own levelling. On the master's calibration command (carried by
the control protocol), the node records the current sensor state as the
reference zero, derives the rotation matrix from the gravity vector, and stores
it in flash (`nq-cal`). From then on it applies that matrix to each raw sample
before sending — so the **data protocol carries already-levelled samples**. This
is the one piece of math the node does; the STM32H503 FPU/DSP handles the matrix
multiply at 125 Hz trivially (HARDWARE.md picked the part for exactly this).
"RAW" still holds where it matters: no compression, no encryption on the node —
just the axis transform.

## D11 — All sensors in one orientation; no per-unit alignment calibration

The reference PCB places **every sensor in the same axis orientation** (aligned
by datasheet measurement axes, not just package), so the per-sensor mounting
matrix `M_mount` is the **identity** and a single levelling matrix `R_level`
serves all sensors. This also collapses the gyro pseudovector sign
(det I = +1), so the gyro transforms identically to the accelerometer.

Why this is the right call, not a lossy shortcut:

- The **gyro has no static reference** — at rest it reads zero, so its axis
  orientation cannot be recovered from gravity. The only software alternative is
  spinning each unit on a rate table, absurd for a potted seismic node. So the
  PCB layout *is* the gyro's calibration.
- The residual misalignment is just placement/solder tolerance (~1°), below the
  ADXL355's own ~1% cross-axis spec — not worth per-unit calibration for this
  device class.

Analog/digital noise isolation stays the layout's first priority (HARDWARE.md);
identical axis orientation is achieved within that constraint. `nq-cal` still
keeps an optional per-sensor `M_mount` (default identity) so a differently laid
out board also works.

## D14 — 16 bits per axis on the wire (ENOB)

Measured ENOB at the 125 Hz sampling is ~14-15 bits, and this holds across all
sensor types. 16 bits therefore carries all the real signal with headroom, so
every axis is transmitted as **2 bytes**, uniformly.

- The ADXL355 reads 20-bit, but its low ~4 bits sit below the noise floor at
  125 Hz. The node keeps full 20-bit precision through the levelling math, then
  drops the low 4 bits only for transmission (`drop_bits = 4`,
  `bytes_per_axis = 2`). The MSBs — the signal — are preserved; only sub-ENOB
  noise is discarded.
- The ICM-42688 is natively 16-bit (`drop_bits = 0`, `bytes_per_axis = 2`).
- `nic_node_pack` divides off the low `drop_bits` (toward zero, so it stays well-defined
  for negatives) then **saturates** to the byte width, so a full-scale event clips
  cleanly instead of wrapping.

Result: 2 B/axis. A fully-populated node's four seismic fields (ADXL, ICM accel, ICM
gyro, SCL) = 4 × 3 × 2 = 24 B of axis data; with per-sensor temperature (D22) and the
status byte (D24) the payload is **28 B** (DATAFLOW.md) — tiny on the bus.

## D15 — Inclinometer: Murata SCL3300 as the dedicated drift / tilt reference

The dedicated inclinometer / drift-reference sensor is the **Murata SCL3300**
(SPI3). It is a purpose-built 3-axis inclinometer with far better
offset-over-temperature stability and a direct, factory-linearised angle
output — the right tool both for the levelling/drift reference and for slow
**structural-tilt monitoring** (subsidence / building movement over mine
shafts). A general accelerometer does absolute long-term tilt poorly because its
offset drift corrupts the angle over time.

Final lineup:

| Sensor | Bus | Role | Rate |
|---|---|---|---|
| ICM-42688-P | SPI1 | IMU (accel + gyro) | 125 Hz, phase-locked |
| ADXL355 | SPI2 | precision seismic accel, **±2g** | 125 Hz, phase-locked |
| SCL3300 | SPI3 | inclinometer / drift ref, **mode per install** | slow (housekeeping) |

- The SCL3300 has **no external clock input**, so it is not phase-locked to the
  master — fine, it is a slow drift/tilt reference, not a seismic sensor. This is
  exactly why it cannot replace the ADXL355, which IS clock-disciplined (EXT_CLK)
  and is the synchronized seismic sensor. It is also noisier in raw acceleration
  (~0.6 mg/√Hz vs the ADXL355's 25 µg/√Hz, ~20×), but the decisive differentiator
  is sync: even were the noise equal, no EXT_CLK means it can't be the
  phase-locked seismic sensor — its strength is long-term tilt stability, not the
  seismic stream.
- **Mode is configurable** (`nq_scl3300_init` takes the mode command), because
  the modes trade tilt range against resolution (per the datasheet):
  - Mode 1 (±1.2g) / Mode 2 (±2.4g): **±90°** — any install orientation.
  - Mode 3 / Mode 4: **±10° only** — near-level mounts, lowest noise / best
    resolution.
  So Mode 4 fits **near-level structural-tilt monitoring** (buildings, shafts);
  an arbitrarily-oriented seismic node needs Mode 1/2 to read its gravity vector
  at all. **Default: a fixed, configured mode** — the master knows it from the
  init handshake, so the node never has to report which mode it picked (less
  communication; and industry does not auto-change range). An **auto-select**
  helper (`nq_scl3300_init_auto`) is **retained but off by default**: it boots in
  Mode 1, measures tilt (trig-free: (x²+y²)/(x²+y²+z²) ≤ sin²10°), and switches to
  Mode 4 if within ~10° of level. It runs once at boot, never at runtime (the node
  is immobile, D11). Kept for anyone who wants it — do not remove.
- The node reads raw **ACC** and **temperature** at the slow housekeeping rate;
  the tilt angle is **computed downstream** (atan2 of the vector). The SCL3300 is
  *not* the levelling source for the seismic path (see D16) — it serves
  structural tilt and slow drift.
- Protocol: 32-bit **pipelined** SPI frames (response in the next frame), 16-bit
  data per axis (native fit for D14's 16-bit wire format), CRC-8 (poly 0x1D) per
  frame; WHOAMI = 0xC1. Mode commands CRC-verified: Mode1 0xB400001F,
  Mode2 0xB4000102, Mode3 0xB4000225, Mode4 0xB4000338.

Note: HARDWARE.md corrected from the PDF — Mode 1 ±1.2g/±90°, Mode 2 ±2.4g/±90°,
Mode 3/4 ±10°. (The earlier ±0.5g figures were physically impossible for a
1g-gravity inclinometer.)

## D16 — Sensor ranges, gyro, and graceful seismic-source fallback

- **Seismic + levelling source:** prefer the **ADXL355**; if it is not populated,
  fall back to the **ICM-42688 accelerometer** (noisier, but present).
  `nq_sensors_seismic_source()` encodes this; the chosen sensor feeds both the
  seismic stream and the nq-cal gravity vector.
- **Dual-range / extended dynamic range.** The two accelerometers can carry
  *different* ranges, run simultaneously, and the master stitches them:
  - **ADXL355** at ±2g (default) or ±4g — the precise, in-range seismic record.
  - **ICM accel** at ±8g or **±16g** — clip-free headroom for extreme events and
    mechanical shocks. At those amplitudes the ICM's coarser precision is
    irrelevant (relative error is tiny when the signal is huge), so the master
    uses the ICM only for peaks the ADXL355 clipped.
  Both streams are sent (the packer already emits every present sensor); the
  master picks the ADXL355 while in-range and the ICM for clipped peaks. Config
  bytes: `..._ACCEL_2G/8G/16G_100HZ` (ICM) and `NQ_ADXL355_RANGE_1/2/3` (ADXL355).
  The **levelling vector still comes from the ADXL355** (gravity ~1g, always in
  range).
- **Gyro = ±15.625 dps** — the ICM's most sensitive range; seismic rotation is
  tiny, so maximise resolution.
- Rationale: the acceleration/rotation needed to saturate even these sensitive
  ranges would already have destroyed the building, rock or station — so the most
  sensitive settings are the safe default.
- **Tilt angle is computed (atan2) from the gravity vector downstream**, not read
  from the SCL3300 angle registers — keeps the node dumb.

## D17 — SCL3300 sampling, and sensor ranges as deployment config

- The SCL3300 is **outside the synchronized 125 Hz acquisition**. With no
  external clock it free-runs on its internal oscillator and is already
  band-limited by its mode (10 Hz LPF in Mode 4). The node **polls** it at a slow
  housekeeping cadence — e.g. read ~10 Hz and average to ~1 Hz for a low-noise
  tilt value, or slower. No phase-lock is needed: tilt/drift moves over
  minutes-to-days, so the master RTC second is timestamp enough and there is no
  sub-sample index. Its reads interleave with the DRDY-driven seismic sampling on
  the separate SPI3 bus, so there is no contention with the 125 Hz path.
- **Sensor ranges are per-deployment config, not hard-coded.** The driver inits
  already take them as parameters: ADXL355 `range_code` (±2/4/8g, default ±2g),
  ICM `accel_config0` (±2/8g), gyro fixed at ±15.625 dps. Switching the ADXL355
  to ±4g is just passing `NQ_ADXL355_RANGE_2` in the glue config — one number,
  documented per site. (It can also be changed in the field via the control
  protocol's register-write, with the caveat that it perturbs the free-running
  stream.)

## D18 — Tilt angle on the master; periodic housekeeping, compression at storage

- **Where each transform runs:** the SCL3300 mode threshold is a trig-free
  integer compare on the node at boot (D15); the seismic axis levelling is a
  matrix multiply on the node (D8); the **reported tilt angle (degrees) is
  computed on the master** (atan2 of the raw gravity vector the node sends). The
  node does no trigonometry — it stays dumb.
- **Housekeeping (tilt + temperature) is sent periodically** at the slow rate
  (D17) with **no on-node change detection**. The node sends raw; a handful of
  bytes per second is negligible on the bus, and the redundancy of a near-constant
  tilt is squeezed at **storage** — NIC-MLA's NIC-DMD (or miniSEED's Steim)
  delta-encode constant/slow runs to almost nothing. So repeats need not be
  suppressed on the node.
- Install practice: a **bubble level** (±1–2°) is enough — well inside Mode 4's
  ±10°. The virtual levelling (nq-cal) corrects the small residual; no precision
  mechanical nivelation needed.

## D21 — Runtime-tunable settings: sensor sensitivity only

The one thing the master may change in operation is each sensor's **sensitivity
/ range** — nothing else. The ODR is fixed by the clock sync (D4), the on-wire
bit width by the node (D14), the levelling zero is computed on-node (D8), and the
filters are off. The change rides the **generic config slots** (D28); the seismo
front maps them:

- slot 0 — ADXL355 range ±2 / ±4 / ±8 g
- slot 1 — ICM accel ±2 / ±8 / ±16 g
- slot 2 — ICM gyro range (default the most sensitive, D16)
- slot 3 — SCL3300 Mode 1 (±90°) or Mode 4 (±10°)

The node writes **only the range register**, not a full re-init: the ICM keeps
its ODR field so the 125 Hz phase-lock is undisturbed; the ADXL355 needs a brief
standby to write RANGE — a one- or two-sample perturbation the master absorbs by
rolling to a new storage table (D20). Each change is ACKed in the node's token
slot. The SCL3300 **auto mode-select** (D15) is a boot-time decision only: the
node is immobile, so re-running it at runtime would just re-measure the same tilt
— it is not a runtime value.

## D22 — Per-sensor die temperature; MCU temperature and supply voltage stay off the stream

Each sensor die drifts on its own, so the DATA payload carries **one temperature
byte per present sensor** (ADXL355, ICM-42688, SCL3300) — the covariate the
off-node drift correction actually needs. It is the low byte of the raw temp
count (a coarse covariate, matching the existing convention); a scaled-°C
encoding is a later option if finer thermal data is ever wanted.

What is **not** streamed, on purpose:

- **MCU temperature.** The MCU is the least drift-relevant part of the chain, so
  sending it 125×/s would spend bus for nothing. If ever needed it belongs on a
  slow query, not the sample path.
- **Supply voltage (24 V).** The resistor-divider-to-ADC is a documented hardware
  hook (SOFTWARE.md, Power Line Monitoring), not wired into the default firmware
  — whoever needs it adds it.

---

## Open / deferred

- Driver `init`/`read` register values are now datasheet-grounded (DS-000347
  v1.7 + PX4 defs for ICM; ADXL355 datasheet). The ICM CLKIN/bank routing and
  both INT setups still want **validation on real hardware**.
- Flash storage of the calibration matrices, and the at-rest gravity averaging
  window (30 min thermal stabilization) — live in the glue (`nic-node`), TBD.
- SCL3300 mode: **resolved** — auto-select at start-up (`nq_scl3300_init_auto`,
  D15). Mode still overridable via `nq_scl3300_init(mode_cmd)` if a deployment
  wants to force one.
- `RESEND_DATA` — **implemented**: on a CRC miss the master addresses the node
  with this opcode and the node resends its buffered last DATA frame in-slot
  (collision-free, like an extra token; the frame keeps its original index). The
  truly-lost frame, if the buffer has since advanced, still shows as a gap the
  master already detects. `GET_SETTINGS` (report the node's current ranges)
  remains protocol intent, not yet implemented in the node glue (D22).
- 24 V supply monitoring — documented hardware hook (SOFTWARE.md), not wired into
  the firmware by default.

