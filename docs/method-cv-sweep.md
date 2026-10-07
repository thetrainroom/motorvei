# Method: mapping CarManager settings to CVs

How every CarManager setting of the IR Mini was mapped to the CVs it writes. Reproducible with the same
hardware; all raw data is kept.

## Setup

| Part | Detail |
|---|---|
| Module | Viessmann 8403 IR Mini, firmware 1.06, own supply (16 V AC transformer or lab supply) |
| Programmer | 8401 (CP210x USB–UART, 38400 baud 8E1) on the Windows laptop |
| CarManager | version 1.32 (Windows), running on the laptop, controlled remotely over VNC (port 5900) |
| USB capture | USBPcap on the laptop, started/stopped over SSH (`tools/ssh_config`) |
| Control / analysis | Mac, `tools/bench.py` (Python 3.12 venv, vncdotool, tshark) |

## Procedure

1. **Defined starting point:** "Reset all" in CarManager (captured: `usb/28_reset_all.pcapng` — CarManager writes
   CV8 = 8 and reads back CV1–102). The factory state is the baseline for every change.
2. **One step = one GUI change = one capture.** For each step `tools/bench.py`:
   - starts a USBPcap capture of all devices on the programmer's root hub (`-A`),
   - performs exactly one GUI change over VNC (click, select, type),
   - waits for CarManager to finish synchronising, saves a screenshot (`captures/screens/<step>.png`),
   - stops the capture, copies it to `captures/usb/<step>.pcapng` (read-only),
   - finds the programmer in the capture (the USB device that receives `Who`), decodes the serial stream and
     extracts every CV write (`cW` CVhi CVlo value),
   - appends a row to `captures/sweep.csv`: time, capture file, GUI change, CV writes.
3. **Order:** one GUI element at a time, stepping through all values of a list with the keyboard (Down) and
   bracketing numeric fields (minimum, maximum, two values in between), as in the brief.
4. **Evaluation:** per step, the written CVs and values; code blocks are decoded with `tools/cmdcode.py`
   (flag, inversion, CRC-8/MAXIM). Results go into `docs/cv-map.md` and `docs/ir-protocol.md`, each claim
   citing its capture.

## Notes

- CarManager writes only CVs whose value changes, and on a category change writes a placeholder code first.
  A step without writes means the setting did not change anything in the module (or only in CarManager).
- The programmer's USB address changes when it is re-plugged; capturing all devices and locating the
  programmer by its traffic avoids losing captures.
- Windows OpenSSH kills processes when the SSH session ends, so the capture runs inside an SSH session that
  stays open for the duration of the step.
- Wireshark must be closed on the laptop (it locks the USBPcap device).
- Typing into CarManager fields over VNC must be slow (one character at a time, `bench.set_field`); fast typing
  reordered or dropped characters (332 became 323). Decimal separators ('.' or ',') were dropped entirely, so
  decimal fields were set via *Direct configuration* instead.
- Numeric fields write on every keystroke (typing 255 writes 2, 25, 255); text fields write on commit.
- The laptop screen switches off after ~1–2 min; `bench.py` moves the mouse before each step and screenshot.
- Nothing in this method touches firmware; "Update decoder" is never used.

## Result of the 2026-09-27 sweep

Steps `usb/25`–`usb/171` (raw list in `captures/sweep.csv`, screenshots in `captures/screens/`). Every setting
of the IR Mini page in CarManager (firmware 1.06) was mapped: description, operating mode and its options
(impulse input, delay, inverted mode, traffic phases, DCC address), *Finetuning* (power, closeup, filters),
all command categories and entries, and every option checkbox. The results are in `docs/cv-map.md` and
`docs/ir-protocol.md`. Not covered: the *preset* dialog (it only combines settings mapped here).
