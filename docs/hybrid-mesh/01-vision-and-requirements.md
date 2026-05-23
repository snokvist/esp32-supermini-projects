# 01 — Vision & Requirements

## Vision

A compact, battery-powered, GPS-capable node that fuses **long-range
low-bandwidth** communication with **local high-speed** peer synchronization
into a resilient, decentralized layer for **mobile groups, autonomous systems,
and off-grid coordination**.

The platform deliberately targets the gap between existing systems:

```
        bandwidth
          ^
   high   |   WiFi/ESP-NOW (local only)         <-- great locally, no reach
          |          \
          |           \  *** Waymesh niche ***
          |            \  (local burst + rationed long-range bridge)
          |             \
   low    |   LoRa mesh (Meshtastic) ------------ ExpressLRS (P2P control)
          +----------------------------------------------> range
              short            medium              long
```

We are *not* trying to be the best LoRa mesh or the best local protocol. We are
trying to be the best **stitch** between a dense local cluster and a sparse
long-range fabric, under mobility and tight power.

## Primary goals (restated as testable intents)

1. **Tiny self-contained mobile node** — wearable/vehicle/drone/robot footprint,
   small LiPo, GPS, minimal infrastructure. *Test:* node + radio + GPS fits a
   matchbox-class enclosure and runs days, not hours.
2. **Layered local + long-range comms** — local plane handles dense/low-latency
   traffic; long-range plane handles sparse/relay/store-and-forward. *Test:*
   measurable reduction in long-range airtime vs sending everything over LoRa.
3. **Group mobility networking** — tolerate nodes appearing/leaving, moving
   fast, sleeping. *Test:* maintain useful delivery under defined churn rates.
4. **Hybrid mesh evaluation** — decide *when* true mesh vs flooding vs
   hierarchy wins. *Test:* per-scenario comparison with numbers.
5. **Battery-friendly operation** — duty-cycled radios, event-driven comms,
   graceful low-power degradation. *Test:* days of runtime on a 1000 mAh cell
   in a realistic duty cycle.
6. **Lightweight mobile interaction** — phone can view nodes/positions/messages
   and change config without being always-on or RF-hostile. *Test:* phone is an
   *ephemeral* gateway, not a tether.
7. **Real-world usefulness** — continuously re-validate against concrete
   scenarios (below).
8. **RF coexistence understanding** — empirical behavior of two co-located
   2.4 GHz radios (WiFi/BLE + 2.4-LoRa) sharing one band. *Test:* quantified
   self-desense and a working time-division schedule.
9. **Incremental POCs** — small milestones, each answering "does this help?"
10. **Architecture over features** — resist feature creep.

## Non-goals (explicit, to prevent scope creep)

- Not a Meshtastic/ExpressLRS clone or drop-in replacement.
- No internet/cloud dependency, no account system, no OTA-from-cloud.
- No media transfer (voice/image/file) in the core; bytes, not megabytes.
- No high-throughput networking; long-range plane is intentionally low-rate.
- No always-on WiFi AP, no permanent phone tether.
- No polished consumer UI in the research phase.
- No security hardening beyond a documented threat model + basic auth/crypto
  hooks (full secure-messaging is a later, separate track).

## Target use cases (the scenarios we test against)

| Scenario | Topology shape | What dominates | Why hybrid helps |
|----------|----------------|----------------|------------------|
| Hiking team | small dense cluster, occasional spread | local chatter + rare long bridge | cheap local sync, LoRa only when spread out |
| Vehicle convoy | moving line, stable order | reference-group mobility, GPS sharing | head-of-convoy bridges, members sync locally |
| FPV / drone group | fast, intermittent, RF-hostile | low-latency local + telemetry digest | ESP-NOW burst locally, LoRa for the stragglers |
| Robotics / swarm | dense clusters + roaming scouts | state sync + sparse command | aggregation; scouts bridge on LoRa |
| Field event / ad-hoc | many clusters, partitioned | partition tolerance | DTN store-and-forward across gaps |
| Sensor relay | static-ish + mobile mule | delay-tolerant collection | mule carries data between islands |

If a use case shows no advantage over plain LoRa-mesh *or* plain ESP-NOW, that
is a finding — we document it and narrow scope.

## Success metrics

The program succeeds if we can demonstrate, with measurements:

- **Airtime win:** long-range channel airtime per node-hour is materially lower
  than a Meshtastic-style "everything floods on LoRa" baseline for the same
  delivered information (target: ≥3× reduction in dense scenarios via local
  aggregation).
- **Power win:** a typical member node achieves multi-day runtime on ≤1000 mAh
  while still being discoverable and reachable (target: ≥3 days).
- **Resilience:** useful delivery (defined per scenario) survives node churn and
  partitions that would break a static assumption.
- **Coexistence:** a documented, repeatable time-division schedule lets the two
  co-located 2.4 GHz radios (WiFi/BLE + 2.4-LoRa) share one band without crippling
  self-desense.
- **Decision clarity:** for each of flooding / routing / hierarchy, we can state
  the regime where it wins, backed by data.

## Design principles

1. **Cheap to listen, expensive to talk** — exploit LoRa's ~6 mA RX as the
   always-on plane; spend airtime/power deliberately.
2. **Aggregate before you bridge** — never forward N raw local updates over
   long-range when one digest will do.
3. **Suppress, don't storm** — overhear-based suppression, hop limits, dedup.
4. **Soft roles, hard hysteresis** — roles (head/relay/member) are negotiated
   and rotate, but with damping to avoid election thrash under mobility.
5. **Time is a shared resource** — treat airtime and the 2.4 GHz medium as a
   scheduled super-frame, not a free-for-all.
6. **Degrade, don't drop** — under low power or congestion, slow down beacons
   and shed optional traffic rather than going dark.
7. **Measure everything** — every node logs the metrics in
   [10](10-experiments-and-metrics.md); decisions follow data.
