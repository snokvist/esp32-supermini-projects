# 04 — Architecture (near-term: flat 2.4-LoRa + Meshtastic BLE-GATT)

> **Scope.** This describes **what we're building now**: a single-radio node that
> runs a **flat 2.4 GHz LoRa mesh** and is inspected/controlled by the **stock
> Meshtastic app over BLE-GATT** ([11](11-mobile-gateway-meshtastic-compat.md)).
> No ESP-NOW, no clusters, no aggregation, no super-frame yet. The full hybrid
> two-plane mesh those add up to — and the power/airtime thesis that justifies a
> second radio — is the **end goal**, kept in
> [12](12-end-goal-full-hybrid-mesh.md).

## The radio we use now: LR1121 2.4 GHz LoRa

The XR2's LR1121 ([02](02-hardware-and-rf-platform.md)) runs **2.4 GHz LoRa** as
the one and only mesh link near-term:

- **Cheap to listen (~6 mA RX)**, modest to talk (~30–50 mA TX at +10/+13 dBm), so
  a node stays in RX continuously and is always reachable.
- **Range is the SF/BW knob:** high SF + narrow BW = more range, more airtime; low
  SF + wide BW = less range, short airtime. Measured in P3 ([09](09-poc-roadmap.md)).

The C3's own 2.4 GHz radio is used near-term **only for BLE** (the Meshtastic
gateway). Its WiFi/ESP-NOW capability is deferred — the power asymmetry that makes
ESP-NOW worth adding as a second plane is documented in
[12 §1](12-end-goal-full-hybrid-mesh.md#1--the-two-plane-architecture).

## The flat mesh

Every node beacons its own **presence + GPS** on the LoRa channel and relays
**text** by **managed flood** — the Meshtastic-style discipline, minus the
clustering:

```
        node A ──beacon(presence/GPS)──>  (broadcast, hop-limited)
        node B ──beacon──>  ... every node carries its own position ...
        text   ──flood──>  dedup by MessageID · hop limit · SNR-delay · overhear-suppress
```

- **No aggregation:** each node's position is its own frame (~20 B). Digest
  aggregation (`O(clusters)` airtime) is the end-goal win → [12 §2](12-end-goal-full-hybrid-mesh.md#2--protocol-extensions-for-aggregation--dtn).
- **Managed flood for multi-hop** (P5): seen-set dedup, per-class hop limit,
  SNR-proportional rebroadcast delay, overhear suppression — so text reaches
  beyond direct range without retransmission storms. Frame formats:
  [05](05-protocol.md).

## Addressing & identity (near-term subset)

- **NodeID:** 32-bit, from the C3 MAC (`!aabbccdd` in Meshtastic terms).
- **MessageID:** `(NodeID:32, seq:16)` → the dedup key everywhere.
- **GroupID (optional):** a shared 16-bit tag so co-located groups ignore each
  other's traffic on the shared band.

`ShortAddr` / `ClusterID` are cluster concepts — deferred with the local plane
([12 §1](12-end-goal-full-hybrid-mesh.md#addressing-extensions-beyond-the-near-term-nodeidmessageid)).
No IP, no routing tables; identity is flat.

## The UI: Meshtastic BLE-GATT gateway

The node exposes the **Meshtastic client GATT service** so an unmodified
Meshtastic app (phone/BLE) or CLI (PC/USB-serial) sees it as a Meshtastic device —
we reuse their apps as our UI instead of building one. Because the mesh is flat,
the gateway maps each node's LoRa beacon **directly** to a Meshtastic `NodeInfo`
/ `Position` (no digest de-aggregation yet). Full design, UUIDs, and the protobuf
handshake: [11 — Meshtastic Client Compatibility](11-mobile-gateway-meshtastic-compat.md).

### Flow — ephemeral phone gateway

```mermaid
sequenceDiagram
    participant Phone as Meshtastic app
    participant GW as Node (Gateway)
    Phone->>GW: BLE connect + bond (PIN)
    Note over GW: BLE takes the 2.4 GHz medium; LoRa RX pauses in the BLE slot
    GW-->>Phone: handshake: MyNodeInfo, NodeInfo per known node, positions, text
    Phone->>GW: ToRadio{ text } → decrypt (channel PSK) → flood onto LoRa
    Phone->>GW: BLE disconnect
    Note over GW: resume LoRa RX
```

## Near-term RF coexistence (two radios, not three)

BLE and 2.4-LoRa both live on the board; while a phone is connected, the BLE slot
preempts LoRa airtime (a connected phone *reduces* mesh throughput — fine for an
ephemeral gateway, not a tether). This is the **two-radio** coexistence question
measured in the near-term P7. The full **three-radio super-frame** (LoRa +
ESP-NOW + BLE) is the end-goal mechanism →
[12 §3](12-end-goal-full-hybrid-mesh.md#3--rf-coexistence-the-three-radio-problem).

## What we defer to the end goal ([12](12-end-goal-full-hybrid-mesh.md))

- The **ESP-NOW local plane** (fast local burst sync) — and the reason for the
  second radio (ESP-NOW also reaches LoRa-less ESP32 nodes).
- **Clusters, roles (Head/Relay), election**, and **digest aggregation** (the
  airtime scaling win).
- The **three-radio super-frame** time-division schedule.
- **DTN** store-and-forward across partitions.

## What we avoid (near-term and end-goal alike)

No proactive/link-state routing on LoRa; no always-on WiFi; no fixed
gateways/infrastructure (the gateway is ephemeral); no assumption that mesh always
helps — every strategy must show a measured win for some real scenario
([10](10-experiments-and-metrics.md)).
