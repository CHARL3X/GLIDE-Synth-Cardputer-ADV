# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Copyright (C) 2026 Charles Tobin (CHARL3X)
"""Field logger for LISTEN verdicts: capture the serial evidence of bad landings.

Run it in a terminal tab and leave it there while testing:

    ~/.platformio/penv/bin/python support/listen_log.py

Everything the device prints lands timestamped in listen_log.txt (next to
this script's working directory), including the [listen] chroma + verdict
lines that let a wrong landing be tuned from numbers instead of vibes.

Type a line and press enter at any moment to drop a note into the log at
that spot — e.g. the song's name and what the right key actually was:

    wrong: Karma Police, said E MIN, should be A MIN

Unplugging the device (SD swaps, flashing) is fine: the logger says so,
waits, and reattaches when the port comes back.
"""
import glob
import sys
import threading
import time

import serial

BAUD = 115200
LOG = "listen_log.txt"


def find_port():
    ports = glob.glob("/dev/cu.usbmodem*")
    return ports[0] if ports else None


def stamp():
    return time.strftime("%H:%M:%S")


def write(line):
    with open(LOG, "a") as f:
        f.write(line + "\n")


def note_reader():
    for line in sys.stdin:
        text = line.strip()
        if text:
            entry = "%s NOTE: %s" % (stamp(), text)
            write(entry)
            print("  -> logged")


def main():
    print("logging to %s — type a note + enter to mark a bad landing" % LOG)
    threading.Thread(target=note_reader, daemon=True).start()
    said_waiting = False
    while True:
        port = find_port()
        if not port:
            if not said_waiting:
                print("waiting for device...")
                said_waiting = True
            time.sleep(2)
            continue
        said_waiting = False
        try:
            with serial.Serial(port, BAUD, timeout=1) as s:
                msg = "%s --- connected %s" % (stamp(), port)
                print(msg)
                write(msg)
                while True:
                    raw = s.readline()
                    if not raw:
                        continue
                    text = raw.decode("utf-8", "replace").rstrip()
                    if text:
                        entry = "%s %s" % (stamp(), text)
                        write(entry)
                        print(entry)
        except (serial.SerialException, OSError):
            msg = "%s --- disconnected (SD swap? unplug?)" % stamp()
            print(msg)
            write(msg)
            time.sleep(2)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nbye — log is in %s" % LOG)
