#!/usr/bin/env python3
"""One-time GPS provisioning for Waymesh Tier-2 nodes (host-side, out-of-band).

The waymesh-8285 firmware is deliberately VENDOR-NEUTRAL: it parses standard
NMEA from whatever GPS is wired to UART0 and never emits configuration bytes.
This tool tunes a *u-blox* module once (the config persists in the module's
flash) so it streams only what the mesh needs:

  - 1 Hz navigation rate (the beacon is ~0.5 Hz, so 1 Hz is already 2x oversampled)
  - NMEA = RMC + GGA only on UART1 (GLL/GSA/GSV/VTG off) -> ~7x less UART traffic

It is SAFE on a non-u-blox module: it first polls UBX MON-VER and, if there is
no UBX reply (not a u-blox, or TX->GPS-RX not wired), it prints a notice and
leaves the module untouched. Nothing is ever auto-emitted by the firmware.

Wiring: GPS TX -> USB-UART RX, GPS RX -> USB-UART TX, GND, VCC (3.3-5 V).
Usage:  python3 gps_provision.py [--port /dev/ttyUSB0] [--baud 115200] [--ram-only]

Requires: pyserial, pyubx2  (pip install pyserial pyubx2)
"""
import argparse
import sys
import time

try:
    import serial
    from pyubx2 import (UBXMessage, UBXReader, POLL, TXN_NONE,
                        SET_LAYER_RAM, SET_LAYER_BBR, SET_LAYER_FLASH, UBX_PROTOCOL)
except ImportError as e:
    sys.exit(f"missing dependency: {e}\n  pip install pyserial pyubx2")

# 1 Hz nav + NMEA RMC/GGA only on UART1; everything else off.
CFG = [
    ("CFG_RATE_MEAS", 1000),                 # measurement period (ms) -> 1 Hz
    ("CFG_RATE_NAV", 1),                      # 1 nav solution per measurement
    ("CFG_MSGOUT_NMEA_ID_RMC_UART1", 1),
    ("CFG_MSGOUT_NMEA_ID_GGA_UART1", 1),
    ("CFG_MSGOUT_NMEA_ID_GLL_UART1", 0),
    ("CFG_MSGOUT_NMEA_ID_GSA_UART1", 0),
    ("CFG_MSGOUT_NMEA_ID_GSV_UART1", 0),
    ("CFG_MSGOUT_NMEA_ID_VTG_UART1", 0),
]


def read_ubx(ser, secs):
    ubr = UBXReader(ser, protfilter=UBX_PROTOCOL)
    out = []
    t0 = time.time()
    while time.time() - t0 < secs:
        try:
            _, parsed = ubr.read()
        except Exception:
            continue
        if parsed is not None:
            out.append(parsed)
    return out


def main():
    ap = argparse.ArgumentParser(description="One-time u-blox GPS provisioning")
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--ram-only", action="store_true",
                    help="apply to RAM only (do not persist to flash)")
    args = ap.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=0.1)

    # 1) Model check: poll MON-VER. No reply -> not u-blox / no TX wire -> abort.
    print(f"# {args.port} @ {args.baud}: polling MON-VER (u-blox model check) ...")
    ser.reset_input_buffer()
    ser.write(UBXMessage("MON", "MON-VER", msgmode=POLL).serialize())
    is_ublox = False
    for m in read_ubx(ser, 1.5):
        if m.identity == "MON-VER":
            is_ublox = True
            print(f"  u-blox: sw={getattr(m,'swVersion',b'')!r} hw={getattr(m,'hwVersion',b'')!r}")
            i = 1
            while True:
                ext = getattr(m, f"extension_{i:02d}", None)
                if ext is None:
                    break
                print(f"    {ext!r}")
                i += 1
        elif m.identity in ("ACK-ACK", "ACK-NAK"):
            is_ublox = True
    if not is_ublox:
        print("  no UBX reply — not a u-blox module (or TX->GPS RX not wired).")
        print("  leaving the module UNTOUCHED. The firmware parses its NMEA as-is.")
        ser.close()
        return 0

    # 2) Apply config.
    layers = SET_LAYER_RAM if args.ram_only else (SET_LAYER_RAM | SET_LAYER_BBR | SET_LAYER_FLASH)
    label = "RAM" if args.ram_only else "RAM+BBR+FLASH"
    print(f"# applying 1 Hz + RMC/GGA-only -> {label} ...")
    ser.reset_input_buffer()
    ser.write(UBXMessage.config_set(layers, TXN_NONE, CFG).serialize())
    res = None
    for m in read_ubx(ser, 1.5):
        if m.identity == "ACK-ACK":
            res = "ACK"
        elif m.identity == "ACK-NAK":
            res = "NAK"
    print(f"  CFG-VALSET: {res or 'no ack'}")
    if res != "ACK":
        print("  !! not acknowledged — config NOT applied.")
        ser.close()
        return 1

    # 3) Verify the resulting NMEA stream.
    print("# measuring NMEA for 3 s ...")
    ser.reset_input_buffer()
    buf = bytearray()
    t0 = time.time()
    while time.time() - t0 < 3.0:
        buf += ser.read(4096)
    heads = {}
    for line in bytes(buf).split(b"\n"):
        line = line.strip()
        if line.startswith(b"$") and b"," in line:
            h = line.split(b",")[0].decode("ascii", "replace")
            heads[h] = heads.get(h, 0) + 1
    hs = ", ".join(f"{k}x{v}" for k, v in sorted(heads.items())) or "none"
    print(f"  bytes={len(buf)}  headers: {hs}")
    print("# done. The config persists in the module — the firmware stays vendor-neutral.")
    ser.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
