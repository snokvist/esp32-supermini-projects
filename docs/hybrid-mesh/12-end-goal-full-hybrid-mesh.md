# 12 — End Goal: The Full Hybrid Mesh (deferred)

> **Status: deferred — designed, not built.** The near-term build is a **flat
> 2.4-LoRa node you inspect with the stock Meshtastic app** ([04](04-architecture.md)
> architecture · [05](05-protocol.md) protocol · [09](09-poc-roadmap.md) roadmap ·
> [11](11-mobile-gateway-meshtastic-compat.md) Meshtastic client). *This* document
> is where that node is **headed**: the full hybrid two-plane mesh that justifies
> carrying **two radios**. It absorbs the former docs 06 (RF coexistence), 07
> (power & runtime), 08 (mobility & topology), and the cluster/aggregation/DTN
> depth pulled out of 04/05. Resume here once the near-term node ships and proves
> the radio + the app path. Jump to [§6 deferred roadmap](#6--deferred-roadmap-phases)
> for the build order.

## The thesis in one paragraph

A small, battery-powered, GPS-capable node carries **one 2.4 GHz band but two
radios with complementary strengths**: the **LR1121** running **2.4 GHz LoRa**
for cheap-to-listen, longer-range, low-rate links, and the **ESP32-C3's own WiFi
radio (ESP-NOW)** for fast, bursty, local peer sync. Both live in 2.4 GHz on the
same tiny board, so they **cannot transmit at once** — the architecture is built
around **time-division (a super-frame)**. The non-obvious lever: **LR1121 2.4 GHz
LoRa RX costs ~6 mA while WiFi RX costs ~95 mA** — so LoRa is the always-listening
control/wake plane and ESP-NOW is a scheduled high-speed burst plane. On top of
that sits the scaling win: **aggregate-then-bridge** — local clusters collapse N
member updates into one LoRa digest, so long-range airtime is `O(clusters)`, not
`O(nodes)`. (Near-term, none of this exists yet: the LoRa mesh is flat and there
is no ESP-NOW plane — see [04](04-architecture.md).)

---

## 1 — The two-plane architecture

```
            ┌────────────── LONG-RANGE PLANE (LR1121, 2.4 GHz LoRa) ──────────────┐
            │  cheap to listen (~6 mA) · costs ~50 mA to talk · low rate · 100s m–~km │
            │  carries: presence/GPS digests, text, relay traffic, store-and-forward  │
            │  discipline: managed flood + hop limit + suppression + airtime budget   │
            └───────▲───────────────────────▲───────────────────────▲───────────────┘
                    │ bridge                 │ bridge                 │ bridge
            ┌───────┴────────┐      ┌────────┴───────┐      ┌─────────┴──────┐
            │   CLUSTER A    │      │   CLUSTER B    │      │   CLUSTER C    │
            │  ESP-NOW 2.4   │      │  ESP-NOW 2.4   │      │  ESP-NOW 2.4   │
            │  M M M (Head)  │      │  M (Head) M    │      │  M M (Head)    │
            └────────────────┘      └────────────────┘      └────────────────┘
              LOCAL PLANE             LOCAL PLANE              LOCAL PLANE

  *** Both planes are 2.4 GHz on the same board → they NEVER transmit at once. ***
```

- **Local plane (LP):** ESP-NOW over the C3 WiFi radio. Connectionless, ≤250 B
  (v1) / ≤1470 B (v2), ~1 Mbps. Used inside a cluster for discovery, fast state
  sync, intra-cluster messaging, and **aggregation**, in **scheduled bursts**
  (WiFi RX is power-expensive).
- **Long-range plane (LRP):** LR1121 2.4 GHz LoRa. Cheap RX (~6 mA), modest TX
  (~50 mA @ +13 dBm). Range set by SF/BW. Used **between** clusters and to
  lone/sparse nodes; only **Heads/Relays** transmit.

**Defining rule:** chatter stays local; only digests and selected traffic cross
the LRP.

### Why the split works on one band (the power asymmetry)

| | LR1121 2.4 LoRa RX | C3 WiFi RX |
|---|---|---|
| Current | ~6 mA | ~95 mA |
| Implication | listen ~continuously for days | must be bursted |

LoRa-2.4 is the **always-on control/wake plane**; ESP-NOW is the **on-demand
burst plane**. A node sits in LoRa RX sipping ~6 mA, hears a "sync window opening"
cue, briefly wakes WiFi for a fast ESP-NOW exchange, then drops WiFi. The
asymmetry is a property of the *radios*, not the *bands*, so it survives
single-band.

### The super-frame (the heart of the full design)

```
|<------------------------------ super-frame (250 ms – 2 s) -------------------------------->|
2.4 GHz medium : [ LoRa RX (cheap, default) ][ ESP-NOW window ][ sleep ][ LoRa TX? ][ BLE? ]
                   one user                     one user                  one user   one user
guard          :  | g |                       | g |                     | g |       | g |
```

- **Exactly one radio uses the medium at a time** — LoRa RX, ESP-NOW window,
  LoRa TX, and BLE-gateway slots are mutually exclusive.
- **LoRa RX fills the gaps** (cheap default) so a node is reachable on the LRP
  whenever it isn't bursting WiFi or transmitting LoRa.
- **ESP-NOW windows** are short and Head-aligned so members wake WiFi together.
- **LoRa TX** is Head/Relay-only, scheduled, airtime-budgeted.
- **Guard intervals (g)** absorb clock drift (sized in [§3](#3--rf-coexistence--the-three-radio-problem)).

### Node roles (soft, negotiated, hysteresis-damped)

| Role | Does | Power posture |
|------|------|---------------|
| **Member (M)** | participates in LP; listens on LRP; no LRP TX | lowest |
| **Cluster Head (H)** | elected per cluster; owns LRP airtime budget; aggregates cluster state → LoRa digest; de-aggregates inbound LoRa → LP | higher (LRP TX); rotates |
| **Relay (R)** | forwards LRP traffic between clusters under suppression rules | higher; only when topology needs it |
| **Gateway (GW)** | *ephemeral*: a node a phone connects to (BLE) to read/inject | transient (already near-term — see [11](11-mobile-gateway-meshtastic-compat.md)) |

Election inputs (computed locally, advertised in beacons):
`score = w1·battery + w2·local_centrality + w3·link_quality + w4·role_stability`.
Highest score becomes Head; ties broken by NodeID. Hysteresis: a challenger must
beat the incumbent by a margin for K consecutive windows.

### Addressing extensions (beyond the near-term NodeID/MessageID)

- **ShortAddr:** low 16 bits of NodeID, used within a cluster (collisions
  resolved at join).
- **ClusterID:** 16-bit, derived from the current Head's NodeID (changes on
  re-election; members learn it from beacons).

### The crux: what crosses the long-range plane

```
on local update U from the cluster:
  if U is purely intra-cluster (e.g. fine-grained position to a neighbor):
      -> stay local, never bridge
  else classify U:
      presence/GPS  -> AGGREGATE into the periodic cluster digest (don't send now)
      text/event    -> bridge now, but DEDUP and rate-limit per originator
      telemetry     -> SAMPLE/decimate to the configured rate, then aggregate
  before any LRP TX:
      if this super-frame's LoRa TX slot is taken / budget spent -> queue (S&F)
      if already overheard from another Head/Relay -> SUPPRESS
      apply hop limit; apply SNR-proportional rebroadcast delay
```

Three levers do the heavy lifting: **Aggregate** (N positions → one digest),
**Suppress** (don't repeat what's been relayed), **Ration** (per-node airtime
budget + the single LoRa TX slot, with store-and-forward for overflow).

### Mesh strategy per plane

- **Local plane:** small diameter, high churn → **flooding with dedup + overhear
  suppression**, no routing tables.
- **Long-range plane:** **managed flood** over *aggregated* traffic only: hop
  limit, dedup, SNR-proportional rebroadcast delay, relay election.
- **Across partitions:** **DTN bundle-lite** — hold-and-forward with
  summary-vector dedup; a mobile node carries bundles between islands.

### Flows

**Local discovery & sync (intra-cluster, ESP-NOW window):** members broadcast
presence + GPS deltas during the scheduled window; the Head replies with a
cluster-state digest; the window closes and nodes return to LoRa RX.

**Bridge a cluster to the world (aggregate → 2.4-LoRa):** the Head collapses N
member positions into one digest, waits for its LoRa TX slot (WiFi off),
transmits with hop limit + msgID; a Relay applies dedup + SNR-delay + suppression
and rebroadcasts; the receiving Head de-aggregates into its own local plane.

**Store-and-forward across a partition (mule):** an island node hands a bundle to
a moving node over LoRa when in range; the mule holds it out of range; later, in
range of the far island, it offers a summary vector, the far node requests missing
msgIDs, and the mule delivers.

### What we explicitly avoid

No proactive/link-state routing on the LRP; no always-on WiFi or always-on relay;
no fixed gateways/infrastructure (gateways are ephemeral); no global time master
(loose sync via Head beacons + guard intervals); no simultaneous 2.4 GHz TX.

---

## 2 — Protocol extensions for aggregation & DTN

The flat-mesh frame header, PRESENCE/GPS/TEXT classes, managed-flood discipline,
and the 2.4-LoRa airtime reference are **near-term** and live in
[05](05-protocol.md). The full mesh adds the following.

### Message classes added by the full mesh

| Class | Code | LP | LRP |
|-------|------|----|----|
| TELEMETRY | 0x4 | yes | sampled/aggregated |
| ROUTING/ROLE | 0x5 | election/role beacons | head/relay coordination |
| DTN_OFFER | 0x6 | n/a | summary vector |
| DTN_REQ/DATA | 0x7 | n/a | bundle pull/deliver |

### PRESENCE/GPS digest payload (the aggregation win)

```
 byte 0    : count N
 byte 1-4  : refLat (int32, 1e-7 deg)   } cluster reference, once per digest
 byte 5-8  : refLon (int32, 1e-7 deg)   }
 then N records, 7 bytes each:
   byte 0-1: shortAddr (16)
   byte 2-3: dLat (int16, ~1 m/LSB rel. to ref)   ±32 km range
   byte 4-5: dLon (int16, ~1 m/LSB rel. to ref)
   byte 6  : [ageQuant:4][battQuant:2][flags:2]    coarse freshness/battery
```

Size for N members: `8 (hdr) + 9 (ref+count) + 7N`.
- N=8 → **73 bytes**, ~240 ms at SF10/BW406 (2.4 GHz LoRa).
- 8 separate single-node position frames (~20 B each, ~114 ms) → **~912 ms**.
- **≈3.8× airtime saving from aggregation alone**, before suppression/dedup.

### Local plane frame (ESP-NOW)

Cheaper per byte within a window, so the LP can be generous. ≤250 B (v1) /
≤1470 B (v2); ESP-NOW FCS covers integrity.

```
LP header (8 bytes): same shape as the LRP header (ver/type, flags, srcNodeID, seq)

PRESENCE/ROLE beacon payload (intra-cluster, full fidelity):
 byte 0   : role (M/H/R/GW)
 byte 1-2 : electionScore (16)
 byte 3   : battery (%, 1 B)
 byte 4-7 : lat (int32, 1e-7 deg)
 byte 8-11: lon (int32, 1e-7 deg)
 byte 12  : numNeighbors
 byte 13-14: clusterID
 byte 15  : seqOfHeadDigest (so members know if they're current)
 ... optional neighbor list for centrality calc ...
```

The Head builds the LRP digest by collecting these LP beacons over a period and
collapsing the positions into the delta-encoded digest above.

### DTN (store-and-forward) frames

```
DTN_OFFER  : summary vector = list of held MessageIDs (NodeID:32, seq:16)
DTN_REQ    : list of MessageIDs the requester is missing
DTN_DATA   : { MessageID, class, TTL, payload }   one bundle
```

TTL is a coarse lifetime (quantized minutes) + optional hop budget. Dedup by
MessageID; **no custody transfer** — epidemic spread with summary-vector pruning.

### Open protocol questions

Optimal digest period vs mobility; ShortAddr collision rate (is 16 bit enough per
cluster?); is LR-FHSS worth it for digest-uplink robustness; selective-ACK vs
pure epidemic for TEXT; Bloom-filter sizing for the seen-set; a minimal MIC/auth
that fits the byte budget (security track).

---

## 3 — RF coexistence: the three-radio problem

On a single-band node this is **the central design problem** once the local plane
exists. The XR2 carries two 2.4 GHz radios millimetres apart; the C3 radio is
itself shared between WiFi (ESP-NOW) and BLE.

| Radio | Role | Concurrency |
|-------|------|-------------|
| C3 WiFi (ESP-NOW) | local burst plane | shares the C3 radio with BLE |
| C3 BLE | ephemeral phone gateway (near-term) | shares the C3 radio with WiFi |
| LR1121 2.4 GHz LoRa | long-range plane | separate chip/antenna, same band |

> **Near-term subset (BLE + 2.4-LoRa only).** Until the ESP-NOW plane lands, this
> is a **two-radio** problem: BLE (the Meshtastic gateway) vs the LR1121 LoRa
> link. That subset is measured in the active P7 ([09](09-poc-roadmap.md)). The
> full three-radio super-frame below is the end-goal version.

### The core problem

A +10…+13 dBm 2.4-LoRa TX a few mm from the WiFi/BLE RX front-end
**desensitizes or fully blocks** that receiver (near-field front-end overload),
and vice-versa. This is **independent of channel offset** — the victim's LNA is
swamped by raw nearby power, not co-channel energy.

**Hard rules:** (1) never transmit on two 2.4 GHz radios at once; (2) never
receive on one while another transmits nearby; (3) therefore **strict
time-division** between LoRa, WiFi/ESP-NOW, and BLE. ExpressLRS already lives by
this on the same hardware (WiFi "config mode" and the RF link are never
simultaneous) — our precedent.

### Residual coupling beyond the obvious

- **Supply/ground noise:** a 50 mA LoRa TX or 240 mA WiFi TX burst sags a small
  LiPo / the XR2 3.3 V rail; budget decoupling, stagger heavy TX.
- **Antenna coupling:** two 2.4 GHz antennas mm apart couple strongly; measure
  isolation even though TDM means they aren't active together.
- **Thermal:** sustained TX on a 0.8 g board heats the LR1121/PA; watch drift.

### Coexistence with the external RF environment

2.4 GHz is a warzone, and FPV/drone use cases make it worse (WiFi APs, BLE,
Zigbee, microwaves, other ESP-NOW/ELRS, co-located FPV control+video). ESP-NOW
uses CSMA/backoff; LoRa relies on processing gain + capture. With no sub-GHz
refuge here, mitigation is channel selection, low duty, high-SF LoRa for
processing gain, and the **GroupID** tag to ignore foreign traffic. No
EU868-style duty cap, but ETSI EN 300 328 / FCC 15.247 impose power +
adaptivity/medium-utilization expectations; the XR2's 10 mW is well within limits.

### SF / bandwidth — the range–airtime–robustness knob

- **High SF + narrow BW (e.g. SF12/BW203):** best sensitivity/range/capture,
  worst airtime → sparse long bridges.
- **Low SF + wide BW (e.g. SF8/BW812):** short airtime, less range → closer nodes,
  tight airtime.
- **FLRC:** fast, moderate range — possible middle option, but ESP-NOW already
  owns "fast local." Evaluate only if needed.

### Empirical test plan (the deferred three-radio Phase 7)

1. **Self-desense (2.4-LoRa TX → WiFi RX):** ESP-NOW PDR while the node's own
   LR1121 transmits at +10/+13 dBm → the PDR cliff that justifies strict TDM.
2. **Self-desense (WiFi TX → LoRa RX):** the reverse → symmetry of the problem.
3. **Antenna isolation:** S21 between the C3 and LR1121 2.4 antennas → isolation
   (dB) informs guard/scheduling.
4. **Supply coupling:** scope the 3.3 V rail during +13 dBm LoRa and 240 mA WiFi
   bursts on 250/500/1000 mAh cells → decoupling/brownout limits.
5. **Guard-interval adequacy:** run the full super-frame on 5 nodes; confirm zero
   concurrent 2.4 GHz TX; measure overlap vs C3 clock drift → minimum safe guard.
6. **External congestion sweep:** ESP-NOW + 2.4-LoRa PDR/latency vs offered 2.4 GHz
   load → the level where we shed optional traffic / change SF.
7. **LoRa-vs-LoRa coexistence:** PDR with N co-channel 2.4-LoRa interferers;
   capture-effect threshold → graceful-degradation curve.

(The near-term P7 runs a reduced version: tests 3, 4, and a BLE-vs-LoRa variant
of 1/2.)

---

## 4 — Power & runtime

Runtime is **RX-dominated**, and the dominant RX cost is *which* radio listens.
All numbers are datasheet-derived estimates; bench measurement replaces them.

### Per-state current budget (estimates)

| Subsystem / state | Current |
|---|---|
| LR1121 2.4 GHz LoRa RX (gap-filler, ~continuous) | ~6 mA |
| LR1121 2.4 GHz TX +10 / +13 dBm | ~30–40 / ~50 mA |
| LR1121 sleep (retention) | ~1.4 µA |
| C3 light-sleep (wake on LoRa IRQ) / deep-sleep | ~130 µA / ~5 µA |
| C3 "LoRa-listening idle" (budget) | ~1 mA |
| C3 WiFi RX (ESP-NOW window) | ~95 mA |
| C3 WiFi TX peak | ~180–240 mA |
| GNSS tracking | ~25 mA |

**Headline:** an idle but *reachable* node sits at **~7 mA** (LoRa RX 6 + C3 idle
1) — LoRa RX beats WiFi RX by ~16×. Everything above is duty-cycled.

### Duty-cycle contributions (swing factors)

`avg added = peak × (on-time / period)`:

| Activity | Example duty | Avg added |
|---|---|---|
| ESP-NOW window 100 ms / 10 s | 1 % | ~0.95 mA |
| ESP-NOW window 200 ms / 2 s | 10 % | ~9.5 mA |
| GPS fix 10 s / 5 min | 3.3 % | ~0.83 mA |
| GPS fix 5 s / 10 s (mobile, hot) | 50 % | ~12.5 mA |
| LRP digest 240 ms / 30 s @ +13 dBm | 0.8 % | ~0.4 mA |
| LRP beacon 114 ms / 60 s @ +10 dBm | 0.2 % | ~0.06 mA |

GPS and ESP-NOW windows dominate active profiles. **Mobility costs power.**

### Node profiles & runtime (80 % usable capacity)

| Profile | Avg current | 250 mAh | 1000 mAh | 2000 mAh |
|---|---|---|---|---|
| **A — Deep-standby member** | ~8.8 mA | ~23 h | ~3.8 d | ~7.6 d |
| **B — Active member (moving)** | ~20.7 mA | ~10 h | ~1.6 d | ~3.2 d |
| **C — Cluster Head (active)** | ~21.1 mA | ~9.5 h | ~1.6 d | ~3.2 d |
| **D — Lone sparse node** | ~8.0 mA | ~25 h | ~4.2 d | ~8.3 d |
| **E — Ultra-low beacon (wearable)** | ~2.0 mA | ~4.2 d | ~16.7 d | ~33 d |

These meet the ≥3-day standby target on ≤1000 mAh, multi-week for a duty-cycled
wearable — *if* GPS and ESP-NOW are disciplined.

### Strategies that produce these numbers

LoRa is the default gap-filler, WiFi is bursted (the 95-vs-6 mA lever);
synchronized wake windows; duty-cycle GPS behind a load switch (motion-gated);
ration LRP TX (Heads only, aggregate, lower power when margin allows); rotate the
Head role; graceful low-power degradation (slow beacons → widen GPS → drop
telemetry → shed Head, never go dark without a last low-battery beacon); LoRa RX
duty-cycling (preamble/CAD sniff) for sub-2 mA wearables.

### Caveats (why bench measurement is mandatory)

Peak-current sag on tiny cells (a 250 mAh LiPo may brown out on 240 mA WiFi or
+13 dBm LoRa peaks); regulator efficiency + quiescent draw aren't in datasheets;
cold temperature cuts LiPo capacity; GNSS cold-start TTFF spikes energy; the C3
~1 mA idle budget is a guess to be profiled.

---

## 5 — Mobility, topology & scalability

*When* does mesh help, *when* is flooding enough, *when* does localized hierarchy
win — under real mobility and the airtime/power budgets above.

### Mobility models

| Model | Fits | Behavior |
|-------|------|----------|
| Random Waypoint (RWP) | loose hiking group, ad-hoc event | random targets/speeds; baseline |
| **Reference Point Group Mobility (RPGM)** | convoy, drone swarm, robotics | group reference moves, members jitter → strong clustering |
| Manhattan grid | vehicles in a town | movement on a street graph |
| Lévy walk | humans on foot | short hops, occasional long ones |

RPGM is the most important — it produces the dense-cluster-plus-bridge topology
the architecture is built for.

### Link lifetime (`t_link ≈ 2·range / v_rel`)

| Plane | Range (XR2 integrated antenna) | Rel. speed | t_link |
|-------|-------|-----------|--------|
| ESP-NOW (local) | ~150 m | 2 m/s (walk) | ~150 s |
| ESP-NOW (local) | ~150 m | 20 m/s (drone) | **~15 s** |
| 2.4-LoRa (high SF) | ~500 m | 10 m/s (convoy) | ~100 s |
| 2.4-LoRa (high SF) | ~1 km | 20 m/s | ~100 s |

**Finding (to verify):** fast local movement shreds ESP-NOW links (~15 s for
drones) while the longer-range LoRa plane stays connected several times longer.
So **under high mobility, lean on the LRP**; the LP is best when the group is
co-located and slow.

### Topology dynamics the system must survive

Join/leave churn (soft state + TTLs); cluster merge (one Head wins by election +
hysteresis); cluster split (fission, each electing a Head, LRP stitches them);
orphan (lone node, LRP presence only); partition (DTN bridges in time via mules).

### Flooding vs routing vs hierarchy — the regimes

| Regime | Best strategy | Why |
|--------|---------------|-----|
| Small, dense, churny (a cluster) | **flood + dedup + suppression** | route maintenance > savings; tiny diameter |
| Sparse, mobile, no stable sinks | **managed flood (epidemic)** | proactive link-state too costly on a slow channel |
| Dense aggregate-able groups | **hierarchy: aggregate at Head, bridge on LRP** | collapses N updates → 1 digest |
| Stable topology, known sinks, low churn | **DV-lite routing** *can* beat flooding | route cost amortizes |
| Partitioned in time | **DTN bundle-lite** | only carry-and-forward crosses gaps |

The POC deliverable is to **draw these crossover lines with data**.

### Scalability: the airtime argument (why hierarchy is non-optional)

N = 40 nodes, position every 30 s, managed-flood redundancy R ≈ 3, SF10/BW406:

**Flat flood:** `40 · 0.114 s · 3 ≈ 13.7 s / 30 s` → ~46 % medium occupancy —
**infeasible** (starves ESP-NOW, saturates the band). Pure LoRa-flood is `O(N)`.

**Hybrid (cluster of 8 → 5 Heads bridge digests):** `5 · 0.24 s · 3 ≈ 3.6 s` →
~12 %, **~3.8× better**, and further reduced by suppression (R→~1.5 → ~6 %),
lower off-motion digest rate (~3 %), and lower SF where range allows.

**Conclusion:** aggregation makes LRP airtime `O(clusters)`; suppression + rate
adaptation keep the shared medium usable for both planes. **Hierarchy is what
makes the system scale** — the central claim to validate.

### Relay suppression toolbox

| Mechanism | Effect | Knob |
|-----------|--------|------|
| Duplicate suppression (seen-set) | drop repeats | seen-set size/TTL |
| Overhear suppression | cancel pending rebroadcast if already heard | listen window |
| SNR-proportional delay | best-placed relay goes first | delay scale, jitter |
| Hop limit | bound flood radius | initial TTL per class |
| Probabilistic forwarding (gossip) | thin redundancy at scale | forward prob p |
| Dominating-set-lite | only "central" nodes relay | centrality threshold |

The experiment is the **R-vs-PDR tradeoff curve** per mechanism.

### Cluster leadership mechanics

Election score (above), advertised in LP beacons; hysteresis (challenger must
exceed incumbent by Δ for K windows → prevents thrash, the failure mode to
watch); periodic rotation to share head current; merge/split detected via
neighbor-set overlap + cluster diameter with the same hysteresis.

### Simulation plan (cheap → faithful)

1. **Tier 1 — analytical airtime model** (the math above). Do first; already
   drives the architecture.
2. **Tier 2 — discrete-event sim** (Python): mobility (RWP/RPGM) + simple PHY +
   the real protocol logic. Where the crossover lines get drawn.
3. **Tier 3 — high-fidelity PHY** (ns-3 / OMNeT++ / FLoRa) only if Tier 2 leaves
   PHY questions open.
4. **Tier 4 — hardware-in-the-loop field trials** (the deferred P9).

### Open mobility questions

Election hysteresis constants vs RPGM speed; the link-lifetime threshold where
DV-lite beats flooding; real ESP-NOW range/PDR under motion; optimal cluster-size
target (too small → many Heads → more LRP airtime; too big → ESP-NOW congestion);
when to fission/fuse clusters.

---

## 6 — Deferred roadmap phases

The **active near-term track** (P0 ✓ → P3 link → Phase G Meshtastic client → P5
flat relay → P6/P7 two-radio power+coexistence → P9 capstone) lives in
[09](09-poc-roadmap.md). The phases below are the deferred local-plane / cluster
arc — resumed once the near-term node ships. Each keeps its original phase number.

### Phase 1 — Local discovery (the local plane)

- **Outcome:** nodes find each other over **ESP-NOW**; maintain a neighbor table
  with soft-state TTLs; presence beacons in the LP format ([§2](#2--protocol-extensions-for-aggregation--dtn)).
- **Validates:** local-plane discovery, churn handling, ESP-NOW range/PDR.
- **Experiments:** N=2..5; walk in/out of range; vary beacon rate; ESP-NOW
  standard vs LR mode range walk.
- **Go/No-Go:** reliable discovery < a few seconds; sane neighbor tables under churn.

### Phase 2 — GPS propagation (local)

- **Outcome:** positions shared on the LP; each node holds a live local map of
  neighbor positions.
- **Validates:** position encoding, freshness/age handling, the data the digest
  will aggregate.
- **Go/No-Go:** neighbor map accurate and timely at a sane beacon rate.

### Phase 4 — Cluster head election + aggregation (the core claim)

- **Outcome:** a local cluster elects a Head; the Head aggregates member positions
  into one LoRa digest; a second cluster's Head **de-aggregates** into its LP and,
  via the gateway shim ([11](11-mobile-gateway-meshtastic-compat.md)), expands the
  digest back to per-node `NodeInfo` for the phone.
- **Validates:** the **aggregate-then-bridge** thesis and the airtime win.
- **Experiments:** 2 clusters of 3–4; measure LRP airtime with aggregation on/off;
  force re-election (kill the Head).
- **Metrics:** **airtime with vs without aggregation** (expect ≥3×), election
  convergence time, thrash rate, cross-cluster position freshness.
- **Go/No-Go:** aggregation delivers a clear airtime reduction *and* elections
  converge without thrash. **The make-or-break gate for the aggregation thesis.**

### Phase 8 — Store-and-forward / DTN

- **Outcome:** bundle-lite carry-and-forward across a partition via a mule node.
- **Experiments:** two islands out of range; a node ferries between them; verify
  summary-vector dedup; TTL expiry.
- **Go/No-Go:** reliable eventual delivery across a partition without unbounded
  duplication/storage.

### Three-radio extensions of the active phases

- **P6 (power):** add the ESP-NOW windows + Head profiles (B/C/E above) and run
  the full super-frame duty-cycle for hours/days on real cells.
- **P7 (coexistence):** add the four ESP-NOW/WiFi tests (1, 2, 5, 6 in
  [§3](#3--rf-coexistence--the-three-radio-problem)) the near-term two-radio P7
  can't cover.
- **P9 (mobility):** the full convoy/hike/swarm trial with *all* planes, A/B vs a
  flat-flood baseline — the program-level go/no-go.

---

## Sources

- [Semtech LR1121 product page](https://www.semtech.com/products/wireless-rf/lora-connect/lr1121)
- [LR1121 datasheet (rev 2.0, PDF)](https://files.waveshare.com/wiki/Core1121/LR1121_H2_DS_v2_0.pdf)
- [ESP-NOW (ESP-IDF guide)](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/network/esp_now.html)
- [ESP32-C3 datasheet](https://documentation.espressif.com/esp32-c3_datasheet_en.html)
