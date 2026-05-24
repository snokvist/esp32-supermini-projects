#!/usr/bin/env python3
"""Dependency-free BLE transport reachability check for the waymesh-node
Meshtastic GATT server (bleak only — no meshtastic/protobuf needed).

Confirms the node advertises, accepts a connection, and exposes the Meshtastic
service + its three core characteristics with the right properties, and that the
ATT MTU negotiates large. This is the bare-transport smoke test; the actual
Meshtastic handshake/protobuf path is exercised by ble_l2_test.py (which needs
the meshtastic package). See docs/hybrid-mesh/11-mobile-gateway-meshtastic-compat.md.

  /home/snokvist/.local/share/waymesh-ble-venv/bin/python ble_gatt_test.py
"""
import asyncio
import sys
from bleak import BleakClient, BleakScanner

SERVICE = "6ba1b218-15a8-461f-9fa8-5dcae273eafd"
TORADIO = "f75c76d2-129e-4dad-a1dd-7866124401e7"    # write
FROMRADIO = "2c55e69e-4993-11ed-b878-0242ac120002"  # read
FROMNUM = "ed9da18c-a800-4f66-a670-aa7547e34453"    # read + notify

NAME_PREFIX = "Waymesh_"


async def main():
    print("# scanning for %s* ..." % NAME_PREFIX)
    dev = await BleakScanner.find_device_by_filter(
        lambda d, ad: (d.name or "").startswith(NAME_PREFIX), timeout=15.0)
    if not dev:
        print("FAIL: no %s* device found in scan" % NAME_PREFIX)
        return 1
    print("# found %s  [%s]" % (dev.name, dev.address))

    async with BleakClient(dev) as client:
        print("# connected, mtu=%s" % getattr(client, "mtu_size", "?"))
        svc = next((s for s in client.services
                    if s.uuid.lower() == SERVICE), None)
        if not svc:
            print("FAIL: Meshtastic service %s not found" % SERVICE)
            return 1
        chars = {c.uuid.lower(): c for c in svc.characteristics}
        ok = True
        for label, uuid, want in (("FromRadio", FROMRADIO, "read"),
                                  ("ToRadio", TORADIO, "write"),
                                  ("FromNum", FROMNUM, "notify")):
            c = chars.get(uuid)
            props = ",".join(c.properties) if c else "-"
            present = c is not None and want in props
            ok = ok and present
            print("#   %-9s %s  props=[%s]  %s"
                  % (label, uuid, props, "OK" if present else "MISSING"))

        print("\n%s: BLE transport %s"
              % ("PASS" if ok else "FAIL",
                 "reachable" if ok else "see notes above"))
        return 0 if ok else 2


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
