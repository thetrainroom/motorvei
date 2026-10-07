"""
Grab the full memory of one or more Rigol channels as raw ADC bytes plus settings/preamble (<base>.json).
Arms a single trigger first. Raw bytes are ~8x smaller than CSV and are what tools/irframes.py reads.

    python tools/grab_raw.py captures/scope/2026-09-26_07_green_stop            # CH1 -> <base>.bin
    python tools/grab_raw.py captures/scope/2026-09-27_x --ch 1 2               # -> <base>.ch1.bin, <base>.ch2.bin

Files are never overwritten (captures are immutable).
"""

import argparse
import datetime
import json
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import rigol_capture as rc  # noqa: E402

SCOPE_IP = os.environ.get("SCOPE_IP", "192.168.1.50")   # set SCOPE_IP to your scope's address


def bin_path(base, ch, channels):
    return base + ".bin" if list(channels) == [1] else "%s.ch%d.bin" % (base, ch)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("base")
    ap.add_argument("--ch", type=int, nargs="+", default=[1])
    ap.add_argument("--no-trigger", action="store_true", help="read what is in memory, do not arm")
    args = ap.parse_args(argv)
    outs = [bin_path(args.base, ch, args.ch) for ch in args.ch] + [args.base + ".json"]
    for p in outs:
        if os.path.exists(p):
            raise FileExistsError("capture exists (immutable): " + p)
    os.makedirs(os.path.dirname(os.path.abspath(args.base)), exist_ok=True)

    s = rc.DS1000Z(SCOPE_IP)
    if not args.no_trigger:
        s.single()
    s.stop()
    meta = s.settings()
    meta["captured_at"] = datetime.datetime.now().astimezone().isoformat(timespec="seconds")
    meta["preamble"] = {}
    t0 = time.time()
    for ch in args.ch:
        raw, pre = s.read_channel_raw(ch)
        meta["preamble"][str(ch)] = pre
        with open(bin_path(args.base, ch, args.ch), "wb") as f:
            f.write(raw)
        print("CH%d: %d points" % (ch, len(raw)))
    with open(args.base + ".json", "w") as f:
        json.dump(meta, f, indent=2)
    print("done in %.1f s, %.3g Sa/s" % (time.time() - t0, meta["sample_rate"]))


if __name__ == "__main__":
    main()
