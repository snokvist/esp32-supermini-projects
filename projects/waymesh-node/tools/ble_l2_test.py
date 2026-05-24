#!/usr/bin/env python3
"""Layer-2 verification: drive the Meshtastic want_config_id handshake over BLE
and decode the node's FromRadio frames with the *official* Meshtastic protobufs.

This bypasses the `meshtastic` CLI's own connection layer (BlueZ pairing / scan
quirks) to test the waymesh-node gateway protocol directly: encode ToRadio with
mesh_pb2, write it, drain FromRadio, and decode each frame as mesh_pb2.FromRadio.
A PASS means our trimmed proto is wire-compatible with upstream Meshtastic.

Run with the meshtastic-cli venv (it ships bleak + the protobufs):
  /home/snokvist/.local/share/meshtastic-cli-venv/bin/python ble_l2_test.py [ADDR]
"""
import asyncio
import sys

from bleak import BleakClient, BleakScanner
from meshtastic.protobuf import mesh_pb2

SERVICE = "6ba1b218-15a8-461f-9fa8-5dcae273eafd"
TORADIO = "f75c76d2-129e-4dad-a1dd-7866124401e7"
FROMRADIO = "2c55e69e-4993-11ed-b878-0242ac120002"
FROMNUM = "ed9da18c-a800-4f66-a670-aa7547e34453"

ADDR = sys.argv[1] if len(sys.argv) > 1 else "DC:06:75:B1:44:BA"
NONCE = 0x12345678


async def main():
    print("# locating", ADDR)
    dev = await BleakScanner.find_device_by_address(ADDR, timeout=15.0)
    if not dev:
        dev = await BleakScanner.find_device_by_filter(
            lambda d, ad: (d.name or "").startswith("Waymesh_"), timeout=10.0)
    if not dev:
        print("FAIL: device not found")
        return 1
    print("# found", dev.name, dev.address)

    async with BleakClient(dev) as client:
        print("# connected")
        fromnum = []
        await client.start_notify(
            FROMNUM, lambda _, d: fromnum.append(int.from_bytes(d, "little")))

        tr = mesh_pb2.ToRadio()
        tr.want_config_id = NONCE
        raw = tr.SerializeToString()
        print("# write ToRadio{want_config_id=0x%08x} (%d B): %s"
              % (NONCE, len(raw), raw.hex()))
        await client.write_gatt_char(TORADIO, raw, response=True)

        await asyncio.sleep(0.5)  # let the node queue + notify

        seen = {"my_info": 0, "node_info": 0, "metadata": 0,
                "config_complete_id": 0, "other": 0}
        done = False
        for i in range(20):
            data = await client.read_gatt_char(FROMRADIO)
            if not data:
                print("# FromRadio drained after %d frames" % i)
                break
            fr = mesh_pb2.FromRadio()
            try:
                fr.ParseFromString(bytes(data))
            except Exception as e:
                print("FAIL: FromRadio parse error on frame %d (%d B %s): %s"
                      % (i, len(data), bytes(data).hex(), e))
                return 2
            which = fr.WhichOneof("payload_variant")
            seen[which if which in seen else "other"] += 1
            print("#  frame %d (%d B): %s" % (i, len(data), which))
            if which == "my_info":
                print("     my_node_num=0x%08x min_app=%d"
                      % (fr.my_info.my_node_num, fr.my_info.min_app_version))
            elif which == "metadata":
                m = fr.metadata
                print("     fw=%r hw_model=%d hasBluetooth=%s role=%d"
                      % (m.firmware_version, m.hw_model, m.hasBluetooth, m.role))
            elif which == "node_info":
                n = fr.node_info
                print("     num=0x%08x user.id=%r long=%r short=%r"
                      % (n.num, n.user.id, n.user.long_name, n.user.short_name))
                if n.HasField("position"):
                    p = n.position
                    print("     pos lat=%.6f lon=%.6f sats=%d"
                          % (p.latitude_i / 1e7, p.longitude_i / 1e7,
                             p.sats_in_view))
            elif which == "config_complete_id":
                print("     config_complete_id=0x%08x (nonce match: %s)"
                      % (fr.config_complete_id, fr.config_complete_id == NONCE))
                done = fr.config_complete_id == NONCE
                break

        await client.stop_notify(FROMNUM)
        print("# FromNum notifications:", fromnum)
        ok = (done and seen["my_info"] and seen["metadata"]
              and seen["node_info"])
        print("\n%s: Meshtastic handshake %s (%s)"
              % ("PASS" if ok else "FAIL",
                 "completed" if ok else "incomplete", seen))
        return 0 if ok else 2


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
