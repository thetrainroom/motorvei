# Motorvei — notes for Claude Code

Open tools and modules for Viessmann CarMotion (H0 road vehicles, IR controlled). `PROJECT-BRIEF.md` is the
original brief from 2026-09-26 (content unchanged); this file is the current state. Findings live in
`docs/` (start at `docs/README.md`).

## State (2026-10-07)

| Brief stage | State |
|---|---|
| 1 Physical layer | done — no carrier, one 21/26 µs flash per bit, CRC-8/MAXIM; decoder `tools/irframes.py` |
| 2 Programming link & CV map | CV map of the IR Mini 8403 command slots done (`docs/cv-map.md`); `proglink/` not written |
| 3 Command sweep | mostly replaced by the CarManager settings sweep (`docs/command-table.md`) |
| 4 Own transmitter | `firmware/transmitter/` (ESP32-C3, 2 channels, MRRoIP): frames identical to the original on the scope; vehicle tests 1–3 passed, 4 entries per playlist work on a moving truck (`docs/module-design.md`) |
| 5 Scanner | not started |
| 6 Network integration | transmitter is an MRRoIP endpoint; TrainMaster integration not started |

Next: 8-entry playlists with shorter frame gaps, MOSFET LED driver (current LEDs are weaker than the
original's), 8-channel port to the classic ESP32 (WROOM).

## Layout

- `docs/` — findings, each claim citing the capture it came from
- `firmware/transmitter/` — ESP-IDF project; encoder `components/cm_ir`, host tests in `test/`
- `tools/` — `irframes.py` (decode scope captures), `cmdcode.py` (encode), `usbserial.py` (programming link
  from USB captures), `grab_raw.py` / `rigol_capture.py` (scope), `bench.py` (CarManager automation),
  `demo_scenes.py` (video demo)
- `captures/log.csv`, `captures/sweep.csv` — index of the captures; the raw captures are not in this repo

## Conventions

- Measurements are data, not assumptions; every claim in `docs/` cites its capture file.
- Captures are immutable; derived data goes elsewhere.
- Record module and vehicle firmware with every capture or vehicle test.
- One variable per capture; baseline captures first.
- No firmware dumping, no decompiling CarManager, no Viessmann binaries in the repo.
- Python for tools (`.venv`, Python 3.12); C on ESP-IDF for firmware.
- Bench addresses come from the environment (`SCOPE_IP`, `BENCH_VNC`, `tools/ssh_config`), never from the code.

## Checks

```bash
cd firmware/transmitter && cc -Wall -Wextra -I components/cm_ir/include components/cm_ir/cm_ir.c test/test_cm_ir.c -o /tmp/test_cm_ir && /tmp/test_cm_ir
```

Our frames must match the original's on the scope before any vehicle test.

## Public wording

This repository is public. Write "compatible with Viessmann CarMotion" and "configuration tool for", never
"clone" or "replaces CarManager". No Viessmann manuals, logos or CarManager screenshots; findings only, no
statements about Viessmann's motives. Keep the "not affiliated" note in `README.md` and `NOTICE`.
