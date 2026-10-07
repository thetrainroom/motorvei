# Other IR protocols CarMotion vehicles understand

Collected from public sources on 2026-09-26. None of this has been measured in this project yet; each
section says how to verify it.

A CarMotion vehicle has to tell apart at least three, probably four, IR signal types. They differ strongly in
timing, so one photodiode + microcontroller can classify them by edge timing alone:

| Signal | Source | Pattern |
|---|---|---|
| NEC | handheld remote 8402 | 38 kHz bursts (13 µs on / 13 µs off), 9 ms leader |
| Native flash frames | IR Mini / modules ([ir-protocol.md](ir-protocol.md)) | isolated 21–26 µs flashes, 90 / 130 / 169 µs spacing |
| DCC over IR | DC-Car / OpenCar stop points (compatibility mode) | DCC square wave, 58 µs / ~100 µs half-bits, LED on ~50 % |
| Vehicle-to-vehicle | the car ahead (distance control, scanner data) | unknown — Stage 5 |

Unverified idea: the native flash widths (21 / 26 µs) are close to one 38 kHz period (26.3 µs). If the
vehicle's receiver amplifier is tuned for the remote's carrier, a single flash of that length excites it well,
and a shorter flash less — which would explain why the shorter flash means "short range". Testable with a
vehicle and a width sweep.

## Handheld remote (8402): NEC

Source: Stummiforum thread "CarMotion: Steuerung via IR Fernbedienungsbefehle durch Arduino generiert"
(<https://www.stummiforum.de/t224228f48-CarMotion-Steuerung-via-IR-Fernbedienungsbefehle-durch-Arduino-generiert.html>).
Consistent with the brief's note that the remote sends "a command in byte 1, user byte zero".

- NEC, 38 kHz, address `0x00`; decoded with Arduino IRremote, re-sent with `sendNECRaw()`
- Holding a button: one frame, then NEC repeat frames about every 92 ms

| Button | NEC command | Raw 32-bit |
|---|---|---|
| Vehicle off | `0x45` | `0xBA45FF00` |
| Accelerate | `0x40` | `0xBF40FF00` |
| Stop & reverse | `0x43` | `0xBC43FF00` |
| Drive forward | `0x15` | `0xEA15FF00` |
| Brake | `0x19` | `0xE619FF00` |
| Turn signal left | `0x07` | `0xF807FF00` |
| Turn signal right | `0x09` | `0xF609FF00` |
| Hazard | `0x5E` | `0xA15EFF00` |
| Headlights | `0x16` | `0xE916FF00` |
| High beam | `0x0C` | `0xF30CFF00` |
| Rotating beacon | `0x18` | `0xE718FF00` |
| Battery display | `0x0D` | `0xF20DFF00` |

Reported limitations: vehicles cannot be switched **on** by IR (only charger / remote proximity); off only via
`0x45`. Commands are relative (accelerate, brake), unlike the module's absolute speed.

Verify: capture our own remote with a VS1838B (demodulated) or a photodiode on CH2.

## DCC over IR (DC-Car / OpenCar "DCC-direkt")

Source: OpenCar System, "DCC-direkt (IR-Kurzstreckensignal)"
(<https://www.opencarsystem.de/basis/dcc_direkt/dcc_direkt.html>); Modellbau-Wiki "DC-Car-System".

- The IR LED is driven directly by the DCC track signal (lit during one polarity), no carrier. Reference
  circuit: IR LED + 750 Ω from ~16 V DCC (~20 mA), anti-parallel 1N4148.
- Received with a phototransistor, decoded as normal DCC → vehicles are **addressable** (1–9999).
- Range ~20–30 cm. DC-Car also has a modulated variant (36–455 kHz carrier, TSOP) with up to 10 m.
- OpenCar advises against layout-wide DCC-direkt because it disturbs inter-vehicle distance control.

### CarMotion compatibility mode

Source: Viessmann support forum, "Kompatibilitätsmodus"
(<https://viessmann-modell.com/forum/thread/4631-kompatibilit%C3%A4tsmodus/>).

| Vehicle firmware | Stops at DC-Car IR stop points |
|---|---|
| 1.40 | yes |
| 1.44 | **no** (regression) |
| 1.48 | yes again (fixed, confirmed by Viessmann staff) |

A later release also evaluates DCC speed information for smoother distance control with OpenCar. Our
vehicles run 1.48. The mode has to be enabled per vehicle in CarManager.

## Consequences for our module

- Native protocol: the main target — works on every vehicle without configuration, richest commands
  (absolute speed, magnets, lanes, distances).
- NEC: trivial on the ESP32 RMT; covers lights, indicators and switching vehicles off.
- DCC over IR: only way to address a specific vehicle regardless of beam, but needs compatibility mode per
  vehicle, is unknown on old firmware, and is known to disturb distance control.
- Different signal types must never overlap on one LED; a time-multiplexed sender handles that.
- The scanner needs a bare photodiode and edge-timing classification, not a 38 kHz receiver module.

## 8406 IR Scanner (from the manual, not measured)

Source: Viessmann operation manual 8406, Stand 01, 08/2026
(<https://viessmann-modell.com/media/1e/ae/f3/1787128103/8406.pdf>).

- Two IR receivers on cables (~2 × 1.2 × 1.2 mm), aimed at the **rear** of passing vehicles (or built into the
  road). Keep ≥ 5 cm from the InduktivCharger 8408 (interference).
- Reads vehicle category, length, speed, battery level and indicator direction from the vehicles' own IR
  transmissions — no query is sent. Consistent with the brief: vehicle telemetry can be captured with a
  photodiode behind a vehicle.
- Rule engine configured in CarManager ("Events"): conditions (e.g. battery level low) → actions (e.g. turnout
  on output 1 → right), plus a default rule and presets.
- 2 potential-free outputs (24 V DC / 16 V AC, 100 mA), inputs `+ ~ E 1 C 2`, IR output for vehicle commands
  (IR LEDs 8442 / 8443), EasyChain support, 10–16 V AC / 12–24 V DC.
- Modes: A both receivers on one section (length discrimination with detectors); B two separate sections with
  independent outputs; C detection only (occupancy output).
- Turnouts controlled over a single wire need software ≥ 1.06 → the turnout drive (8446 / 8448) contains a
  microcontroller and has its own control scheme.

## IR Mini manual and CarManager handbook (from the documents)

Sources: operation manual 8403 (Stand 05, 09/2025,
<https://viessmann-modell.com/media/a9/bb/66/1758533111/8403.pdf>) and "Handbuch CarManager IR-Mini-Modul"
(Stand 05/2025, <https://viessmann-modell.com/media/4a/94/3b/1758533110/Handbuch%20CarManager%20DE-2-1-RS-IR-Mini-mit%20Version.pdf>).

- **Stop LED = the IR LED with a white marking on its cable** (brake + stop on separate channels: the vehicle
  brakes at the unmarked LED and stops at the marked one; the two act like a south and a north magnet).
- **"Stop" (anhalten) corresponds to what a stopped vehicle sends.** Vehicles transmit two signals of different
  strength to judge distance to each other; *short range* makes the module use only the weaker setting. This
  matches the two stop codes (short / long flash) and points to the vehicle-to-vehicle beacon format (Stage 5).
- Magnet semantics: brake = south magnet, stop = north magnet, "clear existing commands" = sequence S-N,
  default S-S-S = high beam on.
- *Feineinstellung*: transmit power (factory 25), additional approach (factory 5), filters lane 0/1/2 and
  direction A/B → CV3, CV4, CV5 in the factory dump (see cv-map.md).
- Operating modes: analogue (optionally impulse-driven input, 0–255 s), pedestrian crossing (inverted mode),
  railway crossing, traffic control (phase durations, factory 10 s / 3 s / 10 s), DCC (accessory address up to
  2048; factory 1).
- Options: time limit up to 255 s + random up to 255 s; custom distance up to 255 cm; turn signal = macro 1 / 2
  (left / right); switch to stopping lane (lane 0) and back.
- Supply: 5–24 V DC (brown wire to +), 8–16 V AC (≥ 8 V for configuration), 8–24 V digital.
- Backups are saved as `*.clone` files.
- Vehicle IR test mode (CarManager → Infrared → Adaptive Cruise Control): the vehicle shows IR reception with
  its indicators — useful for range and beam-angle tests.
