"""
Bench helpers for CarManager sweeps: one USB capture per GUI step, VNC control, write decoding.

The CarManager laptop is reached with `ssh -F tools/ssh_config carlaptop` (USBPcap, file transfer) and VNC
(vncdotool, port 5900). Each step:

    with bench.Step("25_reset_all", "Reset all + Yes") as st:
        bench.click(168, 704); ...                  # GUI actions while USB is captured
    st.writes   -> [(cv, value), ...]               # CarManager -> module CV writes in this step

The capture is copied to captures/usb/<name>.pcapng (read-only), the step appended to captures/sweep.csv and a
screenshot saved to captures/screens/<name>.png.
"""

import csv
import datetime
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import usbserial  # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SSH = ["ssh", "-F", os.path.join(ROOT, "tools", "ssh_config"), "carlaptop"]
SCP = ["scp", "-q", "-F", os.path.join(ROOT, "tools", "ssh_config")]
VNC = [os.path.join(ROOT, ".venv", "bin", "vncdo"), "-s", os.environ.get("BENCH_VNC", "192.168.1.60::5900")]
USBPCAP = r'"C:\Program Files\USBPcap\USBPcapCMD.exe"'
LAST_KNOWN_ADDR = 8          # 2026-09-27 after re-plug; the capture uses -A (all devices), decoding filters by this address
SWEEP_CSV = os.path.join(ROOT, "captures", "sweep.csv")
SCREENS = os.path.join(ROOT, "captures", "screens")


def ssh(cmd, check=False):
    return subprocess.run(SSH + [cmd], capture_output=True, text=True, check=check).stdout


def programmer_address():
    """(hub, address) of the CP210x (changes when re-plugged). Uses setup/find-programmer.ps1 on the laptop."""
    out = ssh("powershell -NoProfile -ExecutionPolicy Bypass -File find-programmer.ps1").split()
    if len(out) < 2:
        print("WARNING: USBPcap device listing empty; falling back to hub 1, address", LAST_KNOWN_ADDR)
        return 1, LAST_KNOWN_ADDR
    return int(out[0]), int(out[1])


# --- VNC -------------------------------------------------------------------

def vnc(*args):
    subprocess.run(VNC + [str(a) for a in args], check=True)


def click(x, y, pause=1.0):
    vnc("move", x, y, "click", 1, "pause", pause)


def key(k, pause=1.0):
    vnc("key", k, "pause", pause)


def set_field(x, y, value, pause=0.5):
    """Click a spin box / line edit, select all, type a value one character at a time (fast VNC typing
    reorders or drops characters)."""
    vnc("move", x, y, "click", 1, "key", "ctrl-a", "pause", 0.5)
    for ch in str(value):
        vnc("type", ch, "pause", 0.6)
    time.sleep(pause)


def screenshot(path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    vnc("capture", path)


# --- capture per step --------------------------------------------------------

class Step:
    """Context manager: USB capture around a GUI step, then fetch + decode + log."""

    hub = addr = None

    def __init__(self, name, change, settle=3.0):
        self.name, self.change, self.settle = name, change, settle
        self.pcap = os.path.join(ROOT, "captures", "usb", name + ".pcapng")
        if os.path.exists(self.pcap):
            raise FileExistsError(self.pcap)
        self.writes, self.reads = [], []

    def __enter__(self):
        vnc("move", 700, 400, "move", 705, 405, "pause", 0.5)   # keep the laptop screen awake
        if Step.addr is None:
            Step.hub, Step.addr = programmer_address()
        ssh("taskkill /f /im USBPcapCMD.exe >nul 2>&1")
        remote = r"C:\captures\%s.pcapng" % self.name
        self.proc = subprocess.Popen(
            SSH + ["%s -d \\\\.\\USBPcap%d -A -o %s" % (USBPCAP, Step.hub, remote)],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(3)
        return self

    def __exit__(self, exc_type, exc, tb):
        time.sleep(self.settle)
        vnc("move", 700, 400, "move", 705, 405, "pause", 1.5)   # wake the screen before the screenshot
        screenshot(os.path.join(SCREENS, self.name + ".png"))
        ssh("taskkill /f /im USBPcapCMD.exe >nul 2>&1")
        self.proc.wait(timeout=10)
        time.sleep(1)
        subprocess.run(SCP + ["carlaptop:C:/captures/%s.pcapng" % self.name, self.pcap], check=True)
        os.chmod(self.pcap, 0o444)
        dev = usbserial.find_programmer(self.pcap) or Step.addr
        for t, d, data in usbserial.messages(self.pcap, dev):
            if d == "host" and data[:2] == b"cW" and len(data) >= 5:
                self.writes.append(((data[2] << 8) | data[3], data[4]))
            if d == "dev" and data[:2] == b"Cr" and len(data) >= 5:
                self.reads.append(((data[2] << 8) | data[3], data[4]))
        if os.path.getsize(self.pcap) < 100:
            print("WARNING: empty capture (USB address changed?)", self.name)
        self._log()
        return False

    def _log(self):
        new = not os.path.exists(SWEEP_CSV)
        with open(SWEEP_CSV, "a", newline="") as f:
            w = csv.writer(f)
            if new:
                w.writerow(["time", "capture", "gui_change", "writes (CV=value hex)", "reads"])
            w.writerow([datetime.datetime.now().isoformat(timespec="seconds"), "usb/%s.pcapng" % self.name,
                        self.change, " ".join("%d=%02X" % cv for cv in self.writes),
                        "%d CVs" % len(self.reads) if self.reads else ""])
        print("%-45s %s" % (self.name, " ".join("%d=%02X" % cv for cv in self.writes) or "(no writes)"))
