"""Read Pocketmate's USB console without toggling reset/boot lines."""
import argparse
import sys
import time
import serial

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--port", required=True)
parser.add_argument("--seconds", type=float, default=60)
args = parser.parse_args()
port = serial.Serial(port=None, baudrate=115200, timeout=0.2)
port.dtr = False
port.rts = False
port.port = args.port
port.open()
try:
    until = time.monotonic() + args.seconds
    while time.monotonic() < until:
        line = port.readline()
        if line:
            sys.stdout.write(line.decode("utf-8", errors="replace"))
            sys.stdout.flush()
finally:
    port.close()
