# Motorvei — open tools for Viessmann® CarMotion

Open tools and modules for the Viessmann CarMotion road system (H0, "Viessmann biler"): the IR protocol
documented from measurements, an ESP32 IR transmitter with playlists and network control, and tools for
configuring the Viessmann 8403 IR Mini without CarManager. Planned: a scanner for vehicle telemetry and
BilLeder, a configuration tool for CarMotion vehicles and IR modules.

All findings come from observing signals on our own hardware (oscilloscope captures of the IR output, USB
captures of the programming link). No firmware was read, extracted or modified, and CarManager was not
decompiled.

Not affiliated with or endorsed by Viessmann Modelltechnik GmbH. "Viessmann", "CarMotion" and "CarManager"
are names of their respective owner and are used here only to describe compatibility.

## Contents

- `docs/` — findings: [overview](docs/README.md), [physical layer](docs/physical-layer.md),
  [IR protocol](docs/ir-protocol.md), [command table](docs/command-table.md), [CV map](docs/cv-map.md),
  [method](docs/method-cv-sweep.md), [related protocols](docs/related-protocols.md),
  [module design](docs/module-design.md)
- `firmware/transmitter/` — ESP32 IR transmitter (ESP-IDF), controlled over MRRoIP
- `tools/` — decoder, encoder, USB serial extraction, bench automation
- `rigol_capture.py` — Rigol DS1000Z capture over LAN
- `PROJECT-BRIEF.md` — scope and build order

## Status

Work in progress. The IR Mini findings are tested on one module (firmware 1.06). The ESP32 transmitter
reproduces the original's frames exactly (checked on the oscilloscope) and has been tested with a CarMotion
truck: stop / continue, speed, lights, indicators, hazard, roof beacons and macros, also several commands
in one playlist (see [module design](docs/module-design.md)).

Use at your own risk — never write CV8 (factory reset), keep a backup, and do not update the module firmware
if you rely on unofficial configurations.

## Licence

- Software and firmware: [Apache-2.0](LICENSE)
- Documentation (`docs/`): [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/)
- Hardware (when published): CERN-OHL-P-2.0
