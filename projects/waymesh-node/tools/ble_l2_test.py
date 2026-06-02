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
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from meshtastic.protobuf import mesh_pb2, channel_pb2

SERVICE = "6ba1b218-15a8-461f-9fa8-5dcae273eafd"
TORADIO = "f75c76d2-129e-4dad-a1dd-7866124401e7"
FROMRADIO = "2c55e69e-4993-11ed-b878-0242ac120002"
FROMNUM = "ed9da18c-a800-4f66-a670-aa7547e34453"

ADDR = sys.argv[1] if len(sys.argv) > 1 else "DC:06:75:B1:44:BA"
NONCE = 0x12345678
# Optional 2nd arg: a text to send on the primary channel (write-path test,
# §7.3). It is CTR-encrypted exactly like the stock app, so it exercises the
# node's wm_meshtastic_ctr decrypt -> v2 TEXT beacon flood. Watch the node serial
# for a "tx ... text ch=8" line after.
SEND_TEXT = sys.argv[2] if len(sys.argv) > 2 else None
DEFAULT_PSK = bytes.fromhex("d4f1bb3a20290759f0bcffabcf4e6901")  # LongFast psk idx 1


def mt_ctr(key, from_node, packet_id, data):
    """Meshtastic channel AES-CTR: nonce = packetId(u64 LE) | fromNode(u32 LE) |
    4 zero counter bytes (doc 13 §7.3). Symmetric (encrypt == decrypt)."""
    nonce = (packet_id.to_bytes(8, "little") + from_node.to_bytes(4, "little")
             + b"\x00\x00\x00\x00")
    enc = Cipher(algorithms.AES(key), modes.CTR(nonce)).encryptor()
    return enc.update(data) + enc.finalize()


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
        # FromNum notify is only a "new data" hint; the handshake test drains
        # FromRadio by polling regardless. BlueZ sometimes rejects the CCCD write
        # on a freshly re-flashed device (stale GATT cache) — don't let that abort
        # the actual protocol check.
        notify_ok = False
        try:
            await client.start_notify(
                FROMNUM, lambda _, d: fromnum.append(int.from_bytes(d, "little")))
            notify_ok = True
        except Exception as e:
            print("# FromNum notify subscribe failed (%s) — polling FromRadio "
                  "directly" % e)

        tr = mesh_pb2.ToRadio()
        tr.want_config_id = NONCE
        raw = tr.SerializeToString()
        print("# write ToRadio{want_config_id=0x%08x} (%d B): %s"
              % (NONCE, len(raw), raw.hex()))
        await client.write_gatt_char(TORADIO, raw, response=True)

        await asyncio.sleep(0.5)  # let the node queue + notify

        seen = {"my_info": 0, "node_info": 0, "metadata": 0,
                "channel": 0, "config_complete_id": 0, "other": 0}
        done = False
        my_num = 0
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
                my_num = fr.my_info.my_node_num
                print("     my_node_num=0x%08x min_app=%d"
                      % (fr.my_info.my_node_num, fr.my_info.min_app_version))
            elif which == "metadata":
                m = fr.metadata
                print("     fw=%r hw_model=%d hasBluetooth=%s role=%d"
                      % (m.firmware_version, m.hw_model, m.hasBluetooth, m.role))
            elif which == "channel":
                # The step-5a advertise: a stock-protobuf Channel the app adopts.
                # psk is the STORED Meshtastic PSK (1-byte index or raw key), so
                # the app derives the same channel hash + key we use (doc 13 §4).
                ch = fr.channel
                role = channel_pb2.Channel.Role.Name(ch.role)
                print("     index=%d role=%s name=%r psk=%s id=%d"
                      % (ch.index, role, ch.settings.name,
                         ch.settings.psk.hex() or "(none)", ch.settings.id))
            elif which == "node_info":
                n = fr.node_info
                print("     num=0x%08x user.id=%r long=%r short=%r channel=%d"
                      % (n.num, n.user.id, n.user.long_name, n.user.short_name,
                         n.channel))
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

        if notify_ok:
            await client.stop_notify(FROMNUM)
        print("# FromNum notifications:", fromnum)
        ok = (done and seen["my_info"] and seen["metadata"]
              and seen["node_info"] and seen["channel"])
        print("\n%s: Meshtastic handshake %s (%s)"
              % ("PASS" if ok else "FAIL",
                 "completed" if ok else "incomplete", seen))

        # Write-path test (§7.3): send a CTR-encrypted channel text the way the
        # stock app does, so the node decrypts (wm_meshtastic_ctr) + floods a v2
        # TEXT beacon. mp.from = our node num so the node's nonce matches ours.
        if SEND_TEXT and my_num:
            inner = mesh_pb2.Data()
            inner.portnum = 1  # TEXT_MESSAGE_APP
            inner.payload = SEND_TEXT.encode("utf-8")
            pid = 0x77777777
            ct = mt_ctr(DEFAULT_PSK, my_num, pid, inner.SerializeToString())
            mp = mesh_pb2.MeshPacket()
            setattr(mp, "from", my_num)  # 'from' is a Python keyword
            mp.to = 0xFFFFFFFF
            mp.channel = 0
            mp.id = pid
            mp.encrypted = ct
            out = mesh_pb2.ToRadio()
            out.packet.CopyFrom(mp)
            raw2 = out.SerializeToString()
            print("\n# write ToRadio{packet: encrypted text %r on ch0} (%d B)"
                  % (SEND_TEXT, len(raw2)))
            await client.write_gatt_char(TORADIO, raw2, response=True)
            await asyncio.sleep(0.5)
            print("# sent — watch the node serial for a 'tx ... text ch=8' beacon")
        return 0 if ok else 2


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
