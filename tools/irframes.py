"""
Decode CarMotion native IR frames from a raw scope capture (tools/grab_raw.py output: <base>.bin + <base>.json).

Uses only sharp rising edges (flash starts), so it tolerates dips inside flashes and the slow decay of an
output without load (the torn-off LED's channel). Bit symbols are the flash-to-flash period:
90 us = 0, 130 us = 1, 169 us = S (sync / end). A valid frame is SSS <flag> <n*8 bits> S with a correct
CRC-8/MAXIM over the data bytes.

    python tools/irframes.py captures/scope/2026-09-26_07_green_stop
    python tools/irframes.py captures/scope/2026-09-27_x.ch1.bin captures/scope/2026-09-27_x.ch2.bin
    python tools/irframes.py --rise=0.5 <capture>       # other rise threshold (default 2 V within 300 ns)
    python tools/irframes.py --mid <capture>            # mid-level crossings: clean logic signals with overshoot
"""

import collections
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cmdcode  # noqa: E402

RISE_V = 2.0        # minimum rise within RISE_SAMPLES to count as a flash start; None = mid-level crossings (--mid)
RISE_SAMPLES = 3
DEBOUNCE_S = 5e-6
SYMBOLS = ((80e-6, 100e-6, "0"), (120e-6, 140e-6, "1"), (160e-6, 180e-6, "S"))


def load(base):
    """base: <capture> (single-channel CH1) or <capture>.chN (multi-channel)."""
    raw = open(base + ".bin", "rb").read()
    ch = "1"
    if len(base) > 4 and base[-4:-1] == ".ch" and base[-1].isdigit():
        ch, base = base[-1], base[:-4]
    meta = json.load(open(base + ".json"))
    pre = meta["preamble"][ch]
    return raw, pre


def mid_level(raw):
    """Midpoint between the baseline (2nd percentile) and the flash level (99.5th percentile), in ADC counts."""
    v = sorted(raw[::97])
    lo, hi = v[len(v) // 50], v[min(len(v) - 1, len(v) * 995 // 1000)]
    return (lo + hi) / 2


def rising_edges(raw, pre):
    """Flash starts. Default: upward crossings of the mid level (overshoot and ringing after a flash stay below
    it); with RISE_V set: a rise of RISE_V volts within RISE_SAMPLES. Both with a 5 us debounce."""
    dt = pre["xinc"]
    debounce = int(DEBOUNCE_S / dt)
    edges, last = [], -debounce
    if RISE_V is None:
        mid = mid_level(raw)
        for i in range(1, len(raw)):
            if raw[i] > mid >= raw[i - 1] and i - last > debounce:
                edges.append(i)
                last = i
        return edges
    jump = int(RISE_V / pre["yinc"])
    for i in range(RISE_SAMPLES, len(raw)):
        if raw[i] - raw[i - RISE_SAMPLES] > jump and i - last > debounce:
            edges.append(i)
            last = i
    return edges


def flash_width(raw, pre, start):
    """Time (us) the signal stays above 50 % between the pre-flash baseline and the flash peak."""
    n30 = int(30e-6 / pre["xinc"])
    base = min(raw[max(0, start - 20):start + 1])
    peak = max(raw[start:start + n30] or [base])
    thr = base + (peak - base) / 2
    i = start
    while i < len(raw) and raw[i] < thr and i < start + 10:   # step onto the rising edge
        i += 1
    j = i
    while j < len(raw) and raw[j] > thr:
        j += 1
    return (j - start) * pre["xinc"] * 1e6


def _symbol(period):
    for lo, hi, s in SYMBOLS:
        if lo <= period <= hi:
            return s
    return None


def frames(base):
    """Yield dicts: t_ms, symbols, data (list or None), crc_ok, width_us (median flash width)."""
    raw, pre = load(base)
    dt = pre["xinc"]
    edges = rising_edges(raw, pre)
    cur = [edges[0]] if edges else []
    groups = []
    for a, b in zip(edges, edges[1:]):
        if _symbol((b - a) * dt) is None:
            groups.append(cur)
            cur = [b]
        else:
            cur.append(b)
    groups.append(cur)
    for g in groups:
        if len(g) < 2:
            continue
        syms = "".join(_symbol((b - a) * dt) or "?" for a, b in zip(g, g[1:]))
        widths = sorted(flash_width(raw, pre, e) for e in g)
        f = {"t_ms": (g[0] * dt + pre["xorig"]) * 1e3, "symbols": syms, "data": None, "crc_ok": False,
             "width_us": widths[len(widths) // 2]}
        body = syms[3:-1]
        if syms.startswith("SSS") and syms.endswith("S") and "S" not in body and len(body) % 8 == 1 and len(body) > 8:
            flag = int(body[0])
            onair = [int(body[1 + 8 * i: 9 + 8 * i], 2) for i in range(len(body) // 8)]
            data = onair if flag else [x ^ 0xFF for x in onair]
            f["data"], f["flag"] = data, flag
            f["crc_ok"] = cmdcode.crc8_maxim(data[:-1]) == data[-1]
        yield f


def width_class(w):
    """21 us = short flash, 26 us = long flash, >40 us = slow decay: an output without load (e.g. the torn-off
    LED's channel) that cannot discharge after the flash."""
    if w > 40:
        return "slow decay (unloaded output)"
    return "~26 us flash" if w >= 24 else "~21 us flash"


def summary(base):
    """Count valid frames by (data, flash width class)."""
    counts, bad = collections.Counter(), 0
    for f in frames(base):
        if f["crc_ok"]:
            counts[(" ".join("%02X" % x for x in f["data"]), width_class(f["width_us"]))] += 1
        else:
            bad += 1
    return counts, bad


def main(argv):
    global RISE_V
    if argv and argv[0].startswith("--rise="):
        RISE_V = float(argv.pop(0).split("=", 1)[1])
    elif argv and argv[0] == "--mid":
        argv.pop(0)
        RISE_V = None
    for base in argv:
        base = base[:-4] if base.endswith((".bin", ".json")) else base
        counts, bad = summary(base)
        print(os.path.basename(base))
        for (data, w), n in counts.most_common():
            print("  %4d x  %-14s  %s" % (n, data, w))
        print("  %4d x  incomplete / invalid (window edges, glitches)" % bad)


if __name__ == "__main__":
    main(sys.argv[1:])
