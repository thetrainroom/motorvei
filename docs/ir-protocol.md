# CarMotion native IR protocol (module → vehicle)

Decoded 2026-09-26 from the 8403 IR Mini (firmware 1.06) by combining scope captures of the IR output
(`captures/scope/`) with USB captures of what CarManager writes into the module (`captures/usb/`).
Physical timing is in [physical-layer.md](physical-layer.md); the CV storage in [cv-map.md](cv-map.md).

Status: frame format, flag rule and CRC are **confirmed** on every frame and code block observed
(14 scope captures, 49 distinct code blocks). The command table is confirmed where marked; meanings of some
bits are hypotheses and are marked as such.

## 1. Frame

```
[sync S S S] [flag] [byte 1] [byte 2] ... [CRC] [end S]
```

| Field | Content |
|---|---|
| sync | 3 × 169 µs symbols |
| flag | 1 bit. `1`: bytes are sent as-is. `0`: every byte is sent bit-inverted |
| bytes | command bytes, MSB first, then a CRC byte |
| CRC | **CRC-8/MAXIM** (Dallas 1-Wire: poly 0x31 reflected = 0x8C, init 0x00, no final XOR) over the command bytes |
| end | one 169 µs symbol |

### Flag selection rule

The sender picks the polarity that puts **fewer `1` bits on the air** (a `1` costs 130 µs, a `0` 90 µs, so
this shortens the frame). On a tie the flag is `0` (inverted). Verified on all observed codes, e.g.:

| Data | 1-bits as-is | Flag | On-air frame |
|---|---|---|---|
| `BE 01 80` (stop) | 8 of 24 | 1 | `1 10111110 00000001 10000000` |
| `BE 2A 83` (50 km/h) | 12 of 24 (tie) | 0 | `0 01000001 11010101 01111100` |
| `BE 22 41` (10 km/h) | 10 of 24 | 1 | `1 10111110 00100010 01000001` |
| `BE 13 1E 73` (reverse, 30 cm) | 18 of 32 | 0 | `0 01000001 11101100 11100001 10001100` |

This rule is why the very first captures looked like "the first 14 bits invert depending on the command
parity".

### Reference implementation

```python
def crc8_maxim(data):
    c = 0
    for b in data:
        c ^= b
        for _ in range(8):
            c = (c >> 1) ^ 0x8C if c & 1 else c >> 1
    return c

def frame_bits(cmd):                       # cmd: command bytes without CRC
    data = list(cmd) + [crc8_maxim(cmd)]
    ones = sum(bin(b).count("1") for b in data)
    flag = 1 if ones < 8 * len(data) - ones else 0
    air = data if flag else [b ^ 0xFF for b in data]
    return str(flag) + "".join(format(b, "08b") for b in air)
```

`tools/cmdcode.py --encode BE 2A` prints data, CarManager code block and on-air bits.

## 2. Command bytes

All commands seen so far are 2 command bytes + CRC; "reverse with custom distance" adds a third byte.

**Byte 1 is the recipient filter** (found 2026-09-27 by toggling the *Finetuning* filters, `usb/37`–`46`):

| Bit | Meaning |
|---|---|
| 7 | on/off / second-code bit (see below) |
| 6 | 0 (not seen set) |
| 5 | vehicles in lane 2 obey |
| 4 | vehicles in lane 1 (default lane) obey |
| 3 | vehicles in lane 0 (stopping lane) obey |
| 2 | vehicles travelling in direction A obey |
| 1 | vehicles travelling in direction B obey |
| 0 | 0 (not seen set) |

`BE` = all lanes and both directions (factory). Unticking a filter clears its bit in every code, e.g. lane 0 off
→ `B6` / `36`. Bits 1–5 equal CV5. So a command can be addressed to vehicles in a given lane and/or direction.

Bit 7 is cleared (`3E` with all filters) for a second meaning that depends on the group:

| Group | `BE` | `3E` | Evidence |
|---|---|---|---|
| direct functions | on | off | usb `13` (Main lights on/off) |
| stop / go | second code of the slot (long flash) | first code (short flash) | scope `_01`, `_02`; usb `05` |
| magnetic sequence | magnet sequence command | "stop at magnet" used by *brake + stop* | usb `16`, scope `_14` |

The general meaning of this bit is **not yet established** (hypothesis for stop/magnet: `3E` = "deferred /
trigger mode").

**Byte 2** is structured as type bits + value:

| Byte 2 | Meaning | Status / evidence |
|---|---|---|
| `00` | continue / drive away (with `3E 00` as first code) | scope `_02_go`; usb `18` (factory slot 2, menu `23`) |
| `01` | stop (with `3E 01` as second code) | scope `_01`, `_07`; usb `03`, `05` |
| `02` | brake | scope `_03_brake`; usb `16` |
| `03` | restore default driving mode (clear existing commands, like magnet sequence S-N) | usb `101` |
| `04`–`11` | **Macro n = `03` + n** (Macro 1 … Macro 14); Macro 2 = blink left | scope `_04`, `_05`; usb `106`–`119` |
| `12` | ? | not seen |
| `13` [cm] | reverse while in range of the signal; optional 3rd byte = custom distance in cm | usb `07`, `08`; scope `_11`, `_12` |
| `14` | reverse until stopped on a magnet | usb `103` |
| `15`, `16` | ? (`16` appears only as CarManager placeholder) | not seen |
| `17`–`1E` | traffic lanes (table below) | usb `15` |
| `20 + n` | **set speed to n × 5 km/h**; CarManager offers n = 0…22 (0–110 km/h) | usb `10`, `11` (7 values); scope `_13` |
| `40`–`47` | magnetic sequence, pole pattern in the low bits (table below) | usb `09`, `17` |
| `60`–`6B` | direct function, momentary / not permanent (table below) | usb `12`–`14` |
| `70`–`77` | direct function, permanent (`60`–`67` + `10`) | usb `12`, `13` (Main lights); others inferred |
| `50 F0` / `51 F0` / `52 F0` | rolling highway: exit / enter / enter (tightly packed) — 3-byte payload | usb `124`–`126` |
| `80`–`9F` | basic command **with modifiers** (see below) | usb `69`–`97`, `83`–`94` |

### Modifiers on basic commands (options)

When an option is ticked, byte 2 of a basic command gets bit 7 set plus flag bits, and the low bits keep the
command (stop `01`, brake `02`, …). Byte 1 bit 0 means "switch to the stopping lane".

| Option | Byte 2 bit | Byte 1 | Option CV bit | Evidence |
|---|---|---|---|---|
| (any modifier present) | 7 | | | |
| ignored by emergency vehicles | 4 (`+90`) | | `10` | usb `69`, `85` |
| turn signal left | 3 (`+88`) | | `08` | usb `71`, `87` |
| turn signal right | 2 (`+84`) | | `04` | usb `73`, `92` |
| set stopping lane (lane 0) | 7 | **bit 0** (`BE` → `BF`) | `80` | usb `83` |
| short range | — | — | `01` (flash width only) | usb `67`, `81` |
| custom distance | — | — | distance CV | 3rd byte = cm (1–255), e.g. `BE 8A 1E` | usb `88`–`91` |
| time limit / + random | — | — | CV56 / CV57 (slot 1, seconds) | none — timed by the module | usb `75`–`80` |

Examples: stop + emergency + turn right = `BE 95`; brake + stopping lane = `BF 82`; brake + turn left + 30 cm
= `BE 8A 1E`.

### No command

"no command" writes header `00` into both code blocks (length 0): nothing is transmitted (usb `129`).

### Speed

The four direct speed commands differ only in **byte 1 bits 7 and 0** (`usb/130`–`133`, 50 km/h):

| GUI entry | Command | Byte 1 bit 7 / bit 0 |
|---|---|---|
| set speed to value | `BE 2A` | 1 / 0 |
| set minimum speed | `3E 2A` | 0 / 0 |
| set maximum speed | `3F 2A` | 0 / 1 |
| set default speed | `BF 2A` | 1 / 1 |

So byte 1 bits 7 and 0 are sub-type bits whose meaning depends on the command group (on/off for functions,
stopping lane for basic commands, speed type here).

`20 + km/h ÷ 5`, measured at 0, 5, 10, 15, 20, 50, 110 km/h (`usb/11_green_speed_sweep`, `usb/10`).
The value occupies the low 5 bits (`001nnnnn`), so n up to 31 (155 km/h) fits the encoding — values above 22
are **untested**. The speed is absolute ("set speed to"), which is safe to receive repeatedly.

### Traffic lanes (`usb/15_green_function_to_traffic_lanes`)

| Byte 2 | GUI entry |
|---|---|
| `17` | set lane 0 (alternate or stopping lane) |
| `18` | set lane 1 (base lane) |
| `19` | set lane 2 (inner lane) |
| `1A` | activate special status |
| `1B` | deactivate special status |
| `1C` | toggle special status (a toggle — avoid for repeated transmission) |
| `1D` | set traffic direction A |
| `1E` | set traffic direction B |

### Magnetic sequence (`usb/17_green_magnetic_sequence_sweep`)

The low 3 bits are the magnet pole pattern in binary, **S = 1, N = 0**. An N magnet always means "stop
immediately", so every longer pattern must start with S — the leading 1 acts as a start bit that also encodes
the pattern length.

| Byte 2 | Pattern | Binary |
|---|---|---|
| `40` | N (stop immediately) | `0` |
| `41` | S | `1` |
| `42` | SN | `10` |
| `43` | SS | `11` |
| `44` | SNN | `100` |
| `45` | SNS | `101` |
| `46` | SSN | `110` |
| `47` | SSS | `111` |

`48`–`5F` would encode 4- and 5-magnet patterns in the same scheme; CarManager does not offer them and they
are **untested**.

The magnets themselves are ordinary permanent magnets (Viessmann 8431 is a generic pack); the information is
the pole facing up. Open for vehicle tests: minimum strength / height under the road surface, and the maximum
spacing between magnets that the car still reads as one sequence.

### Direct functions (`usb/14_green_function_list_sweep_off_nonperm`, `usb/13`)

Byte 1: `BE` = on, `3E` = off. Byte 2: `60 + function`, `+10` for permanent.

| # | Function | Byte 2 | Permanent |
|---|---|---|---|
| 0 | Main lights | `60` | `70` |
| 1 | High beam | `61` | `71` |
| 2 | Roof beacons | `62` | `72` |
| 3 | Turn signals, left | `63` | `73` |
| 4 | Turn signals, right | `64` | `74` |
| 5 | Hazard signal | `65` | `75` |
| 6 | Expansion output / Universal function 1 | `66` | `76` |
| 7 | Universal function 2 | `67` | `77` |
| 8 | Sound: siren | `68` | — |
| 9 | Sound: engine | `69` | — |
| 10 | Sound: horn | `6A` | — |
| 11 | Sound: shout | `6B` | — |

Sounds have no on/off or permanent option in the GUI; they were captured with `3E` (left over from the
"off" state). Whether sounds need `BE` is open.

### Reverse with custom distance

`BE 13 <cm> CRC`, e.g. 30 cm → `BE 13 1E 73` (33-bit frame, `usb/08`, scope `_12`). The distance is data for
the vehicle, not a change of LED power.

## 3. Two codes per slot, separate channels

Each command slot (red / green) holds two code blocks (code 1, code 2):

| GUI command | Code 1 | Code 2 | Output |
|---|---|---|---|
| stop | `3E 01` | `BE 01` | both alternate on the probed LED; code 1 with 21 µs, code 2 with 26 µs flashes (scope `_01`, `_09`) |
| continue / drive away | `3E 00` | `BE 00` | same pattern (scope `_02`; factory slot 2 in usb `18`) |
| brake, macros, speed, lanes, functions, magnets | same code in both | | one frame type |
| **brake + stop (on separate channels)** | `3E 40` | `BE 02` | code 1 only on the **white-marked LED** (stop LED), code 2 only on the unmarked LED (brake LED) (scope `2026-09-26_14`, `2026-09-27_11`; handbook) |

In *brake + stop*, the vehicle is first told to brake (LED 2) and then to stop at the next N magnet (LED 1).
If power fails after the brake command, the car still stops mechanically at the magnet.

**Self-built blocks are transmitted (2026-09-27).** Writing `A3 41 D9 DF` (set speed 30 km/h, separate
channels, short flash) into slot 1 code 1 via *Direct configuration* made the module send `BE 26 20` alongside
the brake code, immediately, with 21 µs flashes (`usb/23`, scope `2026-09-27_09`). Likewise `23 BE 70 26`
(main lights on, permanent) in slot 3 code 1 was sent as `BE 70 26` when slot 3 was active
(`usb/24`, scope `2026-09-27_10`). So any command can be placed
in a slot, including in dual-channel mode: code 1 goes to the white-marked LED, code 2 to the unmarked LED
(single-ground capture `2026-09-27_11`: self-built lights code on the white-marked LED, brake on the other).
A clean confirmation with a load on the torn-off LED's cable end is still worthwhile.

## 4. CarManager code block (how the module stores a code)

CarManager writes each code as a block of up to 6 CVs: `header, stored bytes..., 00 padding`.

| Header bit | Meaning | Status |
|---|---|---|
| 7 | the on-air flag bit | confirmed (all blocks) |
| 6 | **long flash (26 µs) if set, short (21 µs) if clear**; *short range* forces 21 µs | observed on CarManager blocks; tested with a self-built block (bit 6 clear → 21 µs, scope `2026-09-27_09`) |
| 5 | separate channels (code 1 → LED 1, code 2 → LED 2) | observed only in *brake + stop* (`23` / `E3`) |
| 4–3 | — | seen only in the unexplained placeholder `7A` |
| 2–0 | number of bytes (command bytes + CRC) | confirmed (3 and 4) |

Stored bytes = on-air bytes inverted. Equivalently: data = stored XOR (`FF` if flag else `00`).
Headers observed: `03 23 43 44 C3 E3` (plus `7A`, below).

The module does not interpret commands: it plays back the stored block. So any valid block — including
commands and parameter values CarManager does not offer — can be transmitted by writing the CVs
(see cv-map.md). `tools/cmdcode.py` decodes and encodes blocks.

When the category changes, CarManager first writes a placeholder and then the real code. Placeholders seen:
`43 BE FF EB` (data `BE FF`, valid CRC) and once `7A C4 AB` (header `7A`, one command byte `C4`, valid CRC)
during a Macros → Basic change (`usb/03`). Their meaning is unknown.

## 5. Not yet captured

- Basic commands: *restore default driving mode*, *reverse (until stopped on a magnet)*
- Macros beyond 1 and 2 (and their menu mapping)
- Rolling highway
- "no command" (what an empty slot stores / sends)
- Options: turn signal, ignored by emergency vehicles, set stopping lane, custom distance on other commands
- Vehicle reaction to anything — no vehicle has been in the loop yet

Sweeping unknown byte values with a vehicle in range may hit configuration or reset commands (brief §6):
use a spare vehicle with a backed-up configuration.
