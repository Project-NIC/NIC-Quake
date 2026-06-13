<p align="center">
  <img src="NIC-Quake.svg" width="200"/>
</p>

★ N.I.C. ★

# NIC-QUAKE — Data Flow (what runs on the wire)

Two lean, purpose-built frames, each started by its own magic byte and delimited
by the bus idle line. The magic also tells them apart. Consolidates D1–D19.

---

## The two frames

```
┌─ DATA frame (slave → master), 125×/s ───────────────────────────────────┐
│  [MAGIC_D][station][index 2B][ sensors + temp + status (FIXED) ][CRC16]    │
│    0xD5     1B        2B            payload, agreed at init        2B       │
│  • no length field — payload length is fixed per node, set at the init    │
│    handshake; absent sensors just pad zeros                               │
│  • index = 2B sub-second sample index → NIC-MLA `subsec`                  │
│  • last payload byte = D24 status (saturation / supply / temp / fault)    │
└──────────────────────────────────────────────────────────────────────────┘

┌─ CONTROL frame (master ↔ slave), on demand ─────────┐
│  [MAGIC_C][station][attribute][value][CRC16]         │
│    0xC5      1B        1B        1B     2B   = 6 B    │
│  • attribute = what (assign address, set range, …)   │
│  • value     = the argument                          │
│  • same frame both directions (reply sets its own    │
│    station); runtime setting changes ride this       │
└──────────────────────────────────────────────────────┘
```

> `station` = the node's address: its **hardware number** (the number printed on the
> box) at boot / after a hard reset, or its master-assigned **software slot** in
> operation. See `SOFTWARE.md` *Node Addressing* / `DESIGN.md` D23.
>
> *Which* box is the `station` number; *what KIND* of node it is (its **TYPE** —
> seismo / Basic / iono / starDust) is a **separate** field the master learns once at
> discovery (`NIC_OP_NODE_TYPE`, D26), **not** carried in the data frame — the station
> number keys back to it. Payload length does **not** encode type (a full seismo node
> and a Basic weather node are both 28 B).

Both use **CRC-16-CCITT** so the STM32 hardware CRC unit is configured once and
shared. All data is **RAW** — no compression or encryption on the node.

---

## What's in the DATA payload (fully-populated node)

| Field | Source | Range / mode | Bytes |
|---|---|---|--:|
| ADXL355 X/Y/Z | seismic, precise | ±2g (cfg ±4g) | 6 |
| ICM accel X/Y/Z | seismic, extreme | ±8g (cfg ±16g) | 6 |
| ICM gyro X/Y/Z | rotation | ±15.625 dps | 6 |
| SCL3300 X/Y/Z | tilt / drift | Mode 1/4 | 6 |
| Temperature ×N | per-sensor die temp (ADXL/ICM/SCL) | 1 B each | 3 |
| Status (D24) | saturation ×4 / supply-low ×2 / over-temp / sensor-fault | bitfield | 1 |
| **payload** | | | **28** |

The **status byte (D24)** is the fixed last byte — eight cheap per-sample flags the
node computes from values it already holds (so the master never scans the data, and
an offline pass sees saturation at a glance): bits 0–3 saturation of ADXL / ICM-acc /
ICM-gyro / SCL, bit 4 input supply low, bit 5 internal 5 V (switcher) low, bit 6
anything > 50 °C, bit 7 a present sensor went silent. Exact values (voltage, MCU
temp, a live sensor test) are pulled only on a master **query**, never per sample.

Each axis is **16 bits / 2 bytes** (D14). Temperature is **one byte per present
sensor** (each die drifts on its own), appended in the same fixed sensor order.
The slow fields (tilt, temperatures) just carry their latest value, repeated
across frames — the redundancy costs a few bytes on a bus with huge headroom and
is squeezed to nothing at storage (NIC-DMD / Steim). A node with fewer sensors
has a shorter, fixed payload; the master knows the schema from the init handshake.

---

## Sizes, rates, headroom

| Frame | Size | Rate | Bus load |
|---|--:|---|--:|
| DATA (full node) | 4 + 28 + 2 = **34 B** | 125 Hz | ~4.25 kB/s |
| CONTROL | **6 B** | on demand | negligible |

Self-running TDMA has **no per-sample token**, so the DATA stream *is* the bus load
(plus the once-a-second `TICK` and any on-demand query). One full node = 34 B × rate × 8 =
**34 kbps @ 125 Hz**, **68 kbps @ 250 Hz**.

**Keep the bus ≤ 50 % loaded (aim ~30 %).** The free half is not waste — it absorbs the
UART start/stop framing (real ≈ 10 bits/byte), the gap between slots (bus turnaround), the
per-second `TICK`, and any query / `RESEND_DATA`. Utilisation in the THVD1450 **slow
(500 kbps)** mode:

| Nodes | 125 Hz | of 500 kbps | 250 Hz | of 500 kbps |
|--:|--:|--:|--:|--:|
| 1 | 34 kbps | 7 % | 68 kbps | 14 % |
| 2 | 68 | 14 % | 136 | 27 % |
| 3 | 102 | 20 % | 204 | 41 % |
| 4 | 136 | 27 % | 272 | 54 % ⚠ |
| 5 | 170 | 34 % | 340 | 68 % ⚠ |
| 6 | 204 | 41 % | 408 | 82 % ⚠ |
| 7 | 238 | 48 % | 476 | 95 % ⚠ |
| 8 | 272 | **54 % ⚠** | 544 | **>100 % ✗** |

So in slow mode: **125 Hz** stays ~30 % up to **4 nodes** and under 50 % up to **7**;
**250 Hz** stays under 50 % up to **3 nodes**. Past the ⚠ line, switch the THVD1450 to
**fast mode** (up to 50 Mbps) — there even 8 nodes @ 250 Hz (544 kbps) is ~1 %, effectively
unlimited. Slow mode is only for the long-cable / EMC-quiet runs.

---

## Timing model in one line

ADXL355 + ICM run **125 Hz, phase-locked** to the master clock (sample index =
network-wide time). SCL3300 + temperature are **slow, free-running, off-sync** —
read in the background and stuffed (latest value) into each data frame.

Each node transmits in its **own clock-derived slot** (overhear the predecessor, or a
deadline) — self-running once the master says go; no per-sample token.
