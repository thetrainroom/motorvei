# IR Mini CV map and programming link

Stage 2 findings (session 2026-09-26), 8403 IR Mini firmware 1.06, CarManager on Windows 10 driving the
8401 programmer. All USB captures are in `captures/usb/` (see `captures/log.csv`), decoded with
`tools/usbserial.py`.

## Programming link

| Item | Value |
|---|---|
| Programmer | 8401, **Silicon Labs CP210x USB–UART bridge**, VID `10C4` PID `EA60`, USB serial `VIESSMANN_CARM`, COM3 |
| CarManager | Qt5 application using Qt5SerialPort — plain serial over the CP210x |
| Line settings | **38400 baud, 8 data bits, even parity, 1 stop bit (8E1)**, no flow control, DTR and RTS low — from the CP210x setup requests after re-plugging the programmer (`usb/26_programmer_replug`: SET_BAUDRATE `00 96 00 00`, SET_LINE_CTL `0x0820`, SET_MHS `0x0200`/`0x0100`) |
| Capture method | USBPcap on `\\.\USBPcap1`, device 27, started over SSH (`tools/ssh_config`) |

### Serial messages

Requests are lowercase, replies uppercase. CV numbers are 16-bit big-endian.

| Request (CarManager → module) | Reply (module → CarManager) | Meaning | Evidence |
|---|---|---|---|
| `Who` | `ViessStop\0` | identify | `00_idle_heartbeat` |
| `Ver` | `i 01 06 03 xx` | firmware 1.06; `xx` = `01` after module start-up / reconnect (CarManager then reads CV1–102), `02` while synchronising, `00` idle | `00`, `04_health_check`, `03`, `26`, `27` |
| `c r` CVhi CVlo val `00` | `C r` CVhi CVlo val | **read CV** (the request carries the GUI's current value; ignored) | `02_read_cv1` |
| `c W` CVhi CVlo val `00` | `C W` CVhi CVlo val | **write CV**, reply echoes the value | `03`, `05`–`18` |
| `c W 00 08 08 00` | `C W 00 08 08` | **factory reset** (NMRA convention: write 8 to CV8); module busy ~2 s (`Ver` status `02`) | `18_reset_all` |

- `Who`/`Ver` repeats about once per second as a heartbeat.
- On every connect, and whenever the module reports status `01` (e.g. after its supply was switched off and on,
  `usb/27_module_power_cycle`), CarManager reads CV1–102 (full dump). While the module is unpowered the
  programmer stays enumerated and CarManager keeps sending `Who` without reply.
- A CV write takes ~8–9 ms round trip. CarManager writes only CVs whose value changes, and on a category change
  first writes a placeholder code, then the real one.
- The `Ver` status byte can be polled to wait for a sync to finish.

Example (CV1 read, `02_read_cv1`):

```
> 63 72 00 01 4C 00    "cr" CV 0x0001, value field 0x4C, 00
< 43 72 00 01 4C       "Cr" CV 0x0001 = 0x4C (76)
```

## Factory reset and full dump

"Reset all" in CarManager sends a single `cW` of **8 to CV8**, waits ~2 s, then reads back CV1–102
(everything except CV8) — `captures/usb/18_reset_all.pcapng`. After the reset the module is in
**analogue mode**, DCC address 1, "blue wire connected → stop", "blue wire disconnected → brake".
`proglink` must never write CV8 by accident: one write wipes the configuration.

| CV | Factory value | Meaning |
|---|---|---|
| 1 | 1 | **DCC accessory address, low byte** — address = CV9 × 256 + CV1, 1–2048 (`usb/137`–`146`: 256 → 00/01, 332 → 4C/01, 2048 → 00/08) |
| 2 | 0 | unknown — not written by any CarManager setting (sweep 2026-09-27) |
| 3 | 25 | **transmitter power** = **flash width**: long flash ≈ CV3 + 1 µs. Confirmed: 25 → 26 µs, 15 → 16.0 µs (`usb/47`, scope `2026-09-27_13`). GUI range ≤ 25 (typing 255 gives 25, `usb/32`); 0 rejected (`usb/31`) |
| 4 | 5 | **extra closeup** (additional approach): short-range flash ≈ CV3 − CV4 + 1 µs = 21 µs (`usb/34`–`36`) |
| 5 | `3E` | **filter mask** (which vehicles obey): bit 3 lane 0, bit 4 lane 1, bit 5 lane 2, bit 2 direction A, bit 1 direction B (factory all on). Changing it rewrites every code block: the mask is bits 1–5 of the first command byte (`usb/37`–`46`) |
| 6 | 0 | **impulse-driven input** time in seconds (analogue mode), 0 = off, 1–255 (`usb/61`–`66`) |
| 7 | 0 | **operating mode**: `00` analogue, `01` pedestrian crossing, `02` rail crossing, `03` traffic control master, `04` DCC, `05` traffic control slave (`usb/49`–`60`) |
| 8 | (not read after reset) | write 8 = factory reset (NMRA convention). Reads back as `08` on a normal connect (`usb/26`), not as an NMRA manufacturer ID |
| 9 | 0 | **DCC address, high byte** (see CV1) |
| 10–24 | stop | slot 1 |
| 25–39 | continue / drive away | slot 2 |
| 40–54 | brake | slot 3 |
| 55 | 0 | unknown — not written by any setting |
| 56 | 0 | **time limit** of slot 1 in seconds (0 = off; `usb/72`, `75`–`77`) |
| 57 | 0 | **+ random up to** (slot 1 time limit), seconds (`usb/78`–`80`) |
| 58 | 0 | **inverted mode** (pedestrian crossing / traffic control), `01` on (`usb/156`–`157`) |
| 59 | 25 | **delay departure** in 0.1 s (pedestrian crossing / traffic control), factory 2.5 s (`usb/151`–`158`, `170`) |
| 60 | 10 | **traffic control phase green / red**, seconds (`usb/160`–`162`) |
| 61 | 10 | **traffic control phase red / green**, seconds (`usb/166`–`168`) |
| 62 | 3 | **traffic control phase red / red** (clearance), seconds (`usb/163`–`165`) |
| 63 | 2 | unknown — not written by any setting |
| 64, 65, 66 | 50, 50, 50 | speed slider value (km/h) of slot 1, 2, 3 |
| 67–69 | 0 | unused |
| 70–101 | 0 | **description** (General info): ASCII, max. 32 characters, `00`-terminated; written on commit (`usb/147`–`149`) |
| 102 | 0 | unused (terminator position after a 32-character description?) |

## Command slots

The module has **three slots of 15 CVs** (slot base 10, 25, 40). In DCC mode the GUI's "red" and "green"
commands are slots 1 and 2. In analogue mode the GUI labels slot 1 "blue wire connected" and slot 3 "blue wire
disconnected" (factory: stop / brake); slot 2 presumably serves the impulse input or time limit.
**Confirmed 2026-09-27:** blue wire connected → slot 1 transmitted (scope `2026-09-27_09`), disconnected →
slot 3 (scope `2026-09-27_10`), matching the GUI labels.

Direct CV writes to a code block take effect **immediately** (no reload, no menu-CV write needed).

**Why three slots:** the slots belong to the module's three input states, which the status LED shows
(handbook / CarManager "General info"):

| Status LED | Input state | Slot | Factory |
|---|---|---|---|
| single blink | control wire not connected | 3 | brake |
| double blink | wire connected (analogue) or "red" command | 1 | stop |
| triple blink | "green" command | 2 | continue / drive away |

Analogue mode only has connected / not connected (slots 1 and 3; slot 2 hidden in the GUI). DCC mode and the
traffic-light / crossing modes deliver red and green (slots 1 and 2). **Measured 2026-09-27:** in DCC mode **without a DCC
signal** (16 V AC supply) the module behaves like analogue mode — blue wire disconnected → slot 3, connected →
slot 1 (scope `2026-09-27_15`, `_16`), although CarManager hides slot 3 in DCC mode. Slot 2 is reached only by a
real "green" accessory command (not yet tested with a command station). Day 1 (2026-09-26) the module was powered by
an ESU LokProgrammer (DCC signal present, DCC mode, address 332) and sent **slot 2** throughout — i.e. with a
DCC signal and no red command received, the module is in the "green" state (likely power-up default; not
excluded that the LokProgrammer sent accessory packets). To confirm: send red / green from a command station.
Open: how a Viessmann traffic light signals "green" on the
blue wire (logic analyzer).

**Mode switches do not copy commands:** changing the operating mode writes only CV7 (`usb/49`–`60`); each GUI
column writes exactly one slot (DCC green → CV25–39 only, `usb/05`–`17`; analogue "disconnected" → CV40–54 only,
`usb/81`–`94`).

| Offset | Slot 1 | Slot 2 | Slot 3 | Content |
|---|---|---|---|---|
| +0 | 10 | 25 | 40 | GUI menu selection (category × 32 + entry) |
| +1 | 11 | 26 | 41 | custom distance (cm) |
| +2…+7 | 12–17 | 27–32 | 42–47 | code 1 block |
| +8…+13 | 18–23 | 33–38 | 48–53 | code 2 block |
| +14 | 24 | 39 | 54 | option bits: `01` short range, `04` turn signal right, `08` turn signal left, `10` ignored by emergency vehicles, `80` set stopping lane (`usb/67`–`94`) |

Factory contents (`18_reset_all`): slot 1 `3E 01` / `BE 01` (stop), slot 2 `3E 00` / `BE 00` (continue),
slot 3 `BE 02` / `BE 02` (brake).

## CV table (observed writes)

Slot 2 = slot 1 + 15, slot 3 = slot 1 + 30. This table lists what CarManager wrote during the session
(module in DCC mode: GUI "red" = slot 1, "green" = slot 2). Addresses marked "assumed" follow from the slot
layout but were not written in any capture; the factory dump above shows all of them exist.

| Slot 1 CV | Slot 2 CV | Observed values | Meaning | Affects IR? | Status / evidence |
|---|---|---|---|---|---|
| 1 | | 76 | DCC address low byte (332 = 0x14C) | no | read (`02`), matches GUI |
| 10 | 25 | `20`, `21`, `22`, `25`, `40`–`47`, `60`–`67`, `A0`–`AB`, `C0` (+ transients `1F`, `5F`, `9F`, `BF`) | **GUI menu selection** = category × 32 + entry (categories: 0 none, 1 basic, 2 traffic lanes, 3 magnetic, 4 macros, 5 direct function, 6 direct speed, 7 rolling highway) | no (GUI memory) | red: `03`; green: `05`, `07`, `09`–`17` |
| 11 | 26 | `1E` (30) | custom distance in cm (GUI value; the distance itself is also placed in the code block) | via code block | green: `08` |
| 12–17 | 27–32 | e.g. `03 3E 01 AF 00 00` | **code 1** block (header + stored bytes + padding) | **yes** | red: `03`; green: `05`–`17`; matches scope |
| 18–23 | 33–38 | e.g. `C3 41 FE 7F 00 00` | **code 2** block | **yes** | red: `03`; green: `05`–`17`; matches scope |
| 24 | 39 | `21`, `61`, `41` | option bits (GUI checkboxes). Seen: `21` after ticking *short range* (stop); `61` after Main lights *off* (permanent); `41` after unticking *permanent* | *short range* changes flash width without touching the code blocks, so at least that bit is read by the module | green: `06`, `13` |
| 64 | 65 | `00`, `05`, `0A`, `0F`, `14`, `32`, `6E` | speed slider value in km/h (GUI memory; the speed is also in the code block) | via code block | green: `10`, `11` |

Code block format (header bits, inversion, CRC): see [ir-protocol.md §4](ir-protocol.md).

## What this means for `proglink/`

- Writing a slot's code blocks (slot 1: CV12–23, slot 2: CV27–38, slot 3: CV42–53) with valid blocks is
  enough to make the module transmit any code — including speed values and command bytes CarManager does not
  offer. Two different codes in one slot (header bit 5 clear) should alternate on the LED, as stop/continue do;
  not yet tested with arbitrary pairs.
- The menu CVs (10/25/40), option CVs (24/39/54) and slider CVs (64/65/66) are mostly CarManager's own memory
  of the GUI state; keep them consistent if CarManager should still display the configuration correctly.
  Exception: the *short range* bit in the option CV changes the output (flash width).
- Never write CV8 (factory reset).
- **Firmware version gate:** `proglink` reads `Ver` and writes only to tested module firmware (so far 1.06);
  on any other version it warns and stays read-only. A future Viessmann update could reject or mishandle
  self-built blocks; users of the expert mode should not update the module until the new version has been
  regression-tested on the bench (reset → sweep → scope, `docs/method-cv-sweep.md`). Always take a full CV
  backup (JSON) before writing.
- **Stay out of update mode:** firmware updates run over the same USB → 8401 → serial link, probably via a
  bootloader entered by a special command or the CP210x control lines. `proglink` must hold DTR and RTS low
  exactly as CarManager does (`usb/26`), never toggle them, and send only the known commands (`Who`, `Ver`,
  `cr`, `cW`) — no probing of unknown commands. Update traffic is never captured or analysed (brief non-goal:
  it would contain the firmware image).
- `proglink` needs COM3, so CarManager must be closed while it runs. Options: a PowerShell serial↔TCP bridge
  on the laptop (no install), or Python on the laptop. First runs should be read-only (`Who`, `Ver`, `cr`).

## Not yet seen

- CV2, CV55, CV63, CV102: not written by any CarManager setting of firmware 1.06 (full sweep 2026-09-27)
- A CV dump of a configured module (the factory dump exists: `18_reset_all`)
- CarManager backup file format (Backup tab) — brief §4 suggests checking it early
