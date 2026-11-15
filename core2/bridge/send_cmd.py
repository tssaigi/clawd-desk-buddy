#!/usr/bin/env python3
"""Send serial commands to the Core2 and print its replies, without resetting it.

    python3 send_cmd.py info
    python3 send_cmd.py "bootqr off"
    python3 send_cmd.py "clawd visit left" "page colony"

Pause the bridge first (pause.sh): it holds the serial port. Needs pyserial.
Set CLAWD_PORT to force a port.
"""
import os, sys, time

import serial
from serial.tools import list_ports

CORE2_USB = {(0x1A86, 0x55D4), (0x10C4, 0xEA60)}


def find_port():
    if os.environ.get("CLAWD_PORT"):
        return os.environ["CLAWD_PORT"]
    found = sorted(p.device for p in list_ports.comports()
                   if (p.vid, p.pid) in CORE2_USB and "/dev/tty." not in p.device)
    if not found:
        sys.exit("No Core2 found on USB. Is it plugged in with a data cable?")
    return found[0]


def main():
    cmds = sys.argv[1:] or ["info"]
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = find_port(), 115200, 0.2
    s.dtr = False          # keep the reset line quiet: the device must not restart
    s.rts = False
    s.open()
    time.sleep(0.3)
    s.reset_input_buffer()
    for c in cmds:
        s.write((c + "\n").encode())
        print(">>> " + c)
        end = time.time() + 2
        while time.time() < end:
            line = s.readline()
            if line:
                print(line.decode(errors="replace").rstrip()[:300])


if __name__ == "__main__":
    main()
