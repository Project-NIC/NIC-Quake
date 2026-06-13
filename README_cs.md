<p align="center">
  <img src="NIC-Quake.svg" width="200"/>
</p>

*[English](README.md) · [Čeština](README_cs.md) · [Русский](README_ru.md)*

★ N.I.C. ★

# NIC-QUAKE

## Distribuovaná Seismografická Síť — Referenční Hardwarový Návrh

[![License: MIT](https://img.shields.io/badge/License-MIT-red.svg)](https://opensource.org/licenses/MIT)
[![License: CERN-OHL-S v2](https://img.shields.io/badge/License-CERN--OHL--S%20v2-red.svg)](https://ohwr.org/cern_ohl_s_v2.txt)

---

## Co je QUAKE?

QUAKE je open-hardware referenční návrh snímače vhodného i pro distribuovanou seizmografickou síť. Každý uzel je uzavřená, samostatná jednotka určená k trvalé instalaci ve skále, základech budov atd. — zalitá polyuretanovou zalévací hmotou, připojená vodotěsným konektorem M12, a ponechaná svému osudu na roky.

Projekt vznikl z jednoduché frustrace: existující přístupy buď stojí majlant (geofonky), vyžadují přesnou mechanickou instalaci, nebo kompenzují levné zašuměné senzory tunami softwaru. QUAKE jde přesně opačnou cestou — utrať víc za hardware, dostaneš čistý signál rovnou, software zůstane jednoduchý.

---

## Proč ne geofon?

- Stojí tolik co celý uzel QUAKE
- Špatná odezva na nízkých frekvencích — pod rezonancí prudký pokles zisku, nutné složité korekční zesilovače s fázovými chybami
- Tři samostatné kusy pro tři osy
- Vyžaduje přesné vyrovnání a mechanickou instalaci

## Proč ne kapacitní snímání?

- Proof mass musí být těžká pro citlivost, ale těžká znamená nízkou vlastní rezonanci a snadné rozkmitání
- Parazitní kapacity vůči stěnám pouzdra se mění s teplotou, vlhkostí a geometrií
- Výsledek: nestabilní signál který odráží podmínky v pouzdru, ne seizmickou aktivitu

## Proč MEMS?

MEMS proof mass je mikroskopická — vlastní rezonance je v kHz, seizmika je pod 20 Hz. Navzájem si nelezou do zelí. Senzor je hermeticky uzavřen na jednom křemíkovém die, kalibrován z výroby, digitální výstup, tři osy v jednom pouzdru. Jeden čip nahradí tři geofonky za zlomek ceny.

---

## Jak to funguje

### Virtuální urovnání — nainstaluj jak chceš

Žádné libely. Žádné seřizovací šrouby. Žádná přesná mechanická instalace.

Hodíš jednotku do díry ve skále, pošleš kalibrační příkaz, procesor zaznamená aktuální stav senzorů jako referenční nulu. Gravitační vektor automaticky definuje absolutní orientaci — osa Z kolmá k zemskému jádru bez ohledu na fyzické natočení. Transformační matice udělá zbytek.

### Hardwarová synchronizace hodin

Všechny uzly sdílejí jeden fyzický hodinový signál distribuovaný masterem po druhém páru RS-485. Nejde o softwarové timestampování ani NTP — jde o skutečný hodinový signál na drátě. Nejistota synchronizace je v nanosekundách, ne milisekundách.

### Topologie sítě

Standardní kabel UTP Cat 6 (venkovní provedení), čtyři páry — dva pro RS-485 data a hodiny, dva pro napájení. Daisy-chain topologie, každý uzel má dva konektory M12 IP68 (vstup a výstup), terminační odpory pouze na každém konci linky. Až 8 uzlů na segment.

### Modulární návrh

Každý senzor využívá jedno SPI rozhraní z MCU s vlastním Chip Select pinem. Univerzální plošný spoj osadí všechny tři pozice — nepotřebný senzor se jednoduše neosadí. Firmware při startu automaticky detekuje přítomné senzory přes SPI. Žádná rekompilace.

---

## Dokumentace

| Dokument | Popis |
|---|---|
| [HARDWARE.md](HARDWARE.md) | Výběr součástek s odůvodněním |
| [SOFTWARE.md](SOFTWARE.md) | Komunikační architektura, sync protokol, datový stack |
| [LICENSE](LICENSE) | MIT — software |
| [LICENSE-HW](LICENSE-HW) | CERN-OHL-S v2 — hardware |

---

## Datový stack

| Vrstva | Knihovna | Role |
|---|---|---|
| Komprese | [NIC-DMD](https://github.com/Project-NIC/NIC-DMD) | Adaptivní komprese, bez tabulek, běží na ATmega328 |
| Úložiště | [NIC-MLA](https://github.com/Project-NIC/NIC-MLA) | Jednosouborem kontejner, crash-safe, self-describing |
| Šifrování transportu | [NIC-KSF](https://github.com/Project-NIC/NIC-KSF) | SPECK-128 CTR, in-place, bez malloc |
| Zápis | [NIC-GLUE-IN](https://github.com/Project-NIC/NIC-GLUE-IN) | Senzorová data → DMD → MLA |
| Čtení/export | [NIC-GLUE-OUT](https://github.com/Project-NIC/NIC-GLUE-OUT) | MLA → CSV / SQLite |
| Desktop prohlížeč | [NIC-VDE](https://github.com/Project-NIC/NIC-VDE) | MLA jako adresář, export CSV/SQL |

---

## Stav projektu

**Firmware: přenositelné jádro hotové a otestované (v0.5.0)** — drivery čidel (včetně per-čidlo teploty a runtime změny rozsahu), auto-detekce, kalibrace os, RS-485 protokol a stavový automat nodu, vše otestováno mimo cílový čip; STM32 board vrstva je napsaná a čeká na desku. **Hardware:** referenční návrh definovaný, schémata se kreslí. Na řadě je PCB a terénní testování.

Příspěvky vítány — hardware, firmware i software.

---

## Licence

Hardware: CERN-OHL-S v2 — Copyright (c) 2026 NIC — Native Intellect Community

Software: MIT License — Copyright (c) 2026 NIC — Native Intellect Community

---

## Poděkování

Bratrovi za rady při vývoji tohoto projektu.
Za technickou asistenci při optimalizaci kódu AI asistentům Claude (Anthropic) a Gemini (Google).

★ Viva La Resistánce ★
