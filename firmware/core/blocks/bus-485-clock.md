# NIC HW Block — Bus: NIC 485 (data) + clock

*Reusable hardware block. On every node + the master. UTP Cat 6, daisy-chain, M12 IP68.*

- **Data pair** — THVD1450, UART, bidirectional (half-duplex; self-timed slots).
- **Clock pair** — THVD1450, driven from the master's 8.192 MHz oscillator; on the
  node the clock-pair RX is remapped to **HSE_IN** (the clock-pin switch). Clock-only.
- THVD1450 is selectable **500 kbps / 50 Mbps** and regenerates edges → the ~8 MHz
  clock passes even long cable.
- **Termination: 10 Ω series on both branches (A + B) at every node**, plus an
  **80 Ω shunt at the two ends — the master and the last peripheral**
  (10 + 80 + 10 = 100 Ω, matches Cat 6). Middle nodes: 10 Ω series only. The split is
  **80 + 10**, not 90 + 5 — the bigger series R buys surge coordination (it halves the
  SM712 current before the GDT fires) at a signal cost that's negligible on Cat 6. That
  10 Ω doubles as the surge resistor; the tiered TVS / GDT / ISO protection is identical on
  the **data and clock pairs** — see `protection-485.md`.
- **Protection: SM712** at each transceiver (see block `protection-485`).
