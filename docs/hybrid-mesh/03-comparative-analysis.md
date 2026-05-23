# 03 — Comparative Analysis

What exists, what we borrow, and where the Waymesh niche actually is. The
purpose is to avoid reinventing wheels and to be honest about where existing
systems already win.

## At a glance

| System | PHY / band | Topology | Range | Rate | Power model | Mobility | Sweet spot |
|--------|-----------|----------|-------|------|-------------|----------|-----------|
| **Meshtastic** | LoRa sub-GHz | managed flood mesh | km | ~10s–100s B/s | airtime-bound, RX cheap | poor (slow, no roaming) | static hikers/communities, text+GPS |
| **ExpressLRS** | LoRa/FLRC 2.4 & 900 | star, P2P | long for a control link | high packet rate, tiny payload | TX-heavy, mains/large batt | excellent (built for flight) | RC control uplink, low latency |
| **ESP-NOW** | WiFi 2.4 PHY | star / flat (no routing) | ~100–200 m (≤900 m LR mode) | up to 1 Mbps | RX expensive (~95 mA) | good locally | fast local clusters |
| **BLE Mesh** | BLE 2.4 | managed flood | short (~10s m) | low | relay nodes must stay on | moderate | dense building automation |
| **Zigbee/Thread** | 802.15.4 2.4 | mesh (routing) | short–med | low-med | routers mains-powered | weak | home/building IoT |
| **Classic MANET** (AODV/OLSR/Babel) | assumes IP links | proactive/reactive routing | link-dependent | link-dependent | assumes capable nodes | designed for it, but heavy | richer radios than LoRa |
| **DTN / Bundle** | overlay (any) | store-carry-forward | unbounded in time | n/a | tolerant | excellent for partitions | sparse, intermittent |
| **Swarm telemetry** (custom) | varies | flat broadcast | short–med | high | varies | excellent | tight local coordination |
| **Waymesh (this)** | 2.4-LoRa + ESP-NOW (one band, time-shared) | clusters + bridged flood + DTN | 100s m–~km bridge over local clusters | hybrid | RX-asymmetric (LoRa cheap, WiFi burst) | designed for it | **mobile mix of dense + sparse** |

## System-by-system

### Meshtastic — the closest cousin
- **Model:** everything rides one LoRa channel; "managed flood" with hop limit,
  CSMA-ish, SNR-based rebroadcast delay, and duplicate suppression.
- **Strengths:** dead-simple, no infrastructure, genuinely long range, robust.
- **Limits we exploit:** *all* traffic competes for one slow channel → airtime
  is the hard ceiling; density makes it worse, not better; mobility is weak
  (no fast local plane, position updates are costly); battery life fights with
  responsiveness because long RX windows or low beacon rates are the only knobs.
- **What we borrow:** managed-flood discipline (hop limits, SNR-delayed
  rebroadcast, dedup) — but only on the *long-range* plane, and only for
  *aggregated* traffic. We do **not** put local chatter on LoRa.

### ExpressLRS — the RF craftsmanship reference
- **Model:** tightly scheduled point-to-point control link; FHSS, telemetry
  ratios, dynamic power, very low latency. Star, not mesh.
- **What we borrow:** disciplined airtime scheduling, FHSS robustness ideas,
  dynamic TX power, and the engineering culture of measuring RF reality. We also
  borrow the *hardware*: the XR2 target **is** an ELRS receiver (ESP32-C3 +
  LR1121), and ELRS's open-source LR1121 driver / pin map is our Phase-0 radio
  reference. ELRS's "WiFi mode and RF link never run at once" is our coexistence
  precedent.
- **Not applicable:** no mesh, no store-and-forward, single-link focus.

### ESP-NOW — our local plane
- **Model:** connectionless 2.4 GHz frames, 250 B (v1) / up to 1470 B (v2),
  ~1 Mbps (LR mode 256/512 kbps for ≥900 m open), no association, low latency.
- **Strengths:** fast, simple, great for dense local sync and aggregation.
- **Limits:** no long range, no routing/mesh, RX is power-expensive, all in the
  congested 2.4 GHz band.
- **Role here:** the burst plane for intra-cluster traffic; never the long haul.

### BLE Mesh / Zigbee / Thread
- Mature 2.4 GHz mesh stacks, but **assume relay nodes stay powered** and target
  short range / static topologies. Mobility and battery-relay are weak. We take
  the *managed-flood* and *friendship/low-power-node* ideas (a sleepy node leans
  on an always-on neighbor) but reject the always-on-router assumption.

### Classic MANET routing (AODV, OLSR, Babel, DSR)
- Proactive link-state (OLSR/Babel) keeps routing tables fresh — **too much
  control traffic for a slow LoRa channel** under churn. Reactive (AODV/DSR)
  floods route discovery — also airtime-hostile at scale.
- **Lesson:** on the long-range plane, *don't maintain routes*. Use managed
  flooding + hierarchy + DTN. Routing tables only ever make sense on the cheap,
  fast local plane, and even there churn usually makes flooding simpler.

### Delay-Tolerant Networking (DTN / Bundle Protocol)
- **Model:** store-carry-forward, custody, summary vectors, epidemic/spray-and-
  wait routing; built for partitioned, intermittent networks.
- **What we borrow:** a *bundle-lite* overlay for cross-partition delivery — a
  node holds messages and forwards opportunistically when a new neighbor or a
  bridge appears. This is how a "mule" (a moving node) carries data between
  islands. We keep it minimal (no custody transfer, summary-vector dedup only).

### Swarm / tactical telemetry systems
- Often flat high-rate local broadcast with tight time sync. Excellent locally,
  no long-range story. We adopt the *time-synchronized wake window* idea to make
  ESP-NOW bursts cheap, and add the long-range bridge they lack.

## Where Waymesh is genuinely different

1. **Asymmetric dual-plane by power, not just by range.** Others pick a band;
   we exploit that LoRa RX is ~16× cheaper than WiFi RX to make LoRa the
   always-listening plane and ESP-NOW a scheduled burst — a power architecture,
   not only a topology.
2. **Aggregate-then-bridge.** Local clusters collapse N updates into one
   long-range digest. Meshtastic can't (no local plane); ESP-NOW can't (no
   bridge). This is the airtime win.
3. **Built for the dense+sparse *mix* under mobility.** Hikers bunched up then
   spread out; a convoy that fragments; a swarm with roaming scouts. The system
   shifts traffic between planes as topology changes.
4. **Honest about when mesh is wrong.** We will publish the regimes where plain
   flooding or plain ESP-NOW is better and *not* use our hierarchy there.

## Where existing systems still win (be honest)

- **Pure long-range, sparse, static, simple:** Meshtastic is simpler and good
  enough. Our complexity only pays off with local density or mobility.
- **Single low-latency control link:** ExpressLRS is purpose-built; don't compete.
- **Dense static automation indoors:** BLE Mesh/Thread are mature.
- If a target scenario is *only* one of those, the finding is "use the existing
  thing." Our value is specifically the **mixed, mobile, power-constrained**
  middle.

## Sources

- [Semtech LR1121 product page](https://www.semtech.com/products/wireless-rf/lora-connect/lr1121)
- [LR1121 datasheet (rev 2.0, PDF)](https://files.waveshare.com/wiki/Core1121/LR1121_H2_DS_v2_0.pdf)
- [ESP-NOW (ESP-IDF guide)](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/network/esp_now.html)
- [ESP32-C3 datasheet](https://documentation.espressif.com/esp32-c3_datasheet_en.html)
