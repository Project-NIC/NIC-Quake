# NIC HW Block — Protection: RS-485 port

*Reusable hardware block on **every RS-485 port**. The NIC bus carries **two** 485 pairs —
the **data** pair and the **clock** pair — and **both** get this protection (same tiers,
same parts). The **Modbus** meteo bus is the only single-bus case — no clock pair — see its
own section. **Tiered**: pick the level for the run; one footprint set covers all. A
recommendation, not a mandate. **Not** on bought, sealed COTS sensors.*

## The parts

- **SM712** (CDSOT23-SM712) — dual asymmetric TVS (−7 / +12 V, 400 W, ~17 A 8/20 µs, 75 pF,
  transparent at bus speeds), right at the transceiver A/B. The fast **inner clamp**.
- **10 Ω series** per branch — dual purpose: it is the bus termination's series R *and* the
  surge-coordination resistor. Always present.
- **80 Ω shunt** across A–B at the two **END** nodes only → 10 + 80 + 10 = **100 Ω** (Cat 6).
  Middle nodes: series R only, no shunt.
- **GDT 60 V** (long runs) — slow, high-energy **crowbar** at the cable entry.
- **ISO transceiver** (ISO1450, drop-in for the THVD1450) **+ an isolated DC-DC** — a
  galvanic barrier for a truly remote / different-ground link. Needs its own field-side
  ground, kept off the internal (−) — see *Grounding*.

## Tiers — pick per run

| Variant | 10 Ω series | 80 Ω term | SM712 | GDT 60 V | Isolation | Clamp ground |
|---|:--:|:--:|:--:|:--:|:--:|---|
| **END / SHORT**    | ✓ | ✓ | ✓ | — | — | internal (−), 1-pt earth |
| **MIDDLE / SHORT** | ✓ | — | ✓ | — | — | internal (−) |
| **END / LONG**     | ✓ | ✓ | ✓ | ✓ | — | internal (−), 1-pt earth |
| **MIDDLE / LONG**  | ✓ | — | ✓ | ✓ | — | internal (−) |
| **LONG / ISO**     | ✓ | ✓¹ | ✓ | ✓ | ISO1450 + iso DC-DC | **earth only — NOT internal (−)** |
| **Modbus master**  | 120 Ω term | | ✓ | — | — | internal (−) |
| **Modbus sensor**  | — | — | ✓ | — | — | internal (−) |

¹ 80 Ω at end nodes only, as with the others.

## Surge path (outermost → transceiver)

```
  cable ──► [ GDT 60V ]* ──► [ 10Ω series ] ──► [ SM712 ] ──► THVD1450 A/B
            crowbar           coordinates        fast clamp     (+ integrated
            high energy,      + termination      ~24V            ±18kV ESD)
            slow (*LONG)      series R

  END node:    80 Ω across A–B after the series R   (10 + 80 + 10 = 100 Ω)
  MIDDLE node: no 80 Ω
```

## How it survives a strike

A *nearby* strike (not a direct hit) induces a short, fast impulse into the cable; its
inductance/capacitance shape the edge. The THVD1450 tolerates more than the SM712, so the
**SM712 fires first** and is the clamp.

- **SHORT (SM712 + 10 Ω).** The series 10 Ω per branch (20 Ω round-trip) is the limiter. At
  the SM712's ~17 A peak it drops 17 A × 20 Ω = **340 V**, so the pair rides out a ~360 V
  peak (340 V on the resistors + ~24 V the SM712 clamps to the transceiver) on the resistors
  alone. Plenty for an in-station run.
- **LONG (+ GDT 60 V).** As the SM712 current rises, the voltage at the cable entry climbs;
  at **~4 A** through the SM712 it reaches the GDT's 60 V (≈ 24 V clamp + 4 A × 10 Ω), the
  GDT strikes and **crowbars** — the voltage collapses to its arc value and the GDT carries
  the bulk lightning current (kA) for as long as it flows, sparing the SM712. So the SM712
  never sees more than ~4 A, not 17.
- **ISO.** For a remote link across different grounds, the ISO1450 + an isolated DC-DC add a
  galvanic barrier, so a ground-potential difference can't drive current through the node at
  all — **provided the clamp ground stays off the internal (−)**; see below.

## Grounding — the LONG vs ISO difference (important)

Where the SM712 / GDT clamp their ground is what separates the two distant tiers, and
getting it wrong defeats the isolated one:

- **Non-isolated (SHORT / LONG):** the clamp ground **is** the node's internal (−). The
  surge is shunted to internal ground; bond that to **earth at a single point** (one node,
  typically the master / an end) so the diverted energy has a way out. Single-point, so it
  doesn't become a ground loop.
- **Isolated (LONG / ISO):** the clamp ground is the **field / cable / earth** side and must
  **NOT** touch the internal (−). The ISO1450 + an isolated DC-DC are the barrier; the field
  side (clamps + their earth) swallows the surge **and** any ground-potential difference,
  while the internal electronics float behind the barrier, untouched. **Join the two grounds
  and you short the barrier — the isolation is gone.**

```
  field / earth side            ‖ barrier ‖             internal side
  ────────────────────────────────────────────────────────────────────
  cable ─[GDT]─[10Ω]─ A/B ──►  ISO1450 bus  ═‖═  ISO1450 logic ──► MCU / UART
                  SM712 ──┐          ▲
                          │    isolated DC-DC feeds the bus side
                      EARTH ⏚                              internal (−)
  ────────────────────────────────────────────────────────────────────
  EARTH  and  internal (−)  stay SEPARATE — never joined.
```

## Termination split: 80 + 10, not 90 + 5

Both make 100 Ω (10 + 80 + 10 vs 5 + 90 + 5). 90 + 5 passes a touch more signal to the
receiver (across the shunt, 90 % vs 80 % ≈ 1 dB), but at 50 m on Cat 6 the RS-485 margin is
enormous — that dB is irrelevant. The larger **10 Ω series wins on protection**: it roughly
**halves the SM712 current before the GDT fires** (~4 A vs ~7 A) and limits more current to
the transceiver. So **80 + 10**.

The same network and tiers apply to the **clock bus** (`bus-485-clock`): at 8 MHz the
THVD1450 regenerates the edges, and the D6 ÷2 fallback covers any extreme-length run.

## Modbus port (meteo bus)

Protect the **master / processor side** (your STM32), not the bought sensors: 120 Ω
termination + SM712 at the master. The **sensor end gets a bare SM712, no series R** — you
don't open a sealed COTS sensor, and the SM712 clamps an induced surge to ground before the
sensor IC sees it. Minimal on purpose: you can't harden parts you didn't build.
