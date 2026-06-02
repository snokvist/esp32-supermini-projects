# 13 — Authentication & Group Identity (design note + implementation spec)

Status: **design proposal, no firmware yet.** This doc decides *how* Waymesh gets
the two things [01](01-vision-and-requirements.md) reserves as "basic auth/crypto
hooks" — **group identity/filtering** and **authentication** — and then gives a
concrete, actionable spec to implement on. It resolves the `GroupID` placeholder
in [05 §Identifiers](05-protocol.md) and the channel/PSK handling in
[11 §Channels & crypto](11-mobile-gateway-meshtastic-compat.md).

The driving constraint: we already reuse the **Meshtastic client** as our UI
([11](11-mobile-gateway-meshtastic-compat.md), goal 6). Any group/auth scheme we
pick should *exploit* that, not fight it.

## 1 — The fork

There are two ways to get group identity + authentication. They pull in opposite
directions on complexity and on Meshtastic-client reuse.

### Option A — roll our own (`GroupID:16` + custom MIC)

A 16-bit `GroupID` tag in the header (as [05](05-protocol.md) currently sketches)
plus a bespoke MIC for auth.

- ➖ The Meshtastic app **cannot see or manage** our groups or keys. Group
  identity is invisible to the very client we reuse — we'd have to build key/group
  provisioning ourselves (QR/URL/UI), which is exactly the custom UI we're
  avoiding.
- ➖ A custom MIC is a new crypto surface to design, test, and get right.
- ➕ Smallest possible wire bytes; no dependency on Meshtastic's crypto details.

### Option B — reuse Meshtastic's channel + PSK scheme (recommended)

Meshtastic **already has both primitives we want**, and the Android/iOS/CLI apps
already implement the entire management UX for them:

- A **channel** (name → 1-byte hash) **is** group identity.
- The channel **PSK** gives **membership gating + confidentiality** (AES-CTR).
- The app already does channel create / QR / shareable URL / key rotation.

We adopt the *scheme* (channel-hash for filtering, PSK-based encryption) on our
**own** compact LoRa beacon — we do **not** put Meshtastic `MeshPacket`s on the
air (OTA Meshtastic compat stays an explicit non-goal,
[11 §Scope](11-mobile-gateway-meshtastic-compat.md)). We reuse the algorithm and
the app's key UI, not the PHY.

- ➕ Group identity + crypto come "for free" from a UX standpoint — the phone
  manages the keys; we just honor the channel set we already advertise in the
  handshake ([11](11-mobile-gateway-meshtastic-compat.md)).
- ➕ Directly serves goal 6 instead of competing with it.
- ➖ We inherit Meshtastic's crypto model and its limits (see §2).
- ➖ Small AES cost on the ESP8285 tier (software AES; see §6).

**Decision: Option B.** The rest of this doc specifies it.

## 2 — Be honest about what "authentication" means here

Meshtastic's channel model is a **shared symmetric key per channel**. Adopting it
means inheriting exactly this security posture:

| Property | Channel PSK gives us? | Notes |
|----------|----------------------|-------|
| **Group identity / filtering** | ✅ yes | channel hash byte; foreign groups are dropped |
| **Confidentiality** | ✅ yes | AES-CCM (AEAD) with the PSK; only members read positions/text |
| **Group membership "auth"** | ✅ yes (implicit) | only a key-holder can produce a packet that decrypts sanely |
| **Tamper / integrity (vs outsiders)** | ✅ on MIC'd beacons | 4-byte AEAD tag over header+payload (§5), default-on for position/text — detects bit-flips/forgery by non-members (beyond stock Meshtastic). *Clear presence beacons are unauthenticated → forgeable; opt a MIC onto them if that matters (§5).* |
| **Per-node authentication** | ❌ **no** | any group *member* (key-holder) can still forge another node's `srcNodeID` — the MIC is a group key, not a per-node signature |
| **Replay protection** | ⚠️ partial | the dedup seen-set drops exact replays within its window only |

This is the same posture as stock Meshtastic channels: **possessing the key = being
in the group**, and there is **no per-sender signature** in the channel layer.
That is almost certainly the right "simpler than Meshtastic" target — but state it
in the threat model so nobody assumes spoofing protection we don't have.

If per-node authenticity ever becomes a requirement, it's an **additive** track
(a per-node signature / asymmetric identity over the same frame) — out of scope
here, noted in §9.

## 3 — Wire format: `v2` beacon

Extends the as-built `v1` beacon ([05 §As-built beacon](05-protocol.md)). Stays a
compact fixed header — **not** the full LRP header (that migration is separate).

```
v2 header (12 B):
 byte 0     : magic     = 0x57 ('W')        gates RX (unchanged)
 byte 1     : version   = 2
 byte 2     : chanHash  (u8)                 group identity / filter (Meshtastic channel hash)
 byte 3     : flags     (u8)                 bit0 POS valid, bit1 ENCRYPTED, bit2 HAS-MIC, bits3-7 rsvd=0
 byte 4-7   : srcNodeID (u32, LE)            originator (relay identity + nonce material)
 byte 8-11  : packetId  (u32, LE)            dedup MessageID + AEAD nonce material

payload (POS, 10 B when present), AEAD-encrypted (AES-CCM) when flags.ENCRYPTED:
 byte 0-3   : lat_i (i32, 1e-7 deg)
 byte 4-7   : lon_i (i32, 1e-7 deg)
 byte 8     : sats (u8)
 byte 9     : payload-flags (u8, rsvd=0)

MIC tail (4 B, present when flags.HAS-MIC):
 byte 0-3   : AEAD tag (AES-CCM) over header[1..] as AAD + payload (§5)
```

Key changes from `v1`:

1. **`chanHash` (new):** the Meshtastic 1-byte channel hash (§4). This is the
   `GroupID` from [05](05-protocol.md), realized as the Meshtastic channel hash so
   the app's channels *are* our groups.
2. **`packetId:32` replaces `seq:16`.** The dedup `MessageID` becomes
   `(srcNodeID:32, packetId:32)`. A 32-bit id is needed as an AEAD nonce that won't
   wrap over a node's lifetime (a 16-bit `seq` wraps in ~minutes-to-hours at beacon
   cadence → nonce reuse → keystream reuse, a real break). Generate `packetId`
   as a **reboot-safe** monotonic counter (NVS reserve-ahead, §8) so it never
   repeats under one key, even across crashes.
3. **`flags.ENCRYPTED`:** when set, the payload is AEAD (AES-CCM) ciphertext (§5).
   The header (magic..packetId) is **always plaintext** — relays and the dedup path
   read it without the key (§6).
4. **`flags.HAS-MIC` + 4-byte MIC tail (default-on for encrypted beacons):** the
   AEAD tag authenticating the header + payload (§5). Detects tampering/forgery by
   non-members; **stripped at the gateway**, so it costs nothing on the
   Meshtastic-app side (§7).

**Byte budget:** v2 POS beacon = 12 (header) + 10 (payload) + 4 (MIC) = **26 B**
(vs v1's 18 B). At SF9/BW812 that's still well under ~20 ms ToA
([05 §airtime](05-protocol.md)) — the +8 B over v1 is negligible. (A clear,
MIC-less presence beacon is just the 12-B header.)

**Migration:** `version` gates parsing exactly as v1 did. A v2 receiver still
parses v0/v1 (no `chanHash`, treat as the "open"/default group); a v1-only
receiver ignores v2 (length/version guard). Plan a flag-day or a transition window
per deployment; document the chosen `version` floor.

## 4 — Channel hash (group identity / filter)

Reuse Meshtastic's channel-hash so a channel created in the app maps 1:1 to a
Waymesh group. Compute it the Meshtastic way (**verify against the current
`Channels::generateHash` / `CryptoEngine` before coding** — these evolve):

```
chanHash = xorHash(channelName_bytes) XOR xorHash(expandedPSK_bytes)
where xorHash(buf) = buf[0] ^ buf[1] ^ ... ^ buf[n-1]   (single byte)
```

- The PSK is the **expanded** key: a 1-byte PSK in `1..N` selects a well-known
  default key (the famous `psk={0x01}` → default key
  `0xd4f1bb3a20290759f0bcffabcf4e6901`); a 16/32-byte PSK is used directly. Match
  Meshtastic's expansion so hashes agree with the app.
- `chanHash` is **not** unique per channel (1 byte, collisions possible) — it is a
  cheap *filter*, exactly as in Meshtastic. Final authority is whether the payload
  **decrypts** under a configured channel's key (§5). Collisions just cost a failed
  decrypt attempt.
- **Always provision explicit, non-empty channel names.** Meshtastic hashes the
  primary channel's *effective* name, and an empty name is substituted with a
  **preset-derived name** (`LongFast`, …), coupling `chanHash` to the LoRa preset we
  advertise. A non-empty name makes `chanHash` depend only on the `(name, psk)`
  bytes we control — removing that coupling and the main source of hash mismatches
  with the app (Q1).

A node is provisioned with **one or more channels** (name + PSK). Its set of
accepted `chanHash`es is derived from those. The "open" default group is the
Meshtastic default channel (`psk={0x01}`), which keeps today's free-for-all as a
selectable mode, not the only mode.

## 5 — Encryption / MIC

**What must match Meshtastic is narrow.** The app never sees our OTA ciphertext —
the gateway always decrypts an inbound beacon and hands the app a *plaintext*
`MeshPacket.decoded` (just like a real radio,
[11 §Channels & crypto](11-mobile-gateway-meshtastic-compat.md)). So the only crypto
that must be byte-compatible with Meshtastic is **(a)** the channel hash and
**(b)** the PSK/default-key expansion (both app-facing, §4), plus **(c)** on the TX
path only, decrypting the phone's Meshtastic-CTR-encrypted `MeshPacket` (§7.3). **The
cipher protecting our own OTA payload is ours to choose** — it never reaches the app
— so pick the simplest sound AEAD rather than re-deriving Meshtastic's
*unauthenticated* CTR.

- **Cipher: AES-CCM (AEAD), key = the channel's expanded PSK** (AES-128/-256 by key
  length). One primitive gives confidentiality **and** integrity: encrypt the
  payload, authenticate `header[1..]` (version through `packetId`) as AAD, and emit a
  **4-byte tag** = the MIC tail (§3), flagged by `flags.HAS-MIC`. The C3 has an AES
  peripheral; software CCM on the 8285 originator is cheap at ~10 B / beacon cadence.
  *(Acceptable alternative if a CCM impl is inconvenient: AES-CTR + encrypt-then-MAC
  with a 4-byte truncated AES-CMAC under a separate PSK-derived subkey — same wire
  layout, two passes instead of one.)*
- **Nonce (Q4):** derive a unique-per-(key, message) nonce from `packetId` +
  `srcNodeID`. Uniqueness rests on the **reboot-safe 32-bit `packetId`** (§3, §8)
  never repeating under one key. This layout is **ours** (not Meshtastic's — this
  payload never goes to the app); just keep it deterministic so any key-holder
  reconstructs it from the clear header. Pad `(packetId‖srcNodeID)` to the CCM nonce
  length (e.g. 13 B) with a fixed prefix / `chanHash` so both ends agree.
- **MIC = the AEAD tag, default-on for encrypted beacons.** Stock Meshtastic channel
  packets carry *no* integrity tag, leaving them malleable: an outsider with **no
  key** can bit-flip ciphertext to tweak a known plaintext field (e.g. nudge a
  latitude bit) undetected. Because this radio broadcasts physical **location**,
  closing that is worth 4 bytes. The receiver/gateway verifies the tag *before*
  trusting the plaintext; a bad tag → DROP.
  - **No app-compat cost.** The tag lives only on our OTA frame; the gateway verifies
    and **strips** it before mapping to the app (§7) — so adding the integrity
    Meshtastic lacks costs nothing on the client side.
  - **It is a *group* MAC, not a per-node signature** — it stops outsider
    tampering/forgery, not a key-holding insider spoofing another `srcNodeID` (§2).
    Per-node signatures stay deferred (§9).

**Presence vs position (Q3).** Default policy: the **header is always clear** (it
must be — `chanHash`/`srcNodeID`/`packetId` drive keyless relay + dedup), and only
the **position payload** is encrypted+MIC'd. The secret is *where you are*, not
*that you exist* — and bare liveness already leaks via the clear header, so
encrypting an empty presence payload buys almost nothing. A header-only presence
beacon therefore ships clear and MIC-less (12 B).

`ENCRYPTED` implies `HAS-MIC` (an AEAD always tags). The standalone combo
`HAS-MIC && !ENCRYPTED` is **authenticated-but-clear** — opt into it (+4 B) to make
presence *unforgeable* where that matters. By default presence is MIC-less and so
**forgeable by anyone who knows the (public) `chanHash`**; the harm is bounded (a
forged node ID never yields a valid encrypted position) but it can pollute the node
DB, so enable the presence MIC if node-DB spoofing is a concern.

For deployments that need traffic-analysis resistance, offer a per-channel
**stealth** policy: replace `chanHash`/`srcNodeID` with rotating pseudonyms (e.g.
truncated `HMAC(psk, epoch)`) so outsiders can't track a node or confirm group
membership over time. **State the trade plainly: stealth breaks keyless relay** —
relays then need the key to dedup/filter, forcing `relay-known` only. You cannot
have both cheap keyless relay *and* header privacy; it is a conscious per-channel
choice, not the default.

## 6 — RX acceptance & relay (the algorithm changes)

Two code sites change today; both keep the **header in plaintext** so the cheap
ESP8285 relay never needs a key.

### Acceptance filter — Tier-1/2 (`waymesh-node` `main.cpp:165`, `waymesh-8285` `main.cpp:276`)

Current guard is `CRC ok && len>=8 && magic==0x57 && srcId!=self`. Add, in order:

```
1. magic == 0x57, version supported, len >= v2 header           (as today, + v2 len)
2. srcNodeID != self                                            (as today)
3. chanHash ∈ myAcceptedHashes        --- ELSE DROP (foreign group)   <-- NEW
4. dedup: (srcNodeID, packetId) not in seen-set                 (as today, wider key)
5. if HAS-MIC/ENCRYPTED: AEAD-open under the matching channel key
      (verify tag, decrypt payload if ENCRYPTED); failure -> DROP
      (tamper / foreign / wrong key)                            <-- NEW
6. accept: log, upsert peer, feed bleGattOnPeer(...)            (as today)
```

Step 3 is the **group filter** ("share spectrum without merging",
[04 §GroupID](04-architecture.md)). Step 5 is the integrity + implicit membership
check — one **AEAD-open** (with AES-CCM, verify and decrypt are a single atomic op;
the encrypt-then-MAC alternative verifies the tag first, then decrypts).

### Managed-flood relay — Tier-3 dumb relay (`waymesh-8285` `main.cpp:265-310`, `-DWAYMESH_RELAY=1`)

The relay re-floods **verbatim** and must keep working **without the key**
(crypto is end-to-end originator→gateway, exactly like Meshtastic relays):

- Dedup on the wider `(srcNodeID, packetId)` MessageID (seen-set, §3).
- **Group-scoped relay policy** (new knob):
  - `relay-known`: relay only `chanHash`es in the relay's configured set
    (a relay dedicated to a group).
  - `relay-all` (default for a public infrastructure relay): relay every
    `chanHash` verbatim — it can't read payloads anyway, so it carries foreign
    groups blindly. This preserves today's behavior and is the right default for a
    "dumb relay."
- **No decryption, re-encryption, or MIC check** at the relay — `raw[]` (header +
  ciphertext + MIC tail) is re-sent byte-for-byte (`relaySchedule`/`relayService`
  unchanged), so `srcNodeID`/`packetId`/ciphertext/MIC are preserved and the gateway
  still attributes presence to the originator and verifies the MIC end-to-end.

**ESP8285 crypto note:** only the **originator** (a Tier-2 8285 that beacons its own
position) needs AES, and only on its own ~10-byte payload at beacon cadence —
software AES is fine. A **Tier-3 relay needs no crypto at all.** The XR2 (C3) has
an AES peripheral. **Library:** the C3 has `mbedtls_ccm`; the ESP8266/8285 Arduino
core ships BearSSL (`br_ccm`) — both have CCM, but if a single dependency-light path
across both silicons is preferred, the §5 AES-CTR + CMAC alternative is trivial to
implement portably.

## 7 — Gateway / Meshtastic-client integration (`ble_gatt.cpp`)

The gateway is the crypto boundary. Changes layer onto the existing handshake
([11](11-mobile-gateway-meshtastic-compat.md), `queueConfigSequence`):

1. **Advertise the channel set.** Emit `FromRadio{channel: Channel}` frames for
   each provisioned channel (name + PSK + index) during the handshake — the app
   adopts them ([11 §Channels & crypto](11-mobile-gateway-meshtastic-compat.md)).
   Today only the implicit default is assumed; make the set explicit. The reverse
   direction — the app *editing* a channel — is the runtime-update path (§8.1).
2. **Read path (OTA → app):** when a v2 beacon is accepted+decrypted (§6), map to
   `NodeInfo`/`Position`/text **tagged with the matching channel index** on the
   `MeshPacket` so the app shows it on the right channel. (`buildNodeInfoFrame` /
   `buildPositionPacketFrame` already produce plaintext to the app — add the
   channel index.)
3. **Write path (app → OTA):** the stubbed `ToRadio{packet}` TX path
   (`ble_gatt.cpp:336-338`) becomes: the phone sends `MeshPacket.encrypted` on a
   channel → gateway decrypts with that channel's PSK (it advertised it) → recovers
   text → **re-encrypts and MACs into a v2 TEXT beacon** under the same channel's
   key+hash → floods onto LoRa. (Implements [09 Phase G](09-poc-roadmap.md)
   increment 2 with real channel crypto instead of "any PSK".)
4. **Bonding:** pair the OTA channel-PSK story with BLE bonding/PIN
   ([11 §BLE GATT](11-mobile-gateway-meshtastic-compat.md), currently open/no-bond,
   `ble_gatt.cpp:368`) so the *link* to the phone is also protected, not just the
   OTA payload.

## 8 — Provisioning

- **Per-node config:** a channel list `[(name, psk, index)]` (explicit non-empty
  names, §4), plus the relay policy (`relay-known` | `relay-all`). Source it from a
  build-time config / NVS — no cloud, no account
  ([01 non-goals](01-vision-and-requirements.md)).
- **Channels per node (Q2):** beacon presence/GPS on **one home channel** (a node
  belongs to one group) — multi-TX-channel is N× airtime on a shared band.
  *Accepting* and *relaying* multiple channels is cheap (header filter), and the
  **gateway** may bridge up to Meshtastic's max (**8**) channels to the app. So
  multi-channel lives on RX/gateway; the air stays single-home.
- **`packetId` counter (Q4):** a reboot-safe monotonic counter — persist a
  high-water *ceiling* to NVS **before** using a block of IDs (reserve ~1024 at a
  time); resume at the ceiling on boot. A crash mid-block wastes ≤1 block but
  **never reuses** a nonce. Wear ≈ one NVS write per 1024 beacons (~hours of
  runtime). On fresh/corrupt NVS, seed the base from the hardware RNG (`esp_random`
  on the C3; RNG register on the 8285), then reserve-ahead.
- **Sharing:** since channels are Meshtastic channels, a group is shared with the
  **standard Meshtastic channel QR/URL** out of the app — zero custom tooling.
- **Default:** ship the Meshtastic default channel (`psk={0x01}`) so an
  unprovisioned node still interoperates on the open group (today's behavior),
  selectable rather than mandatory.

### 8.1 — Runtime channel update over the Meshtastic app (no reflash)

The reuse payoff: a BLE-gateway (C3) node's channel set is editable from the stock
app's **Channel editor** — no custom tool, no reflash. Flow (verify message/field
numbers against the pinned `admin.proto` / `channel.proto`):

1. App → node `ToRadio{packet}` with `packet.decoded.portnum = ADMIN_APP (6)` and a
   serialized `AdminMessage` payload. Admin rides **inside the existing
   `ToRadio{packet}` path** — no new transport variant; the stub at
   `ble_gatt.cpp:336` is exactly where it lands.
2. `AdminMessage.set_channel = Channel{ index, role, settings{ name, psk, … } }` →
   the gateway recomputes `chanHash` + the AES key (§4), validates, and **persists**
   the channel to NVS.
3. Node re-advertises: bump `FromNum`, emit the updated `FromRadio{channel}`; new
   beacons use the new hash/key immediately.
4. Read-back: `AdminMessage.get_channel_request` → node replies
   `get_channel_response` (or relies on the handshake's `FromRadio{channel}` dump) so
   the editor shows current state.
5. **Session passkey:** recent apps require an admin session — the node returns a
   `session_passkey` nonce (in a `get_*` response) that the app echoes on mutating
   admin writes; it expires. Implement it, or stub-accept for a locally-bonded BLE
   client, per the pinned version.

Share the resulting channel to other **C3** nodes with the standard Meshtastic
channel **QR / URL** out of the app.

### 8.2 — What's needed to close the gap (investigated against the current tree)

None of this exists yet. Concretely, to ship 8.1:

- **Proto (absent):** the vendored trimmed proto
  (`projects/waymesh-node/proto/waymesh_mesh.proto`) has **no** `Channel`,
  `ChannelSettings`, or `AdminMessage`, and `FromRadio` has **no** `channel`
  variant — the gateway can't even represent a channel today. Add
  `ChannelSettings{psk, name, id}`, `Channel{index, settings, role}`, a **trimmed**
  `AdminMessage` (just `set_channel` / `get_channel_request` / `get_channel_response`
  / `session_passkey`), and `FromRadio.channel = 10` (`MeshPacket.channel` already
  exists, field 3). Regenerate nanopb per `proto/README.md`, field numbers in
  lockstep with upstream.
- **NVS/config store (absent):** there is **no** `Preferences`/NVS/EEPROM usage in
  the firmware today. Add a small persistence module holding the channel list **and**
  the §8 `packetId` high-water — one store, written on channel-set and on the
  periodic reserve-ahead. (C3: `Preferences`/NVS; the 8285 tier uses its
  EEPROM/flash equivalent.)
- **Crypto prerequisite:** `set_channel` can't be honored until `chanHash` + the
  PSK/default-key expansion (§4) exist — so the runtime-update path **depends on the
  §9 Phase-1/2 crypto landing first**; it is not independently shippable.
- **Gateway handler:** extend the `ToRadio{packet}` stub (`ble_gatt.cpp:336`) to
  decode `ADMIN_APP` → `AdminMessage`, and emit `FromRadio{channel}` in
  `queueConfigSequence` (the handshake emits my_info/metadata/nodeinfo/complete
  only — no channel today).

### 8.3 — Tier constraints (who can be configured how)

- **C3 / BLE-gateway nodes (XR2):** full 8.1 — set/update channels live from the app.
- **ESP8285 tiers (Bayck / BetaFPV):** the ESP8285 is **WiFi-only, no BLE**, so they
  **cannot** be configured from the Meshtastic app — instead they are provisioned at
  flash/NVS time **or live via an ELRS-style WiFi config portal (§8.4)**. A
  `relay-all` relay needs no channel/key anyway (it re-floods verbatim, §6), so most
  dumb relays need zero channel config; a `relay-known` 8285 takes its allow-list the
  same way.
- **OTA key distribution to headless nodes** (a signed "channel-announce" beacon) is
  a *possible later* convenience but carries a bootstrap-trust problem (securely
  shipping a new key needs an existing shared key) — out of scope here.

### 8.4 — ESP8285 provisioning: ELRS-style WiFi config portal

The 8285's WiFi (its only client-facing radio) is the lever: stand up a **SoftAP +
captive web UI** — the same pattern ExpressLRS already ships on this exact hardware
class — so we crib its proven flow instead of inventing one. This gives the 8285
tier feature-parity with the C3's app-based channel-set (§8.1): **both end at the
same NVS config store (§8.2)**, just reached over WiFi instead of BLE.

- **Entry into config mode:** **long-press the board button ~5 s** → AP mode. These
  boards have a button — typically **GPIO0**, the boot/download strap (held low at
  reset = flash mode) which is free to read as a normal input *after* boot. Add its
  GPIO + a debounced long-press to `board_config.h`/`main.cpp` (neither exists yet).
  *(ELRS's button-less triggers — auto-AP on boot-timeout, power-cycle-N — stay
  available as alternatives, but aren't needed since the button is present.)*
- **Portal (reuse ELRS's):** SoftAP SSID `Waymesh_XXXX` (matches the C3 BLE name),
  optional WPA2 password, UI at **`http://10.0.0.1`**, `DNSServer` captive redirect,
  `ESP8266WebServer`/`ESPAsyncWebServer` + a tiny static page (all bundled with the
  `espressif8266` Arduino stack; LittleFS for assets). Verify the exact SSID/IP/auth
  defaults against the ELRS rig you want to mirror.
- **What it sets:** the home channel (name + PSK), the accepted-channel set + relay
  policy (`relay-all` | `relay-known` allow-list), and node name. On save, recompute
  `chanHash`/key (§4) and write the **same NVS/EEPROM store as §8.2** (ESP8266
  `Preferences`/EEPROM).
- **RF coexistence:** the ESP WiFi radio and the SX1280 are both 2.4 GHz, so
  **suspend LoRa relay while the AP is up** (you're provisioning on the bench, not
  relaying); drop the AP on save/timeout and resume. Same two-radio self-desense
  caution as the C3's BLE↔LoRa slot ([09 P7](09-poc-roadmap.md)).
- **Trust:** typing a PSK into the local AP is a physical-proximity step (same model
  as ELRS WiFi config). Prefer a **WPA2-protected AP** so a passer-by can't join; the
  captive portal is the only writer of the config store.

This is the **one** place we add a *small* custom UI (a single config page),
justified because the Meshtastic app cannot reach a BLE-less node — everything the
app *can* reach still reuses it. Lands with the Tier-2/3 `relay-known` / 8285
home-channel work (§9 / [09 Phase H](09-poc-roadmap.md)).

## 9 — Phasing (slots into [09](09-poc-roadmap.md))

Build on top of Phase G / Phase 5, incrementally:

1. **Group filter only (no crypto).** Add `chanHash` to the v2 header; implement
   acceptance step 3 + relay policy. Positions still in clear. *Proves* spectrum
   sharing / group separation cheaply. **Gate:** two groups on one band ignore each
   other; a `relay-all` relay still carries both.
2. **Channel encryption + MIC.** Add AES-CCM (AEAD) — payload encryption + the
   default-on 4-byte tag (§5) — over the reboot-safe `packetId` nonce (§8); gateway
   opens (verifies + decrypts) and strips the tag on read.
   **Gate:** only key-holders see positions/text; tampered/foreign frames are
   dropped on the MIC; the app shows them on the right channel; relay still works
   without the key.
3. **TX + runtime channel-set ([09](09-poc-roadmap.md) Phase G inc. 2–3).** Wire the
   gateway write path (§7.3) for text, *and* the `AdminMessage.set_channel` →
   NVS-persist → re-advertise flow (§8.1) — both ride the same `ToRadio{packet}`
   handler. **Gate:** phone text round-trips on a chosen channel over multi-hop
   LoRa, and a channel edited in the app persists across reboot and takes effect
   without a reflash.
4. **(Deferred) per-node signatures.** *Only* if a real threat needs non-repudiation
   between group members — an asymmetric per-node signature (§2). Expensive (a 64-B
   Ed25519 sig triples beacon airtime) and needs a key-trust story we don't want, so
   confine it to low-rate, high-value messages (a signed command / role-change),
   never per-position beacons — mirroring how Meshtastic limits PKC to DMs/admin.
   Not built until demanded.

## 10 — Test / verification plan

- **Vectors (Q1):** the app-facing bits — `chanHash` + PSK/default-key expansion —
  must match **upstream**: assert them against vectors **generated from the
  `meshtastic` CLI at the pinned version** (a channel URL → hash), checked into
  `test/` and run in CI (generate-*with*, don't copy-*from* — upstream is GPL-3.0).
  Our OTA **AEAD** is internal, so test it against our own known-answer vectors
  (encrypt→decrypt round-trip + tamper-detect). This is the single highest-value
  check — get it green before any RF.
- **Group filter (bench):** two XR2s on channel A, one on channel B → A-nodes drop
  B's beacons (step 3); a `relay-all` 8285 still re-floods both (`relay==rx`);
  a `relay-known` 8285 on A drops B.
- **Crypto round-trip:** XR2(A) encrypts → 8285 relay (no key) re-floods verbatim →
  XR2(B, has key) decrypts → renders in the stock Meshtastic app on channel A.
  Reuse the verified Tier-2→Tier-1→app chain ([09 Phase H](09-poc-roadmap.md)).
- **Nonce-uniqueness soak (Q4):** run a node past where a 16-bit seq would have
  wrapped, **and across forced reboots/crashes**, confirming `packetId` never
  repeats under one key (no nonce reuse) — exercises the NVS reserve-ahead (§8).
- **Negative:** a wrong-key node fails the MIC/decrypt and drops with no plaintext
  leak; a bit-flipped ciphertext → **MIC verification fails → DROP** (the integrity
  guarantee from §5), not silent acceptance.

### Status (as built — 2026-06-01, `waymesh-node` only)

Steps §9.1–§9.3 are implemented in `waymesh-node` (the XR2 / ESP32-C3 Tier-1
gateway). Verification so far:

- **Host-verified (CI, `pio test -e native`):** channel hash + PSK/default-key
  expansion vs upstream-generated vectors; AES-CCM AEAD (RFC 3610 / FIPS-197);
  the Meshtastic AES-CTR byte-compat KAT (§7.3, vs the pinned `CryptoEngine`);
  the reboot-safe `packetId` reserve-ahead soak (§8); the v2 beacon codec
  (POS + TEXT, round-trip + tamper-detect); and the `Channel` / `AdminMessage`
  proto wire layout (decoded from hand-built upstream-format buffers).
- **Single-device on-air (XR2 + host BLE, `tools/ble_l2_test.py`):** the
  `want_config` handshake + channel advertise decode cleanly in the **stock
  Meshtastic protobuf stack** (`LongFast`/`psk=01` → upstream `Channel{PRIMARY}`);
  v2 encrypted POS beacons + monotonic reboot-safe `packetId` on-air; and the
  **app→OTA text write path end-to-end** — a CTR-encrypted channel text written
  over BLE is decrypted byte-compatibly and re-flooded as a v2 TEXT beacon.

- **Deferred — needs a 2-device bench (the v2/auth code must first be ported
  from `waymesh-node` to the `waymesh-8285` Tier-2/3 relay/originator firmware):**
  - **Group filter on-air (§9.1 gate):** two groups on one band ignore each
    other; a `relay-all` relay carries both; a `relay-known` relay drops foreign.
  - **Crypto round-trip (§9.2 gate):** originator encrypts → a **keyless** relay
    re-floods verbatim → a *second* key-holder decrypts + renders; a wrong-key
    node drops on the MIC.
  - **Heard text in the app chat:** a TEXT beacon from another node surfaced as a
    `MeshPacket{TEXT_MESSAGE_APP}` on the right channel (the read half of §7.3).
  - **Live app admin flow (§8.1):** whether the stock app drives `set_channel`
    via this `AdminMessage` path, the 1-based `get_channel_request` indexing, and
    the `session_passkey` handshake — byte-compat is unverified without the phone
    app driving an edit; the proto layout + apply logic are host-tested only.

  Until the 8285 port lands these stay host-tested / single-device only; see
  `roadmaps/esp32-supermini-projects.md` for the porting task.

### Status (8285 port — 2026-06-02): WiFi config portal landed (step 6, §8.4)

The 8285 port has begun with its provisioning lever. **Step 6 (§8.4 WiFi config
portal)** is implemented in `waymesh-8285` (`bayck_portal` env) and
**device-verified** on a BayckRC 7PWM (`006D2929`):

- The portable `wm_config` store now runs on the 8285 over an **EEPROM-emulation
  `wm_store_t`** backend (`src/wm_store_eeprom.cpp`), sharing the canonical
  `waymesh_config` / `waymesh_crypto` libs from `waymesh-node` via `lib_extra_dirs`
  (chain+ LDF keeps them out of the silent relay firmware — `bayck_7pwm` stays the
  byte-for-byte verified PoC build).
- An **ELRS-style SoftAP + captive web form** (`Waymesh_XXXX` @ `http://10.0.0.1`,
  WPA2) sets the home channel (name + PSK) and relay policy and writes the **same
  store the C3 fills over BLE** — the BLE-less twin of §8.1, both ending at one store.
- **Verified on-device:** fresh EEPROM seeded the `LongFast`/psk=01 default
  (chanHash 8, relay-all); a form edit to `WaymeshA` + relay-known (chanHash
  recomputed 8→31) **persisted across a power-cycle**; LoRa was suspended (radio
  asleep) while the AP was up and resumed on reboot.

### Status (8285 port — 2026-06-02): v2/auth on-air landed (Phase B, §3/§5/§6)

**Phase B is implemented** in `waymesh-8285` behind a new `-DWAYMESH_V2=1` flag and
enabled on the `bayck_portal` env (relay + portal + v2 — the full Tier-2/3
verification build). The XR2's v2/auth on-air stack (`waymesh-node/src/main.cpp`) is
now mirrored on the 8285, driven by the store the step-6 portal provisions:

- **v2 codec into the build** — `bayck_portal` pulls in `waymesh_beacon` (+ its
  `waymesh_crypto` AES/CCM) via the same `lib_extra_dirs` + chain+ LDF; the config
  store now compiles under a `WM_HAVE_CONFIG = (WAYMESH_WIFI_CONFIG || WAYMESH_V2)`
  umbrella (both the portal and v2 need it).
- **RX acceptance (§6)** — `wm_beacon_parse` → `wm_beacon_accept` (drop self /
  foreign group on the clear `chanHash`) → `wm_beacon_open` (AEAD-verify+decrypt a
  member-group frame; bad MIC / wrong key → `drop bad_mic`, no plaintext).
- **Keyless managed-flood relay (§6)** — dedup widened to the 32-bit `(srcId,
  packetId)` MessageID; `wm_beacon_should_relay` gates the verbatim re-flood on the
  **clear** header (`relay-all` carries foreign groups blindly, `relay-known` only
  configured hashes) — **no key at the relay**.
- **AES-CCM origination** — `sendBeacon` TXes a clear-header v2 beacon on the home
  `chanHash` with a reboot-safe `wm_config_next_packet_id`; a position on a keyed
  channel is sealed (`wm_beacon_build_v2_enc`), else clear.
- **v0/v1 → v2 cutover** — the non-backward-compatible step, gated behind the flag.
  `bayck_7pwm` / `betafpv_nano` stay legacy v1 (Flash 38.1% — byte-for-byte the
  verified PoC; the relay-infra MessageID widening is a compile-time no-op for v1),
  so the flip isn't silent. Production cutover (folding `V2`+`WIFI_CONFIG` into them)
  must also resolve the GPS↔portal UART0 share — deferred.

**Build-verified:** `bayck_portal` (v2) builds clean (RAM 40.5% / Flash 44.1%),
`bayck_7pwm` (v1) unchanged (Flash 38.1%, byte-for-byte), `waymesh-node` host
tests **36/36**.

**Device-verified on a multi-node bench (2026-06-02, BayckRC `006D2929`):** flashed
`bayck_portal` and observed, live, against two on-air peers — the v2 XR2 `B17506DC`
(32-bit reserve-ahead `packetId` ~150 M) and a second node `00E0029B`:

- **v2 origination** — `tx … beacon ch=31` then, after a portal re-provision to the
  default channel, `tx … beacon ch=8` (the v2 header carries the home `chanHash`;
  the `packetId` is the reboot-safe counter).
- **Group filter, both directions (§6 step 3)** — on `chanHash 31` the node logged
  `drop … foreign_group ch=8` for the ch-8 peers; re-provisioned to `chanHash 8` it
  switched to `rx … beacon ch=8` (accept) for the same peers. Two groups shared the
  band and ignored each other, then merged on a matching hash.
- **Keyless relay, both policies (§6)** — `relay-known` re-flooded **nothing** while
  hearing foreign ch-8 traffic (`relay=0`); `relay-all` re-flooded both peers
  **verbatim** keyed on the wide 32-bit `(srcId, packetId)` MessageID (`relay=20`,
  `pdr=100%`, `supp=0 qfull=0`, `badcrc=0`). All decisions made off the **clear**
  header — no key at the relay.
- **8285 ↔ XR2 interop restored under v2** — the PoC #0 gate, now group-filtered.

- **On-air AEAD round-trip (§5/§6 step 5)** — a Meshtastic app channel-text ("Test")
  sent to the XR2 was sealed as an encrypted v2 **TEXT** beacon (AES-CCM + 4-byte
  MIC) and flooded; the 8285, holding the same LongFast open key, logged
  `rx … beacon ch=8 text` — `wm_beacon_open` verified the MIC over the clear header
  and decrypted the payload (the `text` flag survived the round-trip; a wrong key
  would have logged `bad_mic`). In the **same loop** it also re-flooded the frame
  verbatim (`relay … fwd`) **without the key** — crypto end-to-end, relay key-free,
  exactly as designed.

All four §6 behaviors are now device-verified on-air. The wrong-key MIC drop is the
only item not exercised on the bench (not practically stageable without a hash
collision) — host-tested in `test_beacon`/`test_ccm`.

**Production cutover (done).** With the bench verification green, the production envs
`bayck_7pwm` / `betafpv_nano` now ship `-DWAYMESH_V2=1 -DWAYMESH_WIFI_CONFIG=1`
alongside `-DWAYMESH_GPS=1` — the full v2/auth relay + GPS originator with the WiFi
provisioning portal. The earlier **GPS↔portal UART0 share** concern is resolved in
`checkPortalTrigger`: the serial-`c` fallback reads UART0 only while the console
owns it (GPS in `DEBUG`, i.e. before NMEA lock or after a no-GPS revert), so it never
steals the GPS's NMEA bytes; the GPIO0 long-press trigger is independent of UART0 and
always works. `bayck_portal` is retained as the GPS-off bench twin (stable console).
Builds: `bayck_7pwm` / `betafpv_nano` (+`*_gpstest`) Flash ~44.6%, RAM ~41%. The
v0/v1 path stays in `main.cpp` (`#if !WAYMESH_V2`) as a flag-toggled legacy build.

## 11 — Resolved decisions

The earlier open questions are now decided (rationale inline above):

1. **Meshtastic byte-compat (Q1):** pin `protobufs` at the commit matching the
   reported `firmware_version`; **generate** known-answer vectors from the upstream
   `meshtastic` CLI at that version and assert them in CI (§10). Always use
   **explicit, non-empty channel names** to avoid the preset-derived-name hash
   coupling (§4).
2. **Multi-channel (Q2):** one **home** TX channel per node; multi-channel on RX +
   at the gateway (up to 8) — the air stays single-home (§8).
3. **Presence privacy (Q3):** clear header + encrypted position by default; an
   optional per-channel **stealth** mode (rotating pseudonyms) for traffic-analysis
   resistance, at the documented cost of keyless relay (§5).
4. **Nonce safety (Q4):** `packetId` = NVS **reserve-ahead** monotonic counter
   (crash-safe, no extra wire bytes), HW-RNG seed only on corrupt NVS (§8).
5. **Integrity vs signatures (Q5):** **default-on 4-byte MIC** (cheap, closes
   outsider tampering, gateway-internal → no app-compat cost, §5); **per-node
   signatures deferred** indefinitely as too expensive for broadcast beacons (§9.4).

### Still open (narrow)

- The exact pinned upstream commit hash (set it when implementation starts).
- Per-channel beacon **cadence** values — an airtime-tuning question for P3/P9, not
  a protocol one.
- `packetId` reserve-ahead **block size** (1024 is a starting point; tune against
  flash wear on the actual NVS partition).

## Sources

- [Meshtastic encryption overview](https://meshtastic.org/docs/overview/encryption/)
- [Meshtastic protobufs — channel.proto](https://github.com/meshtastic/protobufs/blob/master/meshtastic/channel.proto)
- [Meshtastic firmware — CryptoEngine / Channels (hash, nonce, default key)](https://github.com/meshtastic/firmware/tree/master/src/mesh)
