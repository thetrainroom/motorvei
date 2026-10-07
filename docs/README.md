# CarMotion IR — findings

State after the first measurement session (2026-09-26). Project scope and build order: `../PROJECT-BRIEF.md`.

## Summary

1. **Physical layer** ([physical-layer.md](physical-layer.md)): no carrier. Each bit is one IR flash (21 µs or
   26 µs); the time to the next flash is the bit: 90 µs = 0, 130 µs = 1, 169 µs = sync/end. Frames are
   ~3.4 ms and repeat continuously with a randomised gap.
2. **Command reference** ([command-table.md](command-table.md)): every command with its vehicle effect, how to
   enter it in a playlist, and its bytes.
2b. **Frame format** ([ir-protocol.md](ir-protocol.md)): `SSS flag bytes… CRC S`. The flag says whether the
   bytes are sent inverted (chosen to minimise 1-bits); the CRC is CRC-8/MAXIM. Confirmed on every frame and
   every code block captured.
3. **Commands**: byte 1 `BE`/`3E`, byte 2 = type + value — speed (`20 + km/h÷5`), magnet pole patterns
   (`40`–`47`, S=1/N=0), traffic lanes (`17`–`1E`), functions (`60`–`6B`, `+10` permanent, `BE` on / `3E`
   off), stop/go/brake/macros/reverse.
4. **Range**: *short range* shortens the flash (26 → 21 µs) at unchanged voltage; *custom distance* is a cm
   byte inside the frame.
5. **Programming link** ([cv-map.md](cv-map.md)): CP210x serial, readable text protocol (`Who`, `Ver`,
   `cr`/`Cr` read CV, `cW`/`CW` write CV). CarManager writes finished IR code blocks into three command slots
   (CV10–24, 25–39, 40–54); the module just plays them back — so any code can be sent by writing CVs.
   Factory reset = write 8 to CV8; a full factory CV dump is in `captures/usb/18_reset_all.pcapng`.
6. **Other protocols** ([related-protocols.md](related-protocols.md)): the handheld remote is NEC (public);
   DCC over IR exists for DC-Car/OpenCar compatibility (fixed again in vehicle firmware 1.48).
7. **Full CarManager sweep** ([method-cv-sweep.md](method-cv-sweep.md)): every IR Mini setting mapped to its
   CVs (2026-09-27, `captures/sweep.csv`); transmitter power = flash width (CV3 + 1 µs); byte 1 of each command
   is a recipient filter (lane / direction) plus sub-type bits.
8. **Own transmitter, milestone 1** (`firmware/transmitter/`, ESP32-C3): C encoder reproduces every captured
   CarManager block (37 host checks); RMT output verified on the scope — frames identical to the original's. **Milestone 2:** MRRoIP endpoint
   (profile `carmotion`, `firmware/transmitter/PROFILE-CARMOTION.md`): playlists per channel set over the network,
   verified on the scope; MRRoIP core probe passes.
9. **Module design** ([module-design.md](module-design.md)): playlists per output, network configuration first,
   power users as primary audience, factory-compatible defaults; open vehicle questions.

Brief milestones: Stage 1 physical layer — done (decoder in `tools/irframes.py`, to be promoted to
`decoder/`). Stage 2 — CV map for the command slots done; `proglink/` not written yet. The command table
already covers most of what Stage 3's sweep was meant to find.

## Firmware and software versions (Viessmann download page, checked 2026-09-28)

| Item | Latest | Ours (tested) |
|---|---|---|
| CarManager | 1.36 (Windows x64/x86, **macOS 13+**; no Linux) | **1.32** on the Windows laptop — all captures and the settings sweep were made with 1.32 |
| IR Mini 8403 | **1.08** (2026-04-13; file downloaded by the owner, not installed) | **1.06** (confirmed by `Ver` = `01 06` in every capture) — all findings apply to 1.06 only |
| Vehicles | 1.52 (2026-09-28) | 1.48 (updated April 2026); first vehicle tests 2026-10-07 (`module-design.md`), truck firmware not recorded |
| InduktivCharger 8408 | 1.14 | — |
| Turnout (Abzweig) | 1.06 | — |
| IR Scanner 8406 | 1.00 | — |

Source: <https://viessmann-modell.com/carmotion/software-firmware/>. Do not update the IR Mini or the test vehicles
before the plan decides it (findings and `proglink` version gate are for IR Mini 1.06). CarManager for macOS
means the bench could run on the Mac directly (CarManager + USB capture), without the Windows laptop.

## CarMotion devices

| Art. | Device | Relevance |
|---|---|---|
| — | Vehicles | main target: IR receive/send, magnet sensor, programming header (charging + data) |
| 8403 | IR Mini | largely decoded (this session) |
| 8406 | IR Scanner | 2 IR receivers (look at the vehicle's rear), rule engine (conditions → actions, set in CarManager), 2 potential-free outputs, IR output for commands, EasyChain; vehicle telemetry can also be decoded directly with a photodiode (manual: [8406.pdf](https://viessmann-modell.com/media/1e/ae/f3/1787128103/8406.pdf)) |
| 8408 | InduktivCharger | check for programming header / CVs |
| 8401 | Programmer | USB ↔ 3×2 header (CP210x), charges vehicles |
| 8402 | IR remote | NEC, codes known ([related-protocols.md](related-protocols.md)) |
| 8441 | Reed contacts | plain switch contact → input for our device |
| 8445 / 8446 / 8448 | manual turnout / motor / both | the drive has firmware: single-wire control needs SW ≥ 1.06 (8406 manual) → observe the single-wire control with a logic analyzer |
| 8442 / 8443 | IR LEDs | recommended LEDs for the 8406 IR output |
| — | EasyChain | Viessmann connection system supported by the 8406; check whether it carries data between modules |
| 8431 | Magnets | ordinary permanent magnets; pole orientation is the information |
| 8444 | Extension cable | measuring point for the programming link |

## Captures

- `captures/scope/*.bin|.json` — raw Rigol memory (CH1, 10 MSa/s, 1.2 s) + settings/preamble
- `captures/usb/*.pcapng` — USBPcap captures of the 8401 programmer
- `captures/log.csv` — one row per capture: what changed, CarManager setting, firmware
- All captures are read-only. No vehicle was in the loop; vehicle firmware is n/a for all of them.

## Tools

| File | Purpose |
|---|---|
| `rigol_capture.py` | Rigol DS1000Z SCPI driver: settings, single trigger, full-memory read, CSV + JSON, screenshots |
| `tools/grab_raw.py` | single-shot raw capture of CH1 → `.bin` + `.json` |
| `tools/irframes.py` | decode IR frames from a raw capture (rising-edge timing, CRC check, flash width class) |
| `tools/cmdcode.py` | decode a CarManager code block, or encode command bytes → CRC, block, on-air bits |
| `tools/usbserial.py` | extract the CarManager ↔ module serial stream from a USBPcap file (needs tshark) |
| `tools/ssh_config` | SSH access to the CarManager laptop: `ssh -F tools/ssh_config carlaptop` |
| `setup/setup-ssh-key.ps1` | one-time key setup on the laptop |

Python 3.12 venv in `.venv` (`vncdotool` for driving CarManager over VNC; everything else is stdlib).

Reproduce a result, e.g.:

```
.venv/bin/python tools/irframes.py captures/scope/2026-09-26_13_green_speed50_short_range
.venv/bin/python tools/usbserial.py captures/usb/11_green_speed_sweep.pcapng
.venv/bin/python tools/cmdcode.py --encode BE 2A
```

## Bench setup notes

- Scope on the LAN, SCPI port 5555: set `SCOPE_IP` for `tools/grab_raw.py`. Windows laptop with CarManager:
  SSH via `tools/ssh_config` (copy `tools/ssh_config.example`), VNC on port 5900 via `BENCH_VNC=<ip>::5900`.
- USBPcap captures must be started from a session that stays open for the whole capture (Windows OpenSSH
  kills child processes when the session ends); Wireshark must be closed (it locks the capture device).
- A ground loop between a DCC source (LokProgrammer) and the mains-earthed scope caused a short before this
  session. Do not connect scope ground to anything fed from DCC; use an optical pickup or USB isolator.

## State of the bench at end of session (2026-09-26)

- The IR Mini was **factory reset** at the end of the session (`usb/18_reset_all`): analogue mode, DCC address
  1, stop / continue / brake in slots 1–3. Its previous setting (DCC, address 332) must be restored in
  CarManager if the module goes back on the layout.
- CarManager is running on the laptop; USBPcap captures are started over SSH (see bench notes).
- Scratchpad copies are gone after the session; everything needed is in `captures/`, `tools/`, `docs/`.

## Plan for the next session

**Bring from the train room:** lab power supply and resistor box (1 kΩ load for the torn-off LED's cable end;
10 Ω for LED-current measurements).

**Bench setup first:** power the IR Mini from the lab power supply (floating DC output, earth terminal not
bridged to minus, current limit ~200–300 mA, voltage within the IR Mini spec) instead of the 16 V AC Märklin
transformer. **One** scope ground clip on the module's internal GND (programming-header GND pin — find it by
continuity to the USB plug shell), never on transformer terminals or LED wires. Probe tips on one wire of each
LED cable (load across the torn-off LED's cable end). Record probe positions in `captures/log.csv`.

- Per-LED test: restore *brake + stop* in CarManager, capture CH1 + CH2. Per the handbook the **stop LED has a
  white marking on its cable** — check which cable (torn-off LED on CH1 or intact LED on CH2) has it. Result so
  far: the torn-off LED has the white marking (stop LED) and transmits **code 1** (`3E 40` in brake + stop; the
  slow decay is its own unloaded output), the intact LED transmits code 2 (brake). Confirm with a load across
  the torn-off LED's cable end.
- Transmit power test: change CV3 (factory 25) to e.g. 15 and check that long flashes shrink to ~16 µs;
  CV3 − CV4 → short flash. Then repeat with the self-built pair
  (speed 30 / lights on) to confirm arbitrary commands per LED.

0. Plug the 8401 programmer into the Mac (CP210x → `/dev/cu.*`), find the baud rate with a **read-only**
   `Who` probe at **38400 baud, 8E1** (known from `usb/26`). If the module answers `ViessStop`, start `proglink/` natively on macOS
   (pyserial, CV8 guarded, read-back verify, JSON backup/restore) — first step towards an open,
   cross-platform CarManager replacement for the IR Mini (no firmware updates, no Viessmann branding).
1. Switch the module back to DCC mode while capturing USB → identify the operation-mode CV (among CV2–7).
2. *Finetuning* page, one setting at a time → CV59–63.
3. Remaining categories: rest of Basic commands, Macros, Rolling highway, "no command"; the option
   checkboxes (turn signal, emergency vehicles, stopping lane, time limit, impulse-driven input).
4. **Dual-channel test with arbitrary commands:** write two blocks with header bit 5 set into the active
   slot via *Direct configuration*, e.g. code 1 `E3 41 D9 DF` (speed 30 km/h → LED 1) and code 2
   `63 BE 70 26` (main lights on permanent → LED 2); scope CH1 + CH2 on both LED pads. If each LED sends its
   own command, "separate channels" works with any pair, not only brake + stop (a CarManager restriction).
   Also test two blocks with bit 5 clear (should alternate on both LEDs).
5. Device side: `encoder/` library with a test against all captured code blocks; first ESP32 RMT playlist
   sketch; compare its output with the original on the scope.

### Programming connector (3×2 header)

The 8401 has USB on one side and a custom 3×2 pin header on the other; the same header fits every CarMotion
device (modules and vehicles). A similar cable only charges. Plan:

1. Unplugged: continuity test of both cables → which pins are power (present in the charging cable), which
   are data / control. Record the connector pinout.
2. Plugged into the IR Mini on USB power only (no DCC, no scope): multimeter voltages of every pin vs GND →
   logic level (3.3 / 5 V) and supply voltage.
3. Logic analyzer (sigrok / PulseView) on TX / RX via the 8444 extension cable → baud rate, frame format,
   startup sequence, without USB framing. Analyzer GND only to header GND, never with DCC connected.
4. Charging cable and programmer use the same socket. The 8401 **charges vehicles** over the programming
   cable (power + data together), so a charging spot built around an ESP32 could charge and read / configure
   a truck through the same header. The 8401 does **not** power the IR Mini's LED outputs: the module needs
   its own supply to transmit (CarManager's "do not disconnect its power supply" refers to that). Powering
   module LEDs from the programming cable is not worth designing for.
5. Goal: ESP32 talking to the header directly (configure modules / read vehicles over the network). Extra pins
   (reset / programming mode) are identified only, never driven — firmware updates stay out of scope.

### Later: vehicles and other devices

- **Trucks:** read-only first — connect a truck in CarManager with USB capture running, open pages, save
  nothing (device name via `Who`, firmware, CV dump, how the vehicle is connected). Then a CarManager backup of
  the truck; changes only one at a time on a spare truck. Record vehicle firmware in `captures/log.csv`.
  No vehicle firmware updates through our tools.
- **Devices borrowed from the dealer** (with their explicit OK; backup before, restore after): priority the
  **8406 IR Scanner** (decodes vehicle telemetry → shortcut for Stage 5), then traffic-light / single-lane
  controllers. Same method: programming link + IR observation, no opening, no firmware dumping.

## Open questions

1. Sweep the remaining categories: rest of Basic commands, Macros, Rolling highway, "no command".
2. Options: turn signal, ignored by emergency vehicles, set stopping lane, time limit; exact option-CV bits;
   header bit 6.
3. Meaning of byte-1 bit 7 (`BE` vs `3E`) outside functions; the placeholder codes `BE FF` and `7A C4 AB`.
4. `proglink/`: serial access without CarManager at **38400 8E1**; guard CV8 (factory reset); write only to
   tested module firmware (1.06), read-only otherwise; JSON backup before every write; DTR/RTS held low.
5. Measure LED current / optical output; record the probe load.
6. Promote `tools/irframes.py` to `decoder/`, add an encoder test against all captured blocks.
7. First vehicle tests (Stage 5): speed steps > 22, 4–5 magnet patterns, flash-width vs range — with a spare
   vehicle and backed-up configuration.
