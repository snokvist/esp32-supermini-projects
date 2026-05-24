#!/usr/bin/env python3
"""Host-side Layer-1 verification for the waymesh-node Meshtastic BLE-GATT server.

Connects to the Waymesh node, confirms the Meshtastic service + 3 core
characteristics are present, reads the FromRadio stub, subscribes to FromNum
notifications, and writes a probe to ToRadio (check the node's serial log for
the matching "# BLE ToRadio<- ..." line). No protobufs — this is the bare
transport check (Phase G, Layer 1). See docs/hybrid-mesh/11-*.md.
"""
import asyncio
import sys
from bleak import BleakClient, BleakScanner

SERVICE = "6ba1b218-15a8-461f-9fa8-5dcae273eafd"
TORADIO = "f75c76d2-129e-4dad-a1dd-7866124401e7"   # write
FROMRADIO = "2c55e69e-4993-11ed-b878-0242ac120002" # read
FROMNUM = "ed9da18c-a800-4f66-a670-aa7547e34453"   # read + notify

NAME_PREFIX = "Waymesh_"
NOTIFY_WAIT_S = 12  # FromNum bumps every 5s while connected -> expect >=2


def hx(b):
    return " ".join("%02X" % x for x in b) if b else "(empty)"


async def main():
    print("# scanning for %s* ..." % NAME_PREFIX)
    dev = await BleakScanner.find_device_by_filter(
        lambda d, ad: (d.name or "").startswith(NAME_PREFIX), timeout=15.0
    )
    if not dev:
        print("FAIL: no %s* device found in scan" % NAME_PREFIX)
        return 1
    print("# found %s  [%s]" % (dev.name, dev.address))

    async with BleakClient(dev) as client:
        print("# connected, mtu=%s" % getattr(client, "mtu_size", "?"))

        # --- service / characteristic discovery ---
        svc = next((s for s in client.services
                    if s.uuid.lower() == SERVICE), None)
        if not svc:
            print("FAIL: Meshtastic service %s not found" % SERVICE)
            return 1
        chars = {c.uuid.lower(): c for c in svc.characteristics}
        print("# service %s present, %d chars" % (svc.uuid, len(chars)))
        ok = True
        for label, uuid, want in (
            ("FromRadio", FROMRADIO, "read"),
            ("ToRadio", TORADIO, "write"),
            ("FromNum", FROMNUM, "notify"),
        ):
            c = chars.get(uuid)
            props = ",".join(c.properties) if c else "-"
            status = "OK" if (c and want in props) else "MISSING"
            if not c or want not in props:
                ok = False
            print("#   %-9s %s  props=[%s]  %s" % (label, uuid, props, status))
        if not ok:
            print("FAIL: characteristic set incomplete")
            return 1

        # --- read FromRadio stub (expect DE AD BE EF) ---
        val = await client.read_gatt_char(FROMRADIO)
        exp = bytes((0xDE, 0xAD, 0xBE, 0xEF))
        print("# FromRadio read = %s  (expect DE AD BE EF) -> %s"
              % (hx(val), "OK" if val == exp else "MISMATCH"))

        # --- subscribe FromNum, expect a rising counter ~every 5s ---
        notes = []

        def on_note(_, data):
            v = int.from_bytes(data, "little")
            notes.append(v)
            print("#   FromNum notify -> %d  (%s)" % (v, hx(data)))

        await client.start_notify(FROMNUM, on_note)
        print("# subscribed FromNum, waiting %ds for notifications..." % NOTIFY_WAIT_S)
        await asyncio.sleep(NOTIFY_WAIT_S)
        await client.stop_notify(FROMNUM)
        rising = len(notes) >= 1 and notes == sorted(notes)
        print("# FromNum: %d notifications %s -> %s"
              % (len(notes), notes, "OK" if rising else "FAIL"))

        # --- write ToRadio probe (check node serial for the echo) ---
        probe = bytes((0x01, 0x02, 0x03))
        await client.write_gatt_char(TORADIO, probe, response=True)
        print("# ToRadio wrote %s  -> check node serial for "
              "'# BLE ToRadio<- len=3 01 02 03'" % hx(probe))

        passed = (val == exp) and rising and ok
        print("\n%s: Layer-1 BLE transport %s"
              % ("PASS" if passed else "PARTIAL",
                 "verified" if passed else "see notes above"))
        return 0 if passed else 2


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
