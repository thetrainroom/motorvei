# Physical layer — Viessmann 8403 IR Mini output

Findings from Stage 1 (session 2026-09-26). Every claim cites the capture it came from; scope captures are
in `captures/scope/`, USB captures in `captures/usb/`, all listed in `captures/log.csv`.

## Setup

| Item | Value |
|---|---|
| Module | Viessmann 8403 IR Mini (damaged unit, one LED torn off), firmware 1.06, operation mode DCC, DCC address 332 |
| Probe point | LED pads of the remaining LED channel (load as wired on the bench — **not recorded, record next time**) |
| Power | 2026-09-26: **ESU LokProgrammer DCC track output** (module in DCC mode, address 332, DCC signal present). 2026-09-27: 16 V AC Märklin transformer (no DCC signal). The 8401 programming cable does not power the LED outputs |
| Scope | Rigol DS1104Z Plus, firmware 00.04.04.SP3, CH1 DC, 2 V/div, 1x probe |
| Acquisition | 10 MSa/s, 12 Mpts = 1.2 s window, edge trigger rising 2.6 V, raw bytes via `tools/grab_raw.py` |
| Decoder | `tools/irframes.py` (rising-edge timing + CRC check) |

At 10 MSa/s (100 ns per sample) a 38 kHz carrier (26 µs period) would be resolved with ~260 samples per
cycle, so the absence of a carrier is a direct observation, not an inference.

## Result: flash pulse-distance coding, no carrier

| Property | Value | Evidence |
|---|---|---|
| Carrier | **none** — each mark is one single flash | all scope captures |
| Flash width, long | 26.1–26.3 µs | `2026-09-26_01_stop_both_codes`, `_03_brake`, `_09_green_stop_cable_moved` |
| Flash width, short | 21.2–21.4 µs | same captures (second code), `_10_green_stop_short_range` |
| Flash level at pad | 4.93–4.97 V (σ ≈ 0.1 V) | `_09`, `_10` (`flashstats` in session; level identical for both widths) |
| Level between flashes | 0.2–1.1 V pedestal, decaying after each flash | `_09` |
| Bit `0` | 90 µs flash-to-flash | all captures |
| Bit `1` | 130 µs flash-to-flash | all captures |
| Sync / end symbol `S` | 169 µs flash-to-flash (never occurs inside data) | all captures |
| Symbol quantum | periods = 50.5 µs + k × 39.5 µs, k = 1, 2, 3 | derived from the three periods |

Periods measured rising edge to rising edge are stable to ±1 µs.

```
 flash                 flash        flash         flash
  ▌                     ▌            ▌             ▌
  |<------ 169 µs ----->|<- 90 µs -->|<-- 130 µs -->|
        sync / end          bit 0         bit 1
```

## Frame

```
S S S  <flag bit>  <8·n data bits>  S
```

- 3 × 169 µs sync (4 flashes), 1 flag bit, n data bytes (last one is a CRC), one 169 µs end symbol.
- Typical 3-byte frame: 25 bits → ~3.4 ms. A 4-byte frame (reverse + distance) has 33 bits
  (`_12_green_reverse_short_range_distance30cm`).
- The bit-level meaning (flag, inversion, CRC) is described in [ir-protocol.md](ir-protocol.md).

## Repetition

- Frames are repeated continuously while the command is active: ~75–100 valid frames per 1.2 s window
  (`_03_brake`: 90, `_05_macro2`: 106, `_13_green_speed50_short_range`: 103).
- When a slot holds two different codes they alternate, in pairs about **4.9–5.0 ms** apart, followed by a
  **variable gap of ~10–20 ms** (`_01_stop_both_codes`). The variable gap looks like deliberate randomisation
  so nearby transmitters do not collide permanently.

## Range: flash width is the "transmitter power"

*Finetuning* "transmitter power" (CV3) sets the flash length: **width ≈ CV3 + 1 µs** — factory 25 → 26 µs,
15 → 16.0 µs (scope `2026-09-27_13`, `usb/47`). "Extra closeup" (CV4) is subtracted for short-range codes:
25 − 5 → 21 µs. The "power" is pulse length, not LED current.

## Range: "short range signals" shortens the flash, not the voltage

| Setting | Code 1 flash | Code 2 flash | Level | Evidence |
|---|---|---|---|---|
| normal | 21 µs | 26 µs | 4.93–4.97 V | `_09_green_stop_cable_moved` |
| short range ticked | 21 µs | **21 µs** | 4.97 V (unchanged) | `_10_green_stop_short_range` |

- Bits and timing are identical; only the long flashes shrink from 26 to 21 µs.
- CarManager writes a single CV (CV39) for this option; the code blocks do not change
  (`captures/usb/06_green_stop_shortrange_on.pcapng`).
- Caveat: the probe measures pad **voltage**, not LED **current**. Equal voltage strongly suggests an unchanged
  drive level, but LED current (10 Ω series resistor) or optical output (photodiode) has not been measured yet.

Consequence for our module (brief §4, Stage 1 note): range is set by **flash width** on the original. There is
no carrier, so width control means pulse length, which does not disturb bit timing (the bit is the
flash-to-flash period). A current sink stays useful for fine range tuning but is not required to copy the
original behaviour.

"Custom distance" is **not** a physical range setting — it is a data byte in the frame (see ir-protocol.md).

## Two LED channels

- The IR Mini has two LED outputs. With "brake + stop (on separate channels)" each LED sends a different
  code (`2026-09-26_14`, `2026-09-27_11`), probe on the torn-off LED's cable end (the LED with the **white
  cable marking**, i.e. the stop LED per the CarManager handbook):
  - `3E 40` (stop at magnet): sharp rise, then a **slow exponential decay** (τ ≈ 60 µs) — this is the probed
    channel's **own** output: its LED is gone and no load was fitted, so the node cannot discharge
  - `BE 02` (brake): clean flashes — the **other** (intact) LED's output coupled through a shared node, with the
    intact LED providing the fast discharge path
  - Correction (2026-09-27): earlier notes read this the other way round (sharp = own channel). The white
    marking and the handbook settle it: **code 1 → white-marked (stop) LED, code 2 → unmarked (brake) LED.**
- One probe sees both channels; the rising-edge decoder separates them by content. Fit a load (1 kΩ or a spare
  IR LED) across an LED-less cable end so its own flashes become sharp.
- In normal (non-separate) mode, both codes of a slot appeared alternately on the probed LED
  (`_01`, `_07`–`_10`). Whether the other LED sends the same alternation has not been checked.

## Measurement artefacts (not part of the signal)

- **Scope grounds tie channels together (2026-09-27):** with CH1 and CH2 ground clips on the "other side" of
  each LED, the scope's common ground connected the two LED channels, so both probes showed both codes in a
  fixed 1:10 ratio (CH1 probe setting). Use **one** ground clip on module GND for all probes. Frame content in
  those captures is valid, per-LED attribution is not.

- **Dips at flash start:** some flashes briefly dip below threshold right after the rising edge
  (e.g. 3.3 → 1.1 → 5.9 V), splitting the flash into a 1–2 µs fragment and a shorter remainder. A width-based
  decoder counts extra pulses; the rising-edge decoder with 5 µs debounce is immune (`_07`–`_09`).
- **Clipping:** extremes of −5.6 / +9 V in some captures are ADC range limits during those events.
- **Noisy probe contact:** `_06_red_stop_green_macro2_noisy_probe` shows flash widths wandering 22–25 µs;
  re-attaching the probe fixed it.

## Open items

- Record the load on the probed pads; measure LED current (10 Ω series) and optical output (photodiode).
- Check what the second LED sends in normal (non-separate) mode.
- Explain header bit 6 vs flash width (see ir-protocol.md).
- A bare photodiode receiver, not a 38 kHz module (VS1838B), is needed for the scanner: the module signal has
  no carrier.
