"""Video demo: both channels change playlist every 20 s, looping through scenes.

Usage: python tools/demo_scenes.py [seconds]   (default 600; device carmotion.local)
"""
import sys, time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "mrroip" / "src"))
import mrroip

def on(f):  return {"cmd": "function_on", "arg": f}
def off(f): return {"cmd": "function_off", "arg": f}
def speed(v): return {"cmd": "speed", "arg": v}
STOP, GO = {"cmd": "stop"}, {"cmd": "continue"}
LIGHTS, HIGH, BEACON, LEFT, RIGHT, HAZARD = 0, 1, 2, 3, 4, 5

SCENES = [  # (name, ch1, ch2) - each scene also clears what the previous one switched on
    ("lights",     [speed(30), on(LIGHTS), on(HIGH)],          [speed(50), on(LIGHTS), off(HIGH)]),
    ("hazard",     [speed(30), on(HAZARD), off(HIGH)],         [speed(50), off(HAZARD)]),
    ("beacons",    [speed(40), on(BEACON), off(HAZARD)],       [speed(40), off(BEACON), off(HAZARD)]),
    ("indicators", [on(LEFT), off(RIGHT), off(BEACON)],        [on(RIGHT), off(LEFT), off(BEACON)]),
    ("stop+hazard",[STOP, off(LEFT), off(RIGHT), on(HAZARD)],  [GO, off(HAZARD)]),
    ("go",         [GO, off(HAZARD), off(LIGHTS)],             [GO, speed(50), off(LIGHTS)]),
]
STEP = 20.0
dev = mrroip.Device("carmotion.local")
end = time.time() + float(sys.argv[1] if len(sys.argv) > 1 else 600)
try:
    while time.time() < end:
        for name, ch1, ch2 in SCENES:
            print(time.strftime("%H:%M:%S"), name, flush=True)
            t = time.time() + STEP
            while time.time() < t:
                dev.control(mode="transmit", objects={"ch1": ch1, "ch2": ch2})
                time.sleep(0.5)
finally:
    dev.control(mode="release")
