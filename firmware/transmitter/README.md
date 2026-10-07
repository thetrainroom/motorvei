# CarMotion IR transmitter (ESP32)

Open IR module for Viessmann CarMotion vehicles ("Maxi"): playlists of CarMotion commands per output channel,
generated with RMT, later controlled over MRRoIP (`../../../mrroip`). Protocol: `../../docs/ir-protocol.md`.

**Milestone 1 — done 2026-09-28:** RMT output with fixed test playlists (now replaced by milestone 2). Verified on the scope
(`captures/scope/2026-09-28_01_…`): every frame decodes to the original module's code with the original flash
width (26.0 / 21.0 µs), both channels in parallel, ~70 frames/s per channel.

**Milestone 2 — done 2026-09-28:** MRRoIP endpoint, profile `carmotion` ([PROFILE-CARMOTION.md](PROFILE-CARMOTION.md)).
Playlists per channel as objects `ch1`/`ch2`, stored default playlists as the autonomous programme, flash width and
gaps as parameters. Verified: playlists set with `mrroip.Device` appear on the scope code-exact
(`captures/scope/2026-09-28_02_…`); MRRoIP core probe 23/23 applicable tests pass. Wi-Fi credentials are the ones
stored in NVS namespace `mrroip` (shared with other MRRoIP firmware on the same board).

```python
import mrroip                                   # ../../../mrroip/src
dev = mrroip.Device("carmotion.local")          # or the IP
dev.control(mode="transmit", objects={"ch1": [{"cmd": "speed", "arg": 30}], "ch2": [{"cmd": "stop"}]})
dev.set_config(default_ch1="speed:50,stop", persist=True)   # stand-alone playlist
dev.control(mode="release")                     # back to the stored playlists
```

**Milestone 3 — done 2026-09-28:** profile 0.2, numbered presets. The user defines up to 64 presets (label +
playlist) and per channel a name and default preset; a master sets a channel to a preset by number. Verified on
the scope (`captures/scope/2026-09-28_03_…`); presets survive a reboot. Since 2026-09-29 the GUI shows channel names and preset labels ("3 — bus_stop").

```python
dev.set_config(presets=[{"label": "stop", "playlist": "stop"},
                        {"label": "bus_stop", "playlist": "brake,function_on:4/s"}],
               channels=[{"name": "Bus stop Nord", "default_preset": 2}, {"name": "Speed zone", "default_preset": 1}],
               persist=True)
dev.control(mode="transmit", objects={"ch1": 2})     # ch1 -> preset 2; an array is a direct playlist
```

The milestone 2 example above (`default_ch1`) is the 0.1 interface and no longer applies.

**Milestone 4 — 2026-09-29:** profile 0.3 replaces presets with direct mode and profiles
([PROFILE-CARMOTION.md](PROFILE-CARMOTION.md)); the examples above are older interfaces.

```python
dev.set_config(profiles=[{"profile": "red",   "actions": {"ch1": [{"cmd": "stop"}], "ch2": [{"cmd": "brake"}]}},
                         {"profile": "green", "actions": {"ch1": [{"cmd": "continue"}], "ch2": [{"cmd": "continue"}]}}],
               start_profile="red", persist=True)
dev.control(mode="transmit", objects={"profile": "green"})                        # a stored profile
dev.control(mode="transmit", objects={"ch2": [{"cmd": "speed", "arg": 30}]})      # direct
```

A master holds authority only while it repeats its message (control timeout, default 2 s); the MRRoIP GUI and
the Rust `Keeper` do that.

| Channel | GPIO |
|---|---|
| 1 | IO10 |
| 2 | IO1  |

Board: Ai-Thinker ESP-C3-12F-Kit (ESP32-C3, 2 RMT TX channels). Flash = GPIO high (3.3 V); for an IR LED use a
MOSFET per channel. For the scope: probe on the GPIO, ground on the board's GND.

## Build

```bash
export IDF_PATH=~/esp/esp-idf
export PATH=/Library/Frameworks/Python.framework/Versions/3.12/bin:$PATH   # IDF env was set up with 3.12
export LC_ALL=en_US.UTF-8
. $IDF_PATH/export.sh
idf.py set-target esp32c3
idf.py build
idf.py -p /dev/cu.usbserial-XXXX flash monitor
```

## Encoder tests (host, no hardware)

```bash
cc -Wall -Wextra -I components/cm_ir/include components/cm_ir/cm_ir.c test/test_cm_ir.c -o /tmp/test_cm_ir && /tmp/test_cm_ir
```

Every expected block in the test was written by CarManager to an IR Mini and captured over USB.

## Verify on the scope

```bash
python tools/grab_raw.py captures/scope/<name> --ch 1 2
python tools/irframes.py --mid captures/scope/<name>.ch1     # --mid: clean logic signal with overshoot
```

The decoded frames (data, CRC, flash width) must equal the original module's for the same commands.
