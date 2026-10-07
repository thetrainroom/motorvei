"""
Extract the serial byte stream between CarManager and the 8401 programmer (CP210x) from a USBPcap capture.

Uses tshark (Wireshark) to read bulk transfers, merges USB chunks into messages, and optionally hides
the Who/Ver heartbeat CarManager sends once per second.

    python tools/usbserial.py captures/usb/03_red_macro2_to_basic_stop.pcapng
    python tools/usbserial.py --all captures/usb/03_red_macro2_to_basic_stop.pcapng   # include heartbeat
"""

import argparse
import os
import shutil
import subprocess
import sys

TSHARK_CANDIDATES = ["tshark", "/Applications/Wireshark.app/Contents/MacOS/tshark", r"C:\Program Files\Wireshark\tshark.exe"]

# A new message starts when the direction changes or the line is idle longer than this.
GAP_S = 0.02

HEARTBEAT = {
    ("host", b"Who"),
    ("host", b"Ver"),
    ("dev", b"ViessStop\x00"),
    ("dev", b"i\x01\x06\x03\x00"),
}


def _tshark():
    for c in TSHARK_CANDIDATES:
        if shutil.which(c) or os.path.exists(c):
            return c
    raise FileNotFoundError("tshark not found (install Wireshark)")


def read_chunks(path, device=None):
    """Yield (time_s, 'host'|'dev', bytes) for every bulk transfer carrying data (optionally one USB device)."""
    flt = "usb.transfer_type==3 && usb.capdata"
    if device is not None:
        flt += " && usb.device_address==%d" % device
    out = subprocess.run(
        [_tshark(), "-r", path, "-Y", flt,
         "-T", "fields", "-e", "frame.time_relative", "-e", "usb.src", "-e", "usb.capdata"],
        check=True, capture_output=True, text=True).stdout
    for line in out.splitlines():
        t, src, data = line.split("\t")
        yield float(t), "host" if src == "host" else "dev", bytes.fromhex(data.replace(":", ""))


def find_programmer(path):
    """USB address of the device that answers CarManager's heartbeat (host sends 'Who' = 57 68 6f)."""
    out = subprocess.run(
        [_tshark(), "-r", path, "-Y", "usb.capdata contains 57:68:6f", "-T", "fields", "-e", "usb.device_address"],
        check=True, capture_output=True, text=True).stdout.split()
    return int(out[-1]) if out else None


def messages(path, device=None):
    """Merge chunks into messages: list of (time_s, direction, bytes)."""
    msgs = []
    last_t = None
    for t, d, data in read_chunks(path, device):
        if msgs and msgs[-1][1] == d and t - last_t < GAP_S:
            msgs[-1] = (msgs[-1][0], d, msgs[-1][2] + data)
        else:
            msgs.append((t, d, data))
        last_t = t
    return msgs


def fmt(data):
    printable = "".join(chr(b) if 32 <= b < 127 else "." for b in data)
    return "{:<48} {}".format(data.hex(" "), printable)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pcap")
    ap.add_argument("--all", action="store_true", help="include Who/Ver heartbeat")
    ap.add_argument("--device", type=int, help="USB device address (captures made with -A contain all devices)")
    args = ap.parse_args(argv)
    for t, d, data in messages(args.pcap, args.device):
        if not args.all and (d, data) in HEARTBEAT:
            continue
        print("{:9.3f}  {}  {}".format(t, ">" if d == "host" else "<", fmt(data)))


if __name__ == "__main__":
    sys.exit(main())
