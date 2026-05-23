# 08 — Mobility, Topology & Scalability

The question this program exists to answer: *when* does mesh help, *when* is
flooding enough, and *when* does localized hierarchy win — under real mobility
and the airtime/power budgets from the previous docs.

## Mobility models (what we simulate and field-test)

| Model | Fits | Behavior |
|-------|------|----------|
| **Random Waypoint (RWP)** | loose hiking group, ad-hoc event | nodes pick random targets/speeds; classic baseline |
| **Reference Point Group Mobility (RPGM)** | convoy, drone swarm, robotics cluster | a group reference moves; members jitter around it → strong clustering |
| **Manhattan grid** | vehicles in a town | movement constrained to a street graph |
| **Lévy walk** | humans on foot | mostly short hops, occasional long ones |

RPGM is the most important — it produces the dense-cluster-plus-bridge topology
the architecture is built for.

## Link lifetime: why the long-range plane is *more* stable under mobility

Approximate time two nodes stay linked while crossing each other's range:
`t_link ≈ 2·range / v_rel`.

| Plane | Range (2.4 GHz, XR2 integrated antenna) | Rel. speed | t_link |
|-------|-------|-----------|--------|
| ESP-NOW (local) | ~150 m | 2 m/s (walk) | ~150 s |
| ESP-NOW (local) | ~150 m | 20 m/s (drone) | **~15 s** |
| 2.4-LoRa (high SF) | ~500 m | 10 m/s (convoy) | ~100 s |
| 2.4-LoRa (high SF) | ~1 km | 20 m/s | ~100 s |

**Finding (to verify):** fast local movement shreds ESP-NOW links (~15 s for
drones), while the longer-range 2.4-LoRa plane stays connected several times
longer. So **under high mobility, lean more on the LRP**; the LP is best when the
group is genuinely co-located and slow-moving. The margin is smaller than sub-GHz
would give (2.4-LoRa with a 0.8 g integrated antenna reaches hundreds of metres
to ~km, not multi-km), so the plane-split shift with speed matters more, not
less. Ranges are estimates — measure in Phase 3.

## Topology dynamics the system must survive

- **Join/leave churn:** nodes appear, sleep, die. Handled by soft state + TTLs;
  no hard association.
- **Cluster merge:** two clusters drift together → one Head should win
  (election + hysteresis) and the other demote. Avoid ping-pong with margins.
- **Cluster split:** a cluster spreads out → it must fission into two, each
  electing a Head; the LRP stitches them.
- **Orphan:** a node loses all local neighbors → becomes a lone node, presence
  only on the LRP, until it re-joins a cluster.
- **Partition:** whole groups out of range → DTN store-and-forward bridges in
  time, not space (mules).

## Flooding vs routing vs hierarchy — the regimes

| Regime | Topology | Best strategy | Why |
|--------|----------|---------------|-----|
| Small, dense, churny (a cluster) | local | **flood + dedup + suppression** | route maintenance > savings; diameter is tiny |
| Sparse, mobile, no stable sinks | long-range | **managed flood (epidemic)** | proactive link-state too costly on a slow channel under churn |
| Dense *aggregate-able* groups | mixed | **hierarchy: aggregate at Head, bridge on LRP** | collapses N updates → 1 digest; the scalability win |
| Stable topology, known sinks, low churn | either | **DV-lite routing** *can* beat flooding | route cost amortizes when links live long |
| Partitioned in time | any | **DTN bundle-lite** | only carry-and-forward delivers across gaps |

The deliverable from the POCs is to **draw these crossover lines with data**
(link-lifetime threshold where routing beats flooding; density where hierarchy
beats flat flood).

## Scalability: the airtime argument (why hierarchy is non-optional)

On 2.4 GHz there's no EU868 1 % legal cap, but the long-range plane shares the
super-frame (and the crowded band) with ESP-NOW/BLE, so **medium occupancy is the
hard ceiling** — every second the LoRa plane talks is a second the local plane
can't. Compare a Meshtastic-style flat flood vs the hybrid for **N = 40 nodes**,
position every 30 s, managed-flood redundancy R ≈ 3, 2.4-LoRa SF10/BW406:

**Flat flood (every node's position floods on LoRa):**
```
airtime/30s = N · ToA(20B) · R = 40 · 0.114 s · 3 ≈ 13.7 s   (out of 30 s!)
```
→ ~46 % medium occupancy — **infeasible**: it starves ESP-NOW, leaves no room in
the super-frame, and saturates an already-busy 2.4 GHz band. This is why pure
LoRa-flood collapses with density even without a duty-cycle law.

**Hybrid (aggregate per cluster of 8 → 5 Heads bridge digests):**
```
airtime/30s = C · ToA(73B digest) · R = 5 · 0.24 s · 3 ≈ 3.6 s
```
→ ~12 % — still significant, but **~3.8× better**, and further reduced by:
- **Suppression** drives R from ~3 toward ~1.5 → ~1.8 s (~6 %).
- **Lower digest rate** off-motion (60 s) → ~0.9 s (~3 %).
- **Lower SF/wider BW where range allows** (SF8/BW812 digest ~36 ms) → tiny.

**Conclusion:** flat flooding is `O(N)` airtime and hits the wall fast;
aggregation makes LRP airtime `O(clusters)`, and suppression + rate adaptation
keep the shared 2.4 GHz medium usable for both planes. **Hierarchy is what makes
the system scale**, which is the central architectural claim to validate.

## Relay suppression toolbox (and what we'll measure)

| Mechanism | Effect | Knob to tune |
|-----------|--------|--------------|
| Duplicate suppression (seen-set) | drop repeats | seen-set size/TTL |
| Overhear suppression | cancel pending rebroadcast if already heard | listen window |
| SNR-proportional delay | best-placed relay goes first, suppresses others | delay scale, jitter |
| Hop limit | bound flood radius | initial TTL per class |
| Probabilistic forwarding (gossip) | thin redundancy at scale | forward prob p |
| Dominating-set-lite | only "central" nodes relay | centrality threshold |

Each reduces the redundancy factor R (and thus airtime) at some cost to delivery
ratio. The experiment is the **R-vs-PDR tradeoff curve** per mechanism.

## Cluster leadership mechanics

- **Election score:** `w1·battery + w2·local_centrality + w3·link_quality +
  w4·role_stability`, advertised in LP beacons.
- **Hysteresis:** challenger must exceed incumbent by margin Δ for K windows →
  prevents thrash under mobility (the failure mode to watch).
- **Rotation:** even without a challenger, rotate periodically to share the
  higher head current ([07](07-power-and-runtime.md)).
- **Merge/split:** detect via neighbor-set overlap and cluster diameter; apply
  the same hysteresis to avoid oscillation.

## Simulation plan (cheap → faithful)

1. **Tier 1 — analytical airtime model** (Python/spreadsheet). The math above,
   parameterized by N, cluster size, rate, SF, R. *Output:* feasibility maps;
   already drives the architecture. **Do first.**
2. **Tier 2 — custom discrete-event sim** (Python, a few hundred lines):
   mobility (RWP/RPGM) + simple PHY (range threshold + collision/capture) +
   the actual protocol logic (election, aggregation, suppression, DTN). *Output:*
   PDR, latency, airtime, R, neighbor churn vs N / speed / cluster size. This is
   where flooding-vs-routing-vs-hierarchy crossover lines get drawn.
3. **Tier 3 — high-fidelity PHY** (ns-3 or OMNeT++/INET, FLoRa for LoRa) only if
   Tier 2 leaves PHY-level questions (capture, interference) open.
4. **Tier 4 — hardware-in-the-loop field trials** (Phase 9): convoy/hike with
   real nodes; validate or refute the sims.

Keep each tier only as long as it's answering a question the next tier can't
cheaply answer. Start with Tier 1 — it already shows the headline result.

## Open mobility/topology questions

- Election hysteresis constants that prevent thrash without making the Head
  stale — measured vs RPGM speed.
- The link-lifetime threshold where DV-lite routing actually beats flooding.
- Real ESP-NOW range/PDR under motion (the ~150 m / ~15 s figures are estimates).
- Optimal cluster-size target: too small → many Heads → more LRP airtime; too
  big → ESP-NOW window congestion. There's a sweet spot to find.
- When to fission/fuse clusters to keep both planes efficient.
