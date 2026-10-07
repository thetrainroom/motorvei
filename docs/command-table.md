# CarMotion IR command reference

Every command a CarMotion IR module can send to vehicles, as far as decoded (IR Mini firmware 1.06, CarManager
1.32; vehicle reactions from the CarManager handbook, not yet tested by us). For each command: what the vehicle
does, how to enter it in the open transmitter (`firmware/transmitter`, profile `carmotion`), and the bytes on the
air. Protocol details: [ir-protocol.md](ir-protocol.md).

## Entering commands

A channel's playlist is a list of entries, sent one after another, round-robin.

**MRRoIP (GUI, Python, TrainMaster)** — object `ch1` / `ch2`, a JSON list:

```json
[{"cmd": "speed", "arg": 30}, {"cmd": "function_on_permanent", "arg": 0}, {"cmd": "magnet", "arg": 5, "range": "short"}]
```

**Stored default playlist** — parameter `default_ch1` / `default_ch2`, short text (max. 31 characters):

```
speed:30,function_on_permanent:0,magnet:5/s
```

`,` separates entries, `name[:arg]`, `/s` = short range. Fields left out: `arg` 0, `range` normal.

**`range`**: `normal` (26 µs flash, full reach) or `short` (21 µs flash, reaches only close vehicles). The
original uses short range for the "close-up" half of stop / continue and for the *short range signals* option.

## Basic commands

| `cmd` | `arg` | Vehicle | Bytes (without CRC) |
|---|---|---|---|
| `stop` | — | stops fairly quickly but realistically — what a stopped vehicle transmits. Sends two codes: short-range + normal-range | `3E 01` (short) + `BE 01` |
| `continue` | — | drives off at its previous speed, also from a north magnet, without waiting for signal loss (like "Play" on the remote) | `3E 00` (short) + `BE 00` |
| `brake` | — | slows down gently to crawl speed, like a south magnet, until stopped otherwise | `BE 02` |
| `restore` | — | clears temporary commands (macros, magnet sequences, IR commands), back to the default driving mode — like magnet sequence S-N | `BE 03` |
| `reverse` | distance in cm (0 = none) | drives backwards while in range of the signal; with a distance, that far | `BE 13` [`cm`] |
| `reverse_magnet` | — | drives backwards until stopped on a north magnet, also after leaving the signal | `BE 14` |

## Speed

`arg` in km/h (model speed as configured in the vehicle), 0–155 in steps of 5 (CarManager offers 0–110). Byte:
`0x20 + km/h ÷ 5`.

| `cmd` | Vehicle | Bytes (50 km/h) |
|---|---|---|
| `speed` | sets its speed to the value (temporary override) | `BE 2A` |
| `speed_min` | raises its speed to at least the value | `3E 2A` |
| `speed_max` | limits its speed to at most the value | `3F 2A` |
| `speed_default` | changes its default speed permanently (like setting it with the remote) | `BF 2A` |

## Functions

`arg` = function number. `_permanent`: stays active after the vehicle is switched off and on (only functions
0–7); otherwise a temporary override.

| `arg` | Function | on | off | on, permanent | off, permanent |
|---|---|---|---|---|---|
| 0 | main lights | `BE 60` | `3E 60` | `BE 70` | `3E 70` |
| 1 | high beam | `BE 61` | `3E 61` | `BE 71` | `3E 71` |
| 2 | roof beacons | `BE 62` | `3E 62` | `BE 72` | `3E 72` |
| 3 | turn signal left | `BE 63` | `3E 63` | `BE 73` | `3E 73` |
| 4 | turn signal right | `BE 64` | `3E 64` | `BE 74` | `3E 74` |
| 5 | hazard lights | `BE 65` | `3E 65` | `BE 75` | `3E 75` |
| 6 | expansion output / universal function 1 | `BE 66` | `3E 66` | `BE 76` | `3E 76` |
| 7 | universal function 2 | `BE 67` | `3E 67` | `BE 77` | `3E 77` |
| 8 | sound: siren | `BE 68` | `3E 68` | — | — |
| 9 | sound: engine | `BE 69` | `3E 69` | — | — |
| 10 | sound: horn | `BE 6A` | `3E 6A` | — | — |
| 11 | sound: shout | `BE 6B` | `3E 6B` | — | — |

`cmd` = `function_on`, `function_off`, `function_on_permanent`, `function_off_permanent`. Whether sounds need
`on` or play either way is open (no sound vehicle available, 2026-10-07).

## Traffic lanes

`cmd` = `lane`. For the vehicles' lane-aware distance control (same as the lane settings in their macros).

| `arg` | Vehicle | Bytes |
|---|---|---|
| 0 | sets lane 0 (alternate / stopping lane) | `BE 17` |
| 1 | sets lane 1 (base lane) | `BE 18` |
| 2 | sets lane 2 (inner lane) | `BE 19` |
| 3 | activates special status | `BE 1A` |
| 4 | deactivates special status | `BE 1B` |
| 5 | toggles special status — avoid in a repeating playlist | `BE 1C` |
| 6 | sets traffic direction A | `BE 1D` |
| 7 | sets traffic direction B | `BE 1E` |

## Magnetic sequences

`cmd` = `magnet`. Acts as if the vehicle had driven over that sequence of permanent magnets (S = south, N = north),
but only while the module sends it. `arg` = the pattern in binary, S = 1, N = 0.

| `arg` | Pattern | Default meaning in the vehicle | Bytes |
|---|---|---|---|
| 0 | N | stop | `BE 40` |
| 1 | S | brake | `BE 41` |
| 2 | SN | clear commands | `BE 42` |
| 3 | SS | per vehicle configuration | `BE 43` |
| 4 | SNN | per vehicle configuration | `BE 44` |
| 5 | SNS | per vehicle configuration | `BE 45` |
| 6 | SSN | per vehicle configuration | `BE 46` |
| 7 | SSS | high beam on (factory) | `BE 47` |

## Macros

`cmd` = `macro`, `arg` = 1–14. Runs the macro stored **in the vehicle** (configured per vehicle in CarManager).
Macros 1 and 2 are the left / right turn signal by default. Bytes `BE 03+n` (macro 1 = `BE 04` … macro 14 = `BE 11`).

## Rolling highway

`cmd` = `rolling_highway`.

| `arg` | Vehicle | Bytes |
|---|---|---|
| 0 | exit | `BE 50 F0` |
| 1 | enter | `BE 51 F0` |
| 2 | enter (tightly packed) | `BE 52 F0` |

## Raw

`cmd` = `raw`, `raw` = command bytes in hex without the CRC, e.g. `{"cmd": "raw", "raw": "BE 2A"}` or `raw:BE2A`.
The transmitter adds the CRC and the polarity flag. For combinations the table does not cover (below), and for
experiments — **untested byte values may trigger unknown vehicle behaviour; use a spare vehicle.**

## Options (via `raw` for now)

In CarManager these are checkboxes; in the bytes they modify a basic command (see ir-protocol.md):

| Option | Change | Example |
|---|---|---|
| ignored by emergency vehicles | byte 2 + `90` | stop → `BE 91` |
| turn signal left / right | byte 2 + `88` / + `84` | brake + left → `BE 8A` |
| switch to stopping lane | byte 1 bit 0, byte 2 + `80` | brake + stopping lane → `BF 82` |
| custom distance | extra byte, cm | brake + left + 30 cm → `BE 8A 1E` |
| recipient filter (lanes / directions) | byte 1 bits 1–5 | stop only for lane 1 → `90 01` (`BE` with bits 3, 5, 2, 1 cleared) |

## Not yet decoded

Byte-2 values `12`, `15`, `16`, `1F` and the ranges `48`–`5F` (would be 4- and 5-magnet patterns) are unused by
CarManager and untested. Vehicle-to-vehicle frames (distance control, telemetry) are Stage 5.
