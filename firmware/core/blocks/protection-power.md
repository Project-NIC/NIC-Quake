# NIC HW Block — Protection: power line

*Reusable hardware block. The **source / output side** (battery → protected bus rails) is
tiered like the 485 port; one footprint set covers all tiers. The **node-input side** (the
per-node regulators) is the other half — a **concept** at the end of this block. An
**engineering-grade** design, not a certification — a permanent lightning-exposed install
wants a qualified LPS / local-code (ČSN EN 62305) review.*

## Why isolated DC-DC modules everywhere

Fed straight from a **9–15 V battery** (12 V nominal). Non-isolated step-downs that run
directly off the battery weren't sourceable, so **isolated modules are used throughout** —
**SPBW** (the first-schematic part, **1 kV short-term**) for the non-ISO tiers
(SHORT / Modbus / LONG), and the **SCWN** (a different part, **3 kV DC**) for the ISO tier
only. The cheap 1 kV SPBW is fine in the non-ISO tiers because their **primary and secondary
are tied** (one potential, earthed at a single point) — the isolation does no barrier duty
there, so its rating doesn't matter. Only the ISO tier uses isolation for real, and gets the
3 kV part.

**Pick the wattage by node count / consumption** — a lone small node doesn't need 20 W,
three nodes won't run on 3 W. Left to the builder; these modules are the cost driver.

## Tiers

| Tier | DC-DC | Isol. | Output TVS | Choke | GDT 60V | Grounds |
|---|---|:--:|---|:--:|:--:|---|
| **SHORT / Modbus** | SPBW 5 V + 12 V | 1 kV s.t. | 1.5KE6.8A / 1.5KE15A | — | — | P↔S tied, 1-pt earth |
| **LONG** | SPBW 12 V | 1 kV s.t. | 5KP12A | 10–22 µH on **+** | GDP60V → earth | tied, 1-pt earth |
| **LONG / ISO** | SCWN06A-12 (bus) + SCWN03-05 (ISO1450 5 V / 3.3 V) | **3 kV DC** | 5KP12A | 10–22 µH **both rails** | GDP60V → **earth rod only** | bus floats; earth **NOT** on internal (−) |

## SHORT / Modbus (in-station)

9–15 V → SPBW isolated DC-DC → 5 V and/or 12 V, each clamped by a unipolar **1.5KE** TVS
(6.8A for 5 V, 15A for 12 V). Primary and secondary tied, earthed at one point — same
potential, so the cheap low-isolation module is effectively non-isolated. **No GDT**
(nothing would quench its follow-current off a stiff rail).

## LONG (distant, grounded)

Adds the surge front on the **+** output: a **10–22 µH surge choke** (di/dt limiter) → a
bigger **5KP12A** TVS → a **GDP60V** GDT to ground. The choke slows the edge so the TVS
clamps and the GDT crowbars the bulk. Still P↔S tied, single-point earth; same **SPBW (1 kV)**
family as SHORT — the 1 kV is unused (tied), the module sized to the load.

## LONG / ISO (remote / different ground)

The real isolation. A dedicated **SCWN03-05** powers the **ISO1450 bus side** and the
**SCWN06A-12 (12 V)** feeds the bus. That ISO1450 rail **can be 3.3 V directly** (the
ISO1450 works at 3.3 V) instead of 5 V — the extra switcher noise is mild and a small
**LC/RC filter** cleans it. The bus side **floats** — its **GDT goes to the earth
rod / strip only, never to the internal (−)** (mirror of the 485 ISO rule). The field side
floats relative to the internals until a strike breaks the GDT over, which snaps the surge to
the rod. Chokes on **both** rails (the floating side has no ground reference). One GDT to the
rod — not several; the 485 data channel's clamp ground stays bonded as before.

## GDT follow-current — the module must hiccup

A GDT on a **DC** rail only quenches when the source current drops below its holdover (no AC
zero-crossing). So any tier with a power-line GDT (LONG / ISO) needs the DC-DC's OCP to be
**hiccup / auto-recovery**, not constant-current foldback (Mornsun SPBW / SCWN are usually
auto-recovery — verify the chosen part). A series **PTC** ahead of the GDT also self-quenches
it regardless of the source.

## Surge choke

On the **+ only** in the grounded tiers (the − is the reference, so a single choke there
avoids needing a separate signal ground); on **both rails** in the ISO tier (floating).

## Node-input side (concept)

> **Concept — the surrounding passives (decoupling / bulk caps, filter parts, etc.) are not
> drawn yet.** Module types and the surge front mirror the source-side tiers; values get
> pinned with the rest of the bill of materials.

Each node makes its own rails from the bus, in the **same three tiers** as the source side.
**The base output is 5 V** (the node's STM32 + sensor chain); **12 V is optional** — a node
that wants it can take it (pass-through, or a 12 V peripheral). Running the **bus at 12 V is
the long-distance choice**: the node DC-DC takes a **wide ~9–36 V input**, so 12 V sits
mid-range with headroom for the cable voltage drop, and the node still regulates a clean 5 V
at the far end.

| Node tier | DC-DC (5 V base / 12 V opt.) | Isol. | Surge front |
|---|---|:--:|---|
| **NOD SHORT** | R-78E5.0-0.5 / -1.0 (non-iso buck) | — | TVS clamp |
| **NOD LONG** | SPBWxxx-05 / SPBWxxx-12 | 1 kV s.t. | 10–22 µH (5–10 A) + 5KP12A |
| **NOD LONG ISO** | SCWN06A-05 / SCWN03A-05 (5 V) + SCWN03-12 (12 V) | **3 kV DC** | chokes both rails + 5KP12A + TVS → earth rod |

- **NOD SHORT** — in-station / short hop: a non-isolated **R-78E5.0** buck (0.5 A or 1.0 A by
  load) behind a TVS; 12 V passes through.
- **NOD LONG** — distant grounded run: isolated **SPBW** modules (5 V, optional 12 V) behind a
  **10–22 µH (5–10 A)** surge choke and a **5KP12A** TVS.
- **NOD LONG ISO** — remote / different ground: fully isolated **SCWN** modules; chokes on
  **both** rails and the **5KP12A** TVS crowbar referenced to the **earth rod only** (never the
  internal −), same floating rule as the source ISO tier. A dedicated **SCWN03A-05** powers the
  **ISO1450** transceiver — and that rail **can be 3.3 V directly** (the ISO1450 runs at 3.3 V);
  the added switcher noise is mild and a small **LC/RC filter** cleans it.

**Reach (12 V default): one node up to ~100 m at up to ~4 W**, or two nodes at ~50 m — the
cost-sensible envelope; longer / branched runs are the ISO tier's job. The wide-input DC-DC
is what lets the long 12 V run still regulate cleanly. (A wider **48 V**-input part was
weighed earlier for more reach, but at these short distances it isn't needed — the 12 V part
covers it.)

## Threat model

This protects against **induced / nearby** strikes — a bolt to the *surroundings* couples a
fast transient into the cable, the common and survivable case. A **direct hit vaporises** any
node-level SPD; no practical box-level protection stops a kA channel through the enclosure,
and chasing it is pointless. Accepted: the node is **sacrificial**, and the cheap COTS +
re-enterable-gel build (HARDWARE) makes a vaporised node a cheap swap, not a disaster.
Right-sized, not gold-plated.

## Standards sanity-check

Aligns with accepted practice: **coordinated SPD** (GDT coarse + TVS fine + choke/R series,
IEC 61643-11/-21), **single-point earth / equipotential bond** to a rod (IEC 62305-3/-4),
**12 V SELV** so no mains-touch hazard, surge-immunity intent (IEC 61000-4-5). Verify:

- **Isolation ratings — covered.** ISO tier: the SCWN modules are **3 kV DC**, well above the
  GDT break-over (60 V) + any realistic ground-potential difference, so the barrier won't
  flash before the GDT fires (6 kV exists but the cost climbs for no gain). Non-ISO tiers: the
  SPBW is only **1 kV short-term**, which is fine — there the primary/secondary are tied, so
  the isolation isn't a barrier and its rating is moot.
- **Earth electrode** resistance + bonding per 62305-3 / local code.
- **DC-DC OCP is hiccup** (GDT quench, above), and the TVS standoff clears the 12 V rail but
  stays under the downstream abs-max.

Not a certification — a permanent lightning-exposed install wants a qualified LPS review.
