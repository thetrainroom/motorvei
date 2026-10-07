# CarMotion IR — Reverse Engineering & Open IR Module

Project brief for Claude Code. Read this first; it defines scope, hardware,
data formats and the build order.

> **Original brief from 2026-09-26** (only the wording adjusted for publication). Many open questions
> below are answered since; the current state is in `CLAUDE.md` and `docs/README.md`.

---

## 1. Goal

Reverse engineer the IR protocol used by Viessmann CarMotion (H0 model road
traffic system), then build an open, more capable replacement for the
**Viessmann 8403 IR Mini** and **8406 IR Scanner**.

The original modules are limited:
- 2 IR channels, a handful of commands each, configured only via Viessmann's
  CarManager GUI (Windows, no API, no scripting).
- No usable data bus between modules. Each module reacts only to what happens
  at its own spot. No layout-wide view, no logging, no link to control software.

Our replacement targets:
- 8+ IR transmit channels per module, freely assignable commands.
- Free parameter values (e.g. arbitrary speed), not just the presets the GUI offers.
- Vehicle detection (scanner function) with telemetry published over the network.
- Integration with MRRoIP / TrainMaster so the whole road network can be
  controlled and observed centrally.

**Non-goals:** no firmware dumping, no decompiling CarManager, no circumvention
of any protection. Everything here is observation of signals on our own hardware.
Do not add code that touches Viessmann firmware images or binaries.

---

## 2. Current state of knowledge

Established from documentation and forum research (all unverified by measurement
unless marked CONFIRMED):

- **Wavelength: CONFIRMED.** Vehicle IR sensor peaks at 940 nm (50% sensitivity
  at 730 nm and 1100 nm). Per Viessmann support. Use 940 nm for all IR parts.
- The handheld remote appears to send a command in byte 1 only, with a second
  ("user") byte always zero. Source: another hobbyist's write-up, not verified here.
- Working hypothesis: the module sends **16-bit codes**; CarManager stores the
  code in the module and the module simply plays it out. Evidence: a CarManager
  release added new sound commands for the IR Mini with no IR Mini firmware update.
- Some commands carry a **parameter** (e.g. a speed value), likely in the second
  byte or the low bits of the first.
- Vehicles **transmit** data themselves: the 8406 IR Scanner reads vehicle
  category, length, speed, battery level and indicator direction from passing
  vehicles. No query is sent — the scanner is receive-only.
- Vehicles have **compatibility modes** (OpenCar / DC-Car) that accept standard
  DCC over IR, enabled in CarManager. Native mode uses a proprietary protocol.
- **Unknown, to be measured first:** whether the signal uses a modulated carrier
  (e.g. 38 kHz) or plain on/off keying. This determines everything downstream.
- **Unknown:** how the module dims its output (carrier duty cycle vs. LED current).

Vehicle firmware versions in the wild range from ~1.04 to 1.48 (current). Most
users never update. **Support for old firmware matters more than tracking new
releases.** Record the firmware version of every vehicle used in a capture.

---

## 3. Hardware available

| Item | Notes |
|---|---|
| Viessmann 8403 IR Mini | Working unit, configurable via CarManager |
| Viessmann 8403 IR Mini (damaged) | One LED torn off — drive pads exposed, ideal probe point |
| Viessmann 8401 programmer + cable | USB to PC, programming link to module |
| CarMotion vehicles | Firmware 1.48, updated April 2026 |
| Rigol DS1000Z oscilloscope | Borrowed. 4ch, deep memory, SCPI over LAN port 5555 |
| USB logic analyzer 24 MHz, 8ch | sigrok / PulseView |
| ESP32 DevKit (classic, 38-pin) | Capture rig + 8-channel prototype |
| ESP32-C3 / C6 / S3 boards | For small location modules and the scanner variant |
| VS1838B receivers, IR LEDs 940 nm, MOSFETs, etc. | See parts list |
| Old Windows laptop | Runs CarManager; the programmer lives here |

`rigol_capture.py` already exists: pulls deep memory off the scope over LAN and
writes time/volts CSV. No dependencies. Use it as the capture backend.

---

## 4. Build order

Each stage gates the next. Do not start a later stage before its prerequisite
produces verified output.

### Stage 1 — Physical layer (manual, with tooling support)

**Question: how is a bit represented?**

Capture one IR frame on the scope from the damaged module's LED pads (fit a
1 kΩ resistor or a spare IR LED as a load — an open output may float and produce
garbage). Then determine:

- Carrier present? If yes: frequency, duty cycle. If no: plain OOK.
- Bit encoding: pulse-distance (NEC-style), pulse-width, Manchester, other.
- Frame structure: preamble, bit count, stop bit, repeat interval.
- Voltage levels and LED current (measure across a 10 Ω series resistor).
- How intensity/dimming settings change the waveform.
- Relationship between the two LED channels in each operating mode — capture
  both simultaneously.

**Deliverables:**
- `docs/physical-layer.md` — findings, with annotated screenshots
- `decoder/` — CSV or sigrok capture in, bit string out. This is the tool
  everything downstream depends on.

**Note:** if there is no carrier, dimming must be done by current control, not
by shortening pulses — shortening would corrupt the bit timing. If there is a
carrier, duty-cycle dimming is safe. This decides our module's LED driver design.

### Stage 2 — Programming link & CV map

**Question: what does CarManager write to the module?**

Two parallel methods, first session does both, then keep whichever is cleaner:

- **USB capture:** Wireshark + USBPcap on the Windows laptop, filtered to the
  programmer's device address.
- **Logic analyzer on the programming cable:** likely UART; PulseView decodes it
  directly, without USB framing. Tap via the Viessmann 8444 extension cable so
  the module isn't touched.

**Capture discipline (this is what makes the data usable):**
- One variable per capture. Start capture → change exactly one setting → save to
  module → stop capture.
- Baseline captures first: a read with no changes, and a save with no changes.
  These reveal the handshake/polling present in every capture.
- Bracket parameter values: min, max, and two points between.
- Filename encodes the change: `cmd_slot1_brake_speed30.pcapng`
- Maintain `captures/log.csv`: filename, what changed, value in CarManager.

**Deliverables:**
- `docs/cv-map.md` — table: CV number | observed values | meaning | where it
  appears in CarManager | affects transmitted IR? (yes/no)
- `tools/usb-diff/` — diff two captures, report changed bytes
- `proglink/` — library that writes CVs to a module directly, bypassing
  CarManager. **This is the key that unlocks Stage 3.**

Note: CarManager exposes "Direkteinstellung" (direct CV access) and can save
module backup files. Inspect a backup file early — if the format is readable,
generating configurations programmatically may be easier than either capture
method. Save two backups differing in one command and diff them.

### Stage 3 — Automated command sweep

**Question: which code produces which command?**

Once `proglink/` can write CVs and `decoder/` can read frames, the loop is:

```
for config in sweep_plan:
    proglink.write_cv(config)
    trigger_module()          # relay/transistor on the module's control input
    capture = scope.capture() # or logic analyzer via sigrok-cli
    bits = decoder.decode(capture)
    db.append(config, bits)
```

**No vehicle in the loop for the first run.** This is pure electronics:
write, trigger, capture, decode, store. Expect ~1–2 s per iteration, fully
unattended overnight.

**Sweep strategy — sparse, not exhaustive.** 16 bits is 65,536 combinations,
but the space collapses fast:
- Start with every command CarManager offers — these are known-good codes.
- Map the neighbourhood around them to find the command/parameter split.
- For a parameter, test ~5 values across the range and verify linearity rather
  than testing all 256.
- Coarse scan of the remaining space to find undocumented codes; skip blocks
  that produce nothing.

**Deliverables:**
- `data/commands.sqlite` — config, CV values, raw frame, decoded bits, code
- `docs/command-table.md` — human-readable command reference
- `orchestrator/` — the sweep runner

### Stage 4 — Own transmitter

Build the IR Mini replacement. Hardware and firmware.

- **MCU:** classic ESP32 for 8 channels (8 RMT channels = 8 independent
  hardware-timed outputs). ESP32-S3 if the scanner function is on the same board
  (4 dedicated TX + 4 dedicated RX). ESP32-C3/C6 for small per-location modules.
- **More channels than RMT outputs:** one RMT output + per-channel enable
  MOSFETs, time-multiplexed. Trucks sit in the beam for *seconds*, so sharing is
  nearly free. 74HC595 shift registers extend this to dozens of channels.
- **LED driver:** MOSFET per channel. Build a current sink (MOSFET + op-amp +
  sense resistor) so dimming works regardless of what Stage 1 finds; add
  duty-cycle dimming on top if a carrier exists.
- **Beam control is the addressing.** IR range and angle determine which vehicle
  receives a command — no vehicle addresses needed. Adjustable LED current
  matters more than any software feature.
- **Repeat commands continuously** while a channel is active, so a moving vehicle
  catches a complete frame.
- **Prefer absolute commands** ("set speed to X") over toggles — a vehicle will
  receive the same frame several times.

**Verification:** our module's output must be byte-identical to captured original
frames before any vehicle test. Compare captures, not behaviour.

**Deliverables:**
- `firmware/transmitter/` — ESP32 firmware
- `hardware/` — schematic, then JLCPCB board once settled
- `docs/module-design.md`

### Stage 5 — Scanner

Vehicles broadcast their own data. Decode it.

- Point a receiver at the rear of a running vehicle, log raw frames.
- Change one thing at a time to identify fields: vehicle category, length,
  trailer attached/detached, speed, indicator, battery level (log while the
  battery drains).
- Watch for multiple frame types (distance-control beacon vs. full data frame).

**Deliverables:**
- `docs/vehicle-telemetry.md` — field map
- `firmware/scanner/` — receive + decode + publish

### Stage 6 — Network integration

Publish detections and accept commands over MRRoIP so TrainMaster gets a live
view of road traffic. This is the capability the original system structurally
cannot offer: Viessmann's modules only act locally.

Combined transmit + scan in one module enables position-based *and*
identity-aware behaviour: broadcast by default ("whoever is at this LED"), but
conditional on what the scanner just saw ("if it's a bus, send the stop command").

---

## 5. Conventions

- **Language:** Python for tooling and analysis. Rust or C++ for ESP32 firmware
  (match TrainMaster's stack where sensible).
- **All measurements are data, not assumptions.** Every claim in `docs/` cites
  the capture file it came from.
- **Captures are immutable.** Never edit a capture; derived data goes elsewhere.
- **Record firmware versions** with every vehicle-related capture.
- **Everything reproducible.** Someone with the same hardware should be able to
  rerun a sweep and get the same table. This is also what makes the work
  presentable on video.

---

## 6. Risks & open questions

- **Viessmann could change the protocol** in a firmware update. Already happened
  once: v1.44 reportedly broke DC-Car compatibility mode at IR stop points.
  Mitigation: OTA updates on our modules, a test vehicle on a known version,
  automated regression run against new vehicle firmware.
- **Most users never update**, which cuts both ways: our modules must tolerate
  ancient vehicle firmware, and a Viessmann protocol change would reach few users.
- **Sweeping unknown codes may hit configuration or reset commands.** Once
  vehicles enter the loop (Stage 5+), use a spare vehicle with a backed-up
  configuration. Never publish codes that could brick a vehicle.
- **Undetermined:** whether CarManager's backup format is readable. Check early —
  it could shortcut Stage 2 substantially.
- **Undetermined:** whether the programming link is plain UART. If it's something
  more exotic, the USB capture route carries more weight.

---

## 7. Immediate next actions

1. Fit a load (1 kΩ or spare IR LED) to the damaged module's LED pads.
2. Single scope capture of one frame, both channels. Answer: carrier or not.
3. Save two CarManager module backups differing in one command; diff them.
4. From (2), write `decoder/`.
