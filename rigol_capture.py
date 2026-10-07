"""
Remote control and deep-memory capture for Rigol DS1000Z oscilloscopes over LAN (SCPI, raw socket).

No dependencies beyond the Python standard library.

Example use:
    scope = DS1000Z("192.168.1.50")
    print(scope.idn())
    scope.single()                      # arm, wait for trigger
    cap = scope.capture([1, 2])         # full memory of CH1 + CH2
    cap.write_csv("captures/frame_ch1_ch2.csv")

Command line:
    python rigol_capture.py 192.168.1.50 -c 1 2 -o captures/frame.csv --single
    python rigol_capture.py 192.168.1.50 --screenshot captures/frame.png

Every CSV gets a sidecar <name>.json with the scope settings used, so a capture
is self-describing. Existing files are never overwritten (captures are immutable).
"""

import argparse
import datetime
import json
import os
import socket
import sys
import time

SCPI_PORT = 5555

TIMEOUT = 10.0

# Max points per :WAV:DATA? read in BYTE format (DS1000Z programming guide).
MAX_CHUNK = 250000

# Valid memory depths depend on how many channels are enabled.
MEMORY_DEPTHS = {
    1: [12000, 120000, 1200000, 12000000, 24000000],
    2: [6000, 60000, 600000, 6000000, 12000000],
    3: [3000, 30000, 300000, 3000000, 6000000],
    4: [3000, 30000, 300000, 3000000, 6000000],
}


class Capture:
    """Waveform data from one acquisition: a shared time axis and one voltage list per channel."""

    def __init__(self, time_s, volts, meta):
        self.time_s = time_s
        self.volts = volts  # {channel_number: [volts, ...]}
        self.meta = meta

    def write_csv(self, path, overwrite=False):
        if not overwrite:
            for p in (path, _meta_path(path)):
                if os.path.exists(p):
                    raise FileExistsError("Capture file already exists (captures are immutable): {}".format(p))
        os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
        channels = sorted(self.volts)
        with open(path, "w") as f:
            f.write("time_s," + ",".join("CH{}".format(c) for c in channels) + "\n")
            columns = [self.volts[c] for c in channels]
            for i, t in enumerate(self.time_s):
                f.write("{:.9e},".format(t) + ",".join("{:.4g}".format(col[i]) for col in columns) + "\n")
        with open(_meta_path(path), "w") as f:
            json.dump(self.meta, f, indent=2)


class DS1000Z:
    """
    Class to control a Rigol DS1000Z series oscilloscope.

    s = DS1000Z('192.168.1.50')
    """

    def __init__(self, ip, port=SCPI_PORT, debug=False):
        self.__ip = ip
        self.__port = port
        self.__s = None
        self.debug = debug
        self._connect()
        inst_id = self.idn()
        if "RIGOL" not in inst_id.upper():
            raise Exception("Rigol scope not found: {}".format(inst_id))

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()

    def __del__(self):
        self.close()

    def _connect(self):
        self.__s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.__s.settimeout(TIMEOUT)
        try:
            self.__s.connect((self.__ip, self.__port))
        except Exception as e:
            raise Exception("Failed to connect to Rigol at {}:{}: {!r}".format(self.__ip, self.__port, e))

    def close(self):
        if self.__s:
            try:
                self.__s.close()
            except Exception as e:
                print(e)
            finally:
                self.__s = None

    def _send(self, text):
        if self.debug:
            print("DEBUG::Rigol-Send: " + text)
        self.__s.sendall(bytes(text + "\n", "ascii"))

    def _recv_exact(self, n):
        buf = bytearray()
        while len(buf) < n:
            chunk = self.__s.recv(min(n - len(buf), 65536))
            if not chunk:
                raise Exception("Rigol closed connection ({} of {} bytes received)".format(len(buf), n))
            buf += chunk
        return bytes(buf)

    def _recv(self, call=""):
        """Send an optional query and read one newline-terminated ASCII response."""
        if call:
            self._send(call)
        buf = bytearray()
        while not buf.endswith(b"\n"):
            chunk = self.__s.recv(4096)
            if not chunk:
                raise Exception("Rigol closed connection while reading response to {!r}".format(call))
            buf += chunk
        res = buf.decode("ascii").strip()
        if self.debug:
            print("DEBUG::Rigol-Recv: " + res)
        return res

    def _recv_block(self, call):
        """Send a query that returns an IEEE 488.2 definite-length block (#9000001200...) and return the payload."""
        self._send(call)
        if self._recv_exact(1) != b"#":
            raise Exception("Expected binary block in response to {!r}".format(call))
        ndigits = int(self._recv_exact(1))
        length = int(self._recv_exact(ndigits))
        data = self._recv_exact(length)
        self._recv_exact(1)  # trailing "\n"
        if self.debug:
            print("DEBUG::Rigol-Recv: <{} bytes>".format(length))
        return data

    # --- General ---

    def idn(self):
        return self._recv("*IDN?")

    def check_error(self):
        res = self._recv(":SYST:ERR?")
        if not res.startswith("0,"):
            raise RuntimeError("Rigol error: {}".format(res))

    def run(self):
        self._send(":RUN")

    def stop(self):
        self._send(":STOP")

    def trigger_status(self):
        """One of TD, WAIT, RUN, AUTO, STOP."""
        return self._recv(":TRIG:STAT?")

    def single(self, timeout=30.0):
        """Arm a single acquisition and block until it has triggered and stopped."""
        self._send(":SING")
        time.sleep(0.2)
        end = time.time() + timeout
        while time.time() < end:
            if self.trigger_status() == "STOP":
                return
            time.sleep(0.1)
        raise TimeoutError("No trigger within {:.0f} s".format(timeout))

    # --- Setup ---

    def setup_channel(self, ch, scale_v=None, offset_v=None, probe=None, coupling=None):
        if probe is not None:
            self._send(":CHAN{}:PROB {}".format(ch, probe))
        if scale_v is not None:
            self._send(":CHAN{}:SCAL {}".format(ch, scale_v))
        if offset_v is not None:
            self._send(":CHAN{}:OFFS {}".format(ch, offset_v))
        if coupling is not None:
            assert coupling in ["DC", "AC", "GND"]
            self._send(":CHAN{}:COUP {}".format(ch, coupling))
        self._send(":CHAN{}:DISP ON".format(ch))

    def setup_timebase(self, scale_s, offset_s=0.0):
        self._send(":TIM:MAIN:SCAL {}".format(scale_s))
        self._send(":TIM:MAIN:OFFS {}".format(offset_s))

    def setup_edge_trigger(self, source=1, level_v=1.0, slope="POS", sweep="SING"):
        assert slope in ["POS", "NEG", "RFAL"]
        assert sweep in ["AUTO", "NORM", "SING"]
        self._send(":TRIG:MODE EDGE")
        self._send(":TRIG:EDG:SOUR CHAN{}".format(source))
        self._send(":TRIG:EDG:SLOP {}".format(slope))
        self._send(":TRIG:EDG:LEV {}".format(level_v))
        self._send(":TRIG:SWE {}".format(sweep))

    def set_memory_depth(self, depth):
        """depth: 'AUTO' or a point count valid for the number of enabled channels (see MEMORY_DEPTHS)."""
        n = len(self.enabled_channels())
        if depth != "AUTO" and depth not in MEMORY_DEPTHS[n]:
            raise ValueError("Memory depth {} invalid with {} channel(s), use one of {}".format(depth, n, MEMORY_DEPTHS[n]))
        self.run()  # depth can only be changed while running
        self._send(":ACQ:MDEP {}".format(depth))

    def enabled_channels(self):
        return [ch for ch in range(1, 5) if self._recv(":CHAN{}:DISP?".format(ch)) == "1"]

    def settings(self):
        """Snapshot of settings relevant for interpreting a capture."""
        s = {
            "idn": self.idn(),
            "sample_rate": float(self._recv(":ACQ:SRAT?")),
            "memory_depth": self._recv(":ACQ:MDEP?"),
            "timebase_scale_s": float(self._recv(":TIM:MAIN:SCAL?")),
            "timebase_offset_s": float(self._recv(":TIM:MAIN:OFFS?")),
            "trigger": {
                "mode": self._recv(":TRIG:MODE?"),
                "source": self._recv(":TRIG:EDG:SOUR?"),
                "slope": self._recv(":TRIG:EDG:SLOP?"),
                "level_v": float(self._recv(":TRIG:EDG:LEV?")),
            },
            "channels": {},
        }
        for ch in self.enabled_channels():
            s["channels"][ch] = {
                "scale_v": float(self._recv(":CHAN{}:SCAL?".format(ch))),
                "offset_v": float(self._recv(":CHAN{}:OFFS?".format(ch))),
                "probe": float(self._recv(":CHAN{}:PROB?".format(ch))),
                "coupling": self._recv(":CHAN{}:COUP?".format(ch)),
            }
        return s

    # --- Data ---

    def _preamble(self):
        p = self._recv(":WAV:PRE?").split(",")
        return {
            "points": int(p[2]),
            "xinc": float(p[4]),
            "xorig": float(p[5]),
            "xref": float(p[6]),
            "yinc": float(p[7]),
            "yorig": float(p[8]),
            "yref": float(p[9]),
        }

    def read_channel_raw(self, ch, max_points=None):
        """
        Read the full acquisition memory of one channel as raw ADC bytes.
        The scope must be stopped. Returns (bytes, preamble).
        """
        self._send(":WAV:SOUR CHAN{}".format(ch))
        self._send(":WAV:FORM BYTE")
        # Firmware 00.04.04 reports xorig=0 in RAW mode. NORM mode reports it correctly for
        # the screen; RAW memory is centred on the screen, so derive xorig from that (t=0 is trigger).
        self._send(":WAV:MODE NORM")
        norm = self._preamble()
        screen_centre = norm["xorig"] + norm["points"] * norm["xinc"] / 2
        self._send(":WAV:MODE RAW")
        pre = self._preamble()
        pre["xorig_reported"] = pre["xorig"]
        pre["xorig"] = screen_centre - pre["points"] * pre["xinc"] / 2
        total = pre["points"] if max_points is None else min(pre["points"], max_points)
        data = bytearray()
        start = 1
        while start <= total:
            stop = min(start + MAX_CHUNK - 1, total)
            # STOP before STAR: STAR may not exceed the current STOP.
            self._send(":WAV:STOP {}".format(stop))
            self._send(":WAV:STAR {}".format(start))
            data += self._recv_block(":WAV:DATA?")
            if self.debug:
                print("DEBUG::Rigol CH{}: {}/{} points".format(ch, len(data), total))
            start = stop + 1
        return bytes(data), pre

    def capture(self, channels=(1,), max_points=None):
        """Stop the scope and read the given channels. Returns a Capture."""
        self.stop()
        enabled = self.enabled_channels()
        for ch in channels:
            if ch not in enabled:
                raise ValueError("CH{} is not enabled on the scope (enabled: {})".format(ch, enabled))
        meta = self.settings()
        meta["captured_at"] = datetime.datetime.now().astimezone().isoformat(timespec="seconds")
        meta["preamble"] = {}
        volts = {}
        time_s = None
        for ch in channels:
            raw, pre = self.read_channel_raw(ch, max_points)
            volts[ch] = [(b - pre["yorig"] - pre["yref"]) * pre["yinc"] for b in raw]
            meta["preamble"][ch] = pre
            if time_s is None:
                time_s = [(i - pre["xref"]) * pre["xinc"] + pre["xorig"] for i in range(len(raw))]
        n = min(len(v) for v in volts.values())
        for ch in volts:
            volts[ch] = volts[ch][:n]
        return Capture(time_s[:n], volts, meta)

    def screenshot(self, path, overwrite=False):
        """Save the current screen as PNG."""
        if os.path.exists(path) and not overwrite:
            raise FileExistsError("File already exists: {}".format(path))
        data = self._recv_block(":DISP:DATA? ON,OFF,PNG")
        os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
        with open(path, "wb") as f:
            f.write(data)


def _meta_path(csv_path):
    return os.path.splitext(csv_path)[0] + ".json"


def main(argv=None):
    ap = argparse.ArgumentParser(description="Capture waveforms from a Rigol DS1000Z over LAN.")
    ap.add_argument("ip", help="scope IP address")
    ap.add_argument("-c", "--channels", type=int, nargs="+", default=[1], help="channels to read (default: 1)")
    ap.add_argument("-o", "--out", help="output CSV path (sidecar .json written alongside)")
    ap.add_argument("--single", action="store_true", help="arm a single trigger and wait before reading")
    ap.add_argument("--timeout", type=float, default=30.0, help="trigger wait timeout in s (default: 30)")
    ap.add_argument("--max-points", type=int, help="limit points per channel (default: full memory)")
    ap.add_argument("--screenshot", help="save a PNG screenshot to this path")
    ap.add_argument("--debug", action="store_true")
    args = ap.parse_args(argv)

    with DS1000Z(args.ip, debug=args.debug) as scope:
        print(scope.idn())
        if args.single:
            print("Waiting for trigger...")
            scope.single(args.timeout)
        if args.out:
            cap = scope.capture(args.channels, args.max_points)
            cap.write_csv(args.out)
            print("Wrote {} points x {} channel(s) at {:.3g} Sa/s to {}".format(
                len(cap.time_s), len(cap.volts), cap.meta["sample_rate"], args.out))
        if args.screenshot:
            scope.screenshot(args.screenshot)
            print("Wrote screenshot to {}".format(args.screenshot))
        if not args.out and not args.screenshot:
            print(json.dumps(scope.settings(), indent=2))


if __name__ == "__main__":
    sys.exit(main())
