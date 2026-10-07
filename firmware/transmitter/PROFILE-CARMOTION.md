# Profile `carmotion` 0.1 (superseded by 0.3, below)

An IR transmitter for Viessmann CarMotion vehicles: each output channel repeats a playlist of CarMotion
commands (protocol: `../../docs/ir-protocol.md`). A master sets the playlists; without one, the stored default
playlists run, so the module also works stand-alone like an IR Mini. Implementation: `main/profile_carmotion.c`.

It answers the nine items of MRROIP-1.md §14.

| # | Item | `carmotion` |
| --- | --- | --- |
| 1 | `device_type`, `profile_version` | `carmotion`, `0.1` |
| 2 | `device_class` | `stationary`; `capabilities.autonomous` true |
| 3 | Modes | `transmit` (the channels send their playlists), `off` (all outputs dark). No mode takes a `target` |
| 4 | `busy` | Always false: a transmitter has nothing a master must wait for |
| 5 | Rest state | Mode `transmit` with the stored default playlists (`default_ch1`, `default_ch2`). `reset` goes there |
| 6 | Autonomous programme | Sending the stored default playlists. Runs at start, and resumes when the master releases or its authority times out |
| 7 | Faults | None. An unparsable default playlist leaves that channel dark and is reported as `state.profile.default_chN_error`, not as a fault |
| 8 | `estop` | All outputs dark after the frame in progress (≤ 4 ms); latched until `reset` |
| 9 | Tests | Core suite: 23 of 23 applicable tests pass (probe 2026-09-28; C-16/21/22 need profile hooks, not written yet). Scope verification: `captures/scope/2026-09-28_02_…` |

## Objects

`ch1`, `ch2` (one per output; 2 on the ESP32-C3), each a playlist:

```json
{ "id": "ch1", "type": "object[]", "max_count": 8, "fields": [
    { "name": "cmd",   "type": "enum", "values": ["stop", "continue", "brake", "restore", "macro", "reverse",
        "reverse_magnet", "lane", "speed", "speed_min", "speed_max", "speed_default", "function_on",
        "function_off", "magnet", "rolling_highway", "raw", "function_on_permanent", "function_off_permanent"],
      "default": "stop" },
    { "name": "arg",   "type": "int", "min": 0, "max": 255, "default": 0 },
    { "name": "range", "type": "enum", "values": ["normal", "short"], "default": "normal" },
    { "name": "raw",   "type": "string", "max_len": 19, "default": "" } ] }
```

| `cmd` | `arg` | Codes sent |
|---|---|---|
| `stop`, `continue` | — | two codes (short + long flash), like a stopped vehicle |
| `brake`, `restore`, `reverse_magnet` | — | one code |
| `macro` | 1–14 | `BE 03+n` |
| `reverse` | distance in cm, 0 = none | `BE 13 [cm]` |
| `lane` | 0 lane 0, 1 lane 1, 2 lane 2, 3/4/5 special on/off/toggle, 6/7 direction A/B | `BE 17+arg` |
| `speed`, `speed_min`, `speed_max`, `speed_default` | km/h 0–155 (steps of 5) | `BE/3E/3F/BF 20+arg/5` |
| `function_on/off[_permanent]` | function 0–11 (permanent 0–7) | `BE/3E 60+arg(+10)` |
| `magnet` | pole pattern 0–7 (N, S, SN, SS, SNN, SNS, SSN, SSS) | `BE 40+arg` |
| `rolling_highway` | 0 exit, 1 enter, 2 enter tightly packed | `BE 50+arg F0` |
| `raw` | — | the bytes in `raw` (hex, without CRC) |

Full command reference with vehicle effects: [../../docs/command-table.md](../../docs/command-table.md).

`range: short` sends the command's codes with the short flash. Refusals use `invalid_object_state` with the
reasons `wrong_type`, `unknown_cmd`, `missing_cmd`, `out_of_range`, `not_allowed`, `invalid_hex`, `missing_raw`,
`too_many_entries`.

Example:

```json
{ "seq": 1, "mode": "transmit",
  "objects": { "ch1": [ {"cmd": "speed", "arg": 30}, {"cmd": "function_on_permanent", "arg": 0} ],
               "ch2": [ {"cmd": "lane", "arg": 1}, {"cmd": "magnet", "arg": 5, "range": "short"} ] } }
```

## Parameters

| Name | Type | Range | Default | Unit | Meaning |
|---|---|---|---|---|---|
| `long_flash_us` | int | 5–60 | 26 | µs | flash width, normal range (original: transmitter power 25 → 26 µs) |
| `short_flash_us` | int | 5–60 | 21 | µs | flash width, short range (original: 25 − 5 → 21 µs) |
| `gap_min_us`, `gap_max_us` | int | 1000–30000 | 5000, 15000 | µs | random darkness after each frame |
| `default_ch1`, `default_ch2` | string | ≤ 31 chars | `stop` | — | stored playlists, text form `speed:50,stop` (`,` between entries, `name[:arg][/s]`, `/s` = short range, `raw:BE2A`) |

All persistable and applied immediately.

## State

`state.profile`:

```json
{ "source": "master", "transmitting": true,
  "ch1": [ {"cmd": "speed", "arg": 30, "range": "normal", "raw": ""} ],
  "ch2": [ {"cmd": "stop", "arg": 0, "range": "normal", "raw": ""} ] }
```

`source` is `master` while a master's playlists are active, `default` for the stored ones. Frame counters are
not reported (state must read the same over HTTP and UDP, C-19); the serial console command `frames` shows them.

---

# Version 0.3 — direct mode and profiles (current, 2026-09-29)

Replaces the preset design of 0.2 (kept below for the record). Everything is typed JSON: users press buttons or
software sends messages, nobody types commands, so there is no text syntax to parse. Two ways to drive the
channels, usable together:

- **Direct:** the master says what a channel sends, a list of commands repeated in order; `[]` is dark.
  ```json
  {"ch1": [{"cmd": "continue"}], "ch2": [{"cmd": "speed", "arg": 30}, {"cmd": "function_on", "arg": 0}]}
  ```
- **Profile:** stored channel commands, called by name or number: `{"profile": "ns_go"}` or `{"profile": 3}`.
  It sets the channels it has rows for and leaves the others as they are, so unrelated uses on one module don't
  disturb each other. The same profile names can be used on several modules with different channels: one
  TrainMaster route calls `ns_go` on all of them. Channel values in the same message apply after the profile.

A command is `{"cmd", "arg", "range", "raw"}`: `cmd` an enum of the command names (`stop`, `continue`, `speed`, …),
`arg` 0–255, `range` `normal`/`short`, `raw` hex for `cmd: raw`.

**A profile is a stored control message:** a name and `actions`, which has exactly the shape of `objects` in
`/control`. The field type `actions` is new in MRROIP-1.md §7.2 (2026-09-29): the core checks each entry with the
same object check as a control message.

```json
"profiles": [
  {"profile": "ns_go", "actions": {"ch1": [{"cmd": "continue"}],
                                   "ch2": [{"cmd": "stop"}, {"cmd": "function_on", "arg": 4, "range": "short"}]}},
  {"profile": "ew_go", "actions": {"ch1": [{"cmd": "stop"}], "ch2": []}}
]
```

A channel the actions leave out keeps what it sends; `[]` switches it off. Profile number n is row n; a name
finds the first row with it. Up to 32 profiles. Default: `red` = ch1 stop, `green` = ch1 continue (the IR Mini's
two commands).

| Parameter | |
|---|---|
| `profiles` | `object[]` of `profile` (≤ 16) and `actions` (`ch1`, `ch2`, … → command list) |
| `start_profile` | Name or number applied at start and whenever no master is present (release, timeout, reset), after all channels go dark. Empty = all dark (default) |
| `channels` | Per channel: `name`, `long_flash_us`, `short_flash_us` |
| `gap_min_us`, `gap_max_us` | As before |

State: `{"source": "master"|"rest", "transmitting": true, "profile": "ns_go"|null, "ch1": {"name": …, "sends": [{"cmd": "continue", "arg": 0, "range": "normal"}]}}`.
`profile` is the profile applied last; it becomes null when a channel is later changed directly. A repeated message
changes nothing (a channel whose commands are unchanged keeps its place in the round-robin). An unknown profile
is refused with `unknown_profile`. `ui`: channel names label `ch1`…, profile names label the numbers of `profile`.

Templates (crossings and so on) belong to configuration software on the PC, which writes the rows; the module
knows nothing about crossings.

---

# Version 0.2 (superseded by 0.3) — presets and independent channels (implemented 2026-09-28, labels 2026-09-29)

Running on the bench board (2 channels). Verified: presets and channels set via `/config` (unknown field refused
with `not_allowed`), master holds `{"ch1": 4, "ch2": 3}`, scope shows the preset codes exactly, release returns
to the default presets, presets survive a reboot (`captures/scope/2026-09-28_03_…`). Labels served in `/definition` `ui` since
2026-09-29 (a label edit moves the ETag). Core probe on 0.2: 23 passed, 0 failed, 8 skipped.

Agreed 2026-09-28. Channels are unrelated: each LED is its own "virtual IR Mini" at its own place on the layout
(a bus stop on ch1, a speed zone on ch4). The user defines **numbered presets**; a master sets a channel to a preset
by number — "set ch3 to preset 3" — instead of sending playlists. Direct playlists remain as the expert option.

## Model

- **Preset library** — presets 1–64, each a playlist plus an optional label ("bus_stop"), shared by all channels.
  Preset 0 = nothing (the channel is dark).
- **Per channel** — a name, the preset it sends now, a default preset (no master), flash widths. No physical inputs.
- **Control** — per channel, independently: `{"mode": "transmit", "objects": {"ch3": 3}}` sets only ch3 to preset 3
  (§9.2 partial map). An array instead of a number is a direct playlist (expert).
- Numbers rather than names: automation sends a number (like a DCC address or macro number), and `/definition`
  stays the same when a preset is edited or relabelled. Labels are for people (GUI, TrainMaster), never needed by
  a master.

## Parameters

```json
{ "name": "presets", "type": "object[]", "count": 64, "persist": true,
  "doc": "Preset n is element n-1",
  "fields": [
    { "name": "label",    "type": "string", "max_len": 16, "default": "" },
    { "name": "playlist", "type": "string", "max_len": 120,
      "doc": "Text form: speed:30,function_on:3/s  (',' between entries, name[:arg][/s], raw:BE2A)" } ] }

{ "name": "channels", "type": "object[]", "count": 8, "persist": true, "fields": [
    { "name": "name",           "type": "string", "max_len": 24, "default": "" },
    { "name": "default_preset", "type": "int", "min": 0, "max": 64, "default": 1 },
    { "name": "long_flash_us",  "type": "int", "min": 5, "max": 60, "default": 26, "unit": "us" },
    { "name": "short_flash_us", "type": "int", "min": 5, "max": 60, "default": 21, "unit": "us" } ] }
```

Factory library: preset 1 = `stop`, others empty (the user fills them). The playlist inside a preset is text, not a
list of records: MRROIP-1.md §7.2 keeps records one level deep, and a
table of name | playlist renders in any master. `gap_min_us` / `gap_max_us` stay module-wide. `count` is the
board's channel count (2 on the ESP32-C3, 8 on an 8-channel board).

## Objects

```json
{ "id": "ch1", "one_of": [
    { "type": "int", "min": 0, "max": 64, "doc": "preset number; 0 = dark" },
    { "type": "object[]", "max_count": 8, "fields": [ "cmd", "arg", "range", "raw" as in 0.1 ] } ] }
```

The two forms differ in JSON type (number vs array), as §7.2 requires for `one_of`. Selecting an empty preset is
accepted and leaves the channel dark (a preset may be filled later by `/config`).

**Labels are shown.** The preset labels go to masters through the `ui` section (§7.7) as value labels of each
channel object, so a GUI shows "3 — bus_stop" and TrainMaster can offer the list by label:

```json
"ui": { "ch1": { "label": "Bus stop Nord", "value_labels": { "0": "dark", "1": "stop", "3": "bus_stop" } } }
```

The channel's `name` becomes the `ui` label of its object. Because labels come from `/config`, the `/definition`
ETag must change when a label or channel name changes (profile ETag = hash of the labels).

## State

```json
{ "source": "master",
  "ch1": { "name": "Bus stop Nord", "preset": 3,    "playlist": "brake,function_on:4/s" },
  "ch2": { "name": "Speed zone",    "preset": null, "playlist": "speed:30" } }
```

`preset` is null while a direct playlist is active.

## Authority

MRRoIP authority is per endpoint: one master holds the whole module, and on release or timeout **every** channel
returns to its default preset. Decision: **TrainMaster is the single master** for all channels; automations are
rules in TrainMaster. Independent masters per channel would need authority per object — a question for the MRRoIP
spec, not a local workaround.

## Needed in the MRRoIP C core

**Done 2026-09-28** (`PARAM_RECORDS`, mrroip repository, uncommitted; stored as an NVS blob). Still open: the
Python core, `conformance/vectors` and the spec text. Original note: the C core supported int and string
parameters (≤ 31 characters) only. Version 0.2 needs `object[]` parameters with
string and int fields: validation against `fields`, `count` / `max_count`, `/definition` and `/config` output, and
storage (JSON in NVS). Per the mrroip CLAUDE.md the same change goes into the Python core, `conformance/vectors`
and the spec where it lags. Size: 64 presets ≈ 64 × 140 bytes of JSON ≈ 9 KB — fits the NVS partition next to the
Wi-Fi data; a dedicated data partition if it grows.

## Needed for the labels (mrroip repository)

**Done 2026-09-29** (uncommitted): all three below. A preset without a label is shown by its playlist text;
empty presets are left out of `value_labels`.

1. **Spec §7.7:** `value_labels` also for an `int` value (keys are the numbers as strings). Today "for an enum".
2. **C core:** a `ui` section in `/definition` — core parameters plus an optional profile hook
   (`profile_emit_ui`, weak default: nothing). Today the C core emits no `ui` section at all.
3. **GUI:** use `value_labels` for an int (a dropdown of the labelled values, plus free entry of the number).

## Not planned: physical inputs

The module has no inputs; channels change preset only by a master (TrainMaster) or at start (default preset).
Should inputs ever be wanted (contact, reed contact, DCC accessory address switching a channel's preset, like the
IR Mini's blue wire), they would be added as a per-channel assignment then.

## Open

- Optional per-channel subset of presets (shorter lists in the GUI)
