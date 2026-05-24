# waymesh-node protobufs (Meshtastic client compat)

`waymesh_mesh.proto` is a **trimmed, import-free subset** of the Meshtastic
client protocol ([meshtastic/protobufs](https://github.com/meshtastic/protobufs)
`mesh.proto`), holding only the messages/fields the Phase G BLE-GATT gateway
emits or consumes. Field numbers are kept in lockstep with upstream so a full
Meshtastic client decodes our frames; enum-typed fields are declared as
`uint32`/scalars here (enums are varints on the wire). See
`../docs/hybrid-mesh/11-mobile-gateway-meshtastic-compat.md`.

## Vendored, not generated at build time

The generated nanopb code (`waymesh_mesh.pb.{c,h}`) and the pinned nanopb
runtime (`pb*.{c,h}`, `pb.h`) are vendored in `../lib/nanopb/` and committed —
so `pio run` just compiles C, with no protoc/codegen dependency on the build
host or CI. Regenerate only when editing the `.proto`/`.options`.

## Regenerate

Requires the nanopb generator (pinned to the runtime version in
`../lib/nanopb/pb.h`, `PB_PROTO_HEADER_VERSION 40` → nanopb 0.4.9.x) plus a
protoc. One-time setup:

```bash
python3 -m venv /tmp/nanopb-gen && \
  /tmp/nanopb-gen/bin/pip install grpcio-tools protobuf
git clone --depth 1 --branch 0.4.9.1 https://github.com/nanopb/nanopb.git /tmp/nanopb-0491
```

Then, from `projects/waymesh-node/`:

```bash
/tmp/nanopb-gen/bin/python /tmp/nanopb-0491/generator/nanopb_generator.py \
  -I proto -D lib/nanopb proto/waymesh_mesh.proto
```

`waymesh_mesh.options` (co-located, auto-detected) makes the variable-length
fields fixed-size C arrays. If you bump the nanopb version, re-copy the runtime
`pb*.{c,h}` / `pb.h` from the matching nanopb checkout into `../lib/nanopb/`.
