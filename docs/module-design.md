# Module design — decisions and requirements so far

Brief Stage 4 deliverable, started 2026-09-26 from the session discussion. Nothing is built yet; this records
decisions and the reasoning behind them. Protocol details: [ir-protocol.md](ir-protocol.md).

## What the original offers (context)

- The IR Mini combines the IR output with a DCC accessory decoder and the automatic traffic-light and
  single-lane logic on one small module; two outputs and three command slots are what it offers.
- From the measurements: CarManager computes finished code blocks (CRC, flag) and the module only plays them
  back; fixed three slots × two blocks; GUI state stored in CVs.
- CVs follow the NMRA DCC convention (configurable from any command station), but make configuration rigid:
  fixed 8-bit registers, one CV per ~9 ms round trip, no atomic updates (a half-written block can be
  transmitted briefly; the CRC makes the vehicle discard it), no validation.
- Consequence: the module protocol is unlikely to change; the bigger risk is vehicle firmware (brief §6).

## Scope decision (2026-09-28)

- **Infrastructure only, no vehicles.** The project builds modules (IR transmitter "Maxi", scanner, network
  integration) that complement Viessmann's vehicles; it does not build or clone vehicles.
- **"Maxi" IR module target:** classic ESP32, **8 independent output channels**, **many commands per channel**
  (playlists), and **several channels active at once**; per-command flash width; network configuration.
- **Numbered presets** (1–64, with labels) selected per channel by the master; see
  `firmware/transmitter/PROFILE-CARMOTION.md` (0.2 design).
- **No physical inputs** for now: control only over the network (TrainMaster as the single master).

## Audience

- **Primary: power users** — network control, free commands and parameters, playlists, telemetry,
  TrainMaster / MRRoIP integration. This is the group the original cannot serve.
- Secondary, optional: users who want physical controls and no PC (no programming cable needed). Buttons /
  presets are a convenience layer, not the design driver.
- Default behaviour out of the box should match the IR Mini factory configuration
  (`captures/usb/18_reset_all.pcapng`: input active → stop, inactive → brake, continue in slot 2), because
  that is what vehicles in the field are used to.

## Transmitter

- **Playlist per output:** each LED output has a list of frames sent round-robin (e.g. speed + lights +
  lane). The firmware adds CRC and flag and generates the timing; entries are just command bytes.
- **Timing budget:** ~3.4 ms per 3-byte frame + ≥1.5 ms idle ≈ 200 frames/s per LED. With N entries each is
  repeated ~200/N times per second (the original sends one code ~40×/s).
- **Per-entry flash width** (21 µs / 26 µs) to reproduce *short range*; optional current control on top.
- **Randomised inter-frame gap**, like the original, so neighbouring outputs/devices don't collide on every
  frame.
- **Mixed protocols** on one output are allowed as separate playlist entries (NEC remote codes, DCC over IR),
  never overlapping.
- **Separate-channel pairs** (like *brake + stop*) are just two outputs with different playlists.
- **Rules:** prefer absolute commands; no toggles (`BE 1C`) in repeating playlists; no contradictory commands
  (brake + speed) on one output.
- **MCU:** classic ESP32, one RMT channel per output (8), 1 µs resolution; a frame = 30 RMT items; the TX-done
  callback loads the next playlist entry. Timing is hardware-generated, independent of Wi-Fi load.
- **Verification before any vehicle test:** our frames must match the captured originals flash for flash
  (`tools/irframes.py` on a scope capture of our module).

## Configuration

- **Primary: structured configuration over the network** (JSON / MQTT), one object per output; validated and
  swapped in atomically between two frames; versioned and exportable (backup, reproducibility).
- Optional: small DCC CV window (address + presets) for command-station users.
- Optional, later: emulate the IR Mini serial protocol so CarManager can configure the device. Technically
  simple, but copies the CV limitations and would identify the device to CarManager as a Viessmann module —
  weigh before doing.

## Inputs and automation

- Physical inputs for users without network: contact / track sensor / traffic-light output, and an
  **isolated** DCC accessory input (see the ground-loop incident in README).
- Traffic-light and single-lane automation belongs in TrainMaster (layout-wide view); a local fallback in the
  device is optional.

## Vehicle questions that decide the limits

To be answered with one spare vehicle (backed-up configuration), in this order:

1. Single command from our module (e.g. lights on) — does it work at all?
   **Yes — 2026-10-07:** a truck stopped on `stop` from the ESP32-C3 (IO10/IO1, SMD 940 nm LED 3216 directly on
   the GPIO via 100 Ω, ≈19 mA; car held/driven within a few cm). Vehicle firmware not recorded (fleet was on 1.48
   in April 2026); no scope capture (scope off). Same session: `continue` drove it off again, and with macro 1 on
   IO10 / macro 2 on IO1 the truck blinked left / right depending on which LED it was held at, so both channels
   work independently. Main lights `function_on 0` on IO10 / `function_off 0` on IO1 switched the lights on / off
   at the respective LED.
2. Two alternating harmless commands (lights + indicator) — does the car execute both?
   **Yes — 2026-10-07**, same truck and setup as test 1: ch1 playlist `function_on 0, macro 1` switched on both
   main lights and the left indicator; ch2 `function_off 0` switched the lights off. Reaction time per command
   not measured.
3. Speed + lights — the practical case.
   **Yes — 2026-10-07**, same truck and setup, truck driving past the LEDs: ch1 `speed 30, function_on 0` slowed
   it and switched the lights on, ch2 `speed 80, function_off 0` sped it up and switched them off, each in one
   pass. 80 km/h is too fast for the layout; use ≤50 for further tests.
   Functions checked on the truck (2026-10-07, held at the LED with `stop` + one function, switching every 2 s):
   `function_on` / `function_off` 0 main lights, 1 high beam, 2 roof beacons, 3 left / 4 right indicator, 5
   hazard — all switch on and off.
4. Longer playlists and higher vehicle speed — minimum repeats per command for reliable reaction.
   **2026-10-07, 4 entries: yes.** ch1 `speed 30, function_on 0/1/5` and ch2 `speed 50, function_off 0/1/5`
   (lights, high beam, hazard): the driving truck executed all four in a pass at each LED.
   **8 entries: not reliable when driving.** Same commands plus 4 fillers. First run with fillers `function_off
   2/3/4/8` (turn signals off — possibly conflicting with hazard, not separated); second run with neutral fillers
   `function_off 2/10/11/8`: driving past, hazard did not come on (one left blink seen); **truck held still in
   front of ch1: hazard came on.** So a moving truck receives too few repeats of each entry with 8 entries (≈70
   frames/s per channel → ≈9/s per entry) and the weak direct-drive LED (beam a few cm). Limit with this LED: 4
   entries. To recheck with the MOSFET driver (longer beam = more time in range).
5. Speed steps above 22, 4–5-magnet patterns, flash width vs reception distance.
6. Speculative: several command pairs in one frame (the header length field allows up to 7 bytes).
