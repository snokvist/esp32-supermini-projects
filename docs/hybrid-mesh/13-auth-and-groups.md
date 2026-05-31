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
  provisioning ourselves (QR/URL/UI), which is exactly the custom-UU we're
  avoiding.
- ➖ A custom MIC is a new crypto surface to design, test, and get right.
- ➕ Smallest possible wire bytes; no dependency on Meshtastic's crypto details.

### Option B — reuse Meshtastic's channel + PSK scheme (recommended)

Meshtastic **already has both primitives we want**, and the Android/iOS/CLI apps
already implement the entire management UX for them:

- A **channel** (name → 1-byte hash) **is** group identity.
- The channel **PSK** gives **membership gating + confidentiality** (AES-CTR).
- The app already does channel create / QR / shareable URL / key rotation.

We adopt the *scheme* (channel-hash for filtering, PSK/AES-CTR for crypto) on our
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
| **Confidentiality** | ✅ yes | AES-CTR with the PSK; only members read positions/text |
| **Group membership "auth"** | ✅ yes (implicit) | only a key-holder can produce a packet that decrypts sanely |
| **Per-node authentication** | ❌ **no** | any group member can forge any other node's `srcNodeID` |
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
 byte 3     : flags     (u8)                 bit0 POS valid, bit1 ENCRYPTED, bit2 has-MIC (rsvd), bits3-7 rsvd=0
 byte 4-7   : srcNodeID (u32, LE)            originator (relay identity + nonce material)
 byte 8-11  : packetId  (u32, LE)            dedup MessageID + AES-CTR nonce material

payload (POS, 10 B when present), AES-CTR-encrypted in place when flags.ENCRYPTED:
 byte 0-3   : lat_i (i32, 1e-7 deg)
 byte 4-7   : lon_i (i32, 1e-7 deg)
 byte 8     : sats (u8)
 byte 9     : payload-flags (u8, rsvd=0)
```

Key changes from `v1`:

1. **`chanHash` (new):** the Meshtastic 1-byte channel hash (§4). This is the
   `GroupID` from [05](05-protocol.md), realized as the Meshtastic channel hash so
   the app's channels *are* our groups.
2. **`packetId:32` replaces `seq:16`.** The dedup `MessageID` becomes
   `(srcNodeID:32, packetId:32)`. A 32-bit id is needed as a CTR nonce that won't
   wrap over a node's lifetime (a 16-bit `seq` wraps in ~minutes-to-hours at beacon
   cadence → nonce reuse → CTR keystream reuse, a real break). Generate `packetId`
   as a per-node monotonic counter seeded from a random base at boot.
3. **`flags.ENCRYPTED`:** when set, the payload is AES-CTR ciphertext (§5). The
   header (magic..packetId) is **always plaintext** — relays and the dedup path
   read it without the key (§6).

**Byte budget:** v2 POS beacon = 12 (header) + 10 (payload) = **22 B** (vs v1's
18 B). At SF9/BW812 that's still well under ~20 ms ToA
([05 §airtime](05-protocol.md)) — the 4 extra bytes are negligible.

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

A node is provisioned with **one or more channels** (name + PSK). Its set of
accepted `chanHash`es is derived from those. The "open" default group is the
Meshtastic default channel (`psk={0x01}`), which keeps today's free-for-all as a
selectable mode, not the only mode.

## 5 — Encryption / MIC

Reuse Meshtastic's payload crypto so the gateway↔app boundary is a straight
passthrough of plaintext (read path needs no crypto on the app side — the gateway
decrypts, just like a real radio,
[11 §Channels & crypto](11-mobile-gateway-meshtastic-compat.md)).

- **Cipher:** AES-CTR, key = the channel's expanded PSK (AES-128 or -256 by key
  length). Encrypt **only the payload**, in place; the header stays plaintext.
- **Nonce (16 B CTR initial block)** — construct from header fields so it's unique
  per (key, message) and reconstructable by any receiver (verify against
  Meshtastic's `CryptoEngine::initNonce`):
  ```
  bytes 0-3  : packetId (u32 LE)
  bytes 4-7  : 0
  bytes 8-11 : srcNodeID (u32 LE)
  bytes 12-15: 0
  ```
  This is why §3 widens to a 32-bit `packetId`: nonce uniqueness depends on it not
  wrapping under a fixed key.
- **MIC:** stock Meshtastic channel packets carry **no MIC** (CTR has no integrity
  tag — group membership is the only "auth"). To match Meshtastic and the §2 model,
  **ship v2 with no MIC** (`flags.has-MIC = 0`, reserved). The bit and a 4-byte
  truncated-CMAC tail are reserved for the optional later "integrity track" so the
  byte layout doesn't change when/if it lands.

PRESENCE/no-position beacons (header only) are sent in clear (`flags.ENCRYPTED=0`):
there's nothing secret in "node X is alive," and it keeps liveness cheap. Make this
a per-channel policy knob (some deployments may want even presence hidden → then
encrypt an empty/padded payload).

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
5. if flags.ENCRYPTED: AES-CTR decrypt payload under the channel
      whose hash matches; if no configured channel decrypts to a
      sane payload -> DROP                                       <-- NEW
6. accept: log, upsert peer, feed bleGattOnPeer(...)            (as today)
```

Step 3 is the **group filter** ("share spectrum without merging",
[04 §GroupID](04-architecture.md)). Step 5 is the implicit membership check.

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
- **No decryption, no re-encryption** at the relay — `raw[]` is re-sent byte-for-byte
  (`relaySchedule`/`relayService` unchanged), so `srcNodeID`/`packetId`/ciphertext
  are preserved and the gateway still attributes presence to the originator.

**ESP8285 crypto note:** only the **originator** (a Tier-2 8285 that beacons its own
position) needs AES, and only on its own ~10-byte payload at beacon cadence —
software AES is fine. A **Tier-3 relay needs no crypto at all.** The XR2 (C3) has
an AES peripheral.

## 7 — Gateway / Meshtastic-client integration (`ble_gatt.cpp`)

The gateway is the crypto boundary. Changes layer onto the existing handshake
([11](11-mobile-gateway-meshtastic-compat.md), `queueConfigSequence`):

1. **Advertise the channel set.** Emit `FromRadio{channel: Channel}` frames for
   each provisioned channel (name + PSK + index) during the handshake — the app
   adopts them ([11 §Channels & crypto](11-mobile-gateway-meshtastic-compat.md)).
   Today only the implicit default is assumed; make the set explicit.
2. **Read path (OTA → app):** when a v2 beacon is accepted+decrypted (§6), map to
   `NodeInfo`/`Position`/text **tagged with the matching channel index** on the
   `MeshPacket` so the app shows it on the right channel. (`buildNodeInfoFrame` /
   `buildPositionPacketFrame` already produce plaintext to the app — add the
   channel index.)
3. **Write path (app → OTA):** the stubbed `ToRadio{packet}` TX path
   (`ble_gatt.cpp:336-338`) becomes: the phone sends `MeshPacket.encrypted` on a
   channel → gateway decrypts with that channel's PSK (it advertised it) → recovers
   text → **re-encrypts into a v2 TEXT beacon** under the same channel's key+hash →
   floods onto LoRa. (Implements [09 Phase G](09-poc-roadmap.md) increment 2 with
   real channel crypto instead of "any PSK".)
4. **Bonding:** pair the OTA channel-PSK story with BLE bonding/PIN
   ([11 §BLE GATT](11-mobile-gateway-meshtastic-compat.md), currently open/no-bond,
   `ble_gatt.cpp:368`) so the *link* to the phone is also protected, not just the
   OTA payload.

## 8 — Provisioning

- **Per-node config:** a channel list `[(name, psk, index)]`, plus the relay policy
  (`relay-known` | `relay-all`). Source it from a build-time config / NVS — no
  cloud, no account ([01 non-goals](01-vision-and-requirements.md)).
- **Sharing:** since channels are Meshtastic channels, a group is shared with the
  **standard Meshtastic channel QR/URL** out of the app — zero custom tooling.
- **Default:** ship the Meshtastic default channel (`psk={0x01}`) so an
  unprovisioned node still interoperates on the open group (today's behavior),
  selectable rather than mandatory.

## 9 — Phasing (slots into [09](09-poc-roadmap.md))

Build on top of Phase G / Phase 5, incrementally:

1. **Group filter only (no crypto).** Add `chanHash` to the v2 header; implement
   acceptance step 3 + relay policy. Positions still in clear. *Proves* spectrum
   sharing / group separation cheaply. **Gate:** two groups on one band ignore each
   other; a `relay-all` relay still carries both.
2. **Channel encryption.** Add AES-CTR (§5) + the 32-bit `packetId` nonce; gateway
   decrypts on read. **Gate:** only key-holders see positions/text; the app shows
   them on the right channel; relay still works without the key.
3. **TX with channel crypto.** Wire the gateway write path (§7.3). **Gate:** phone
   text round-trips on a chosen channel over multi-hop LoRa.
4. **(Optional, later) per-node integrity track.** Only if the threat model demands
   it — the reserved `has-MIC` bit + tag, or a per-node signature (§2).

## 10 — Test / verification plan

- **Vectors:** unit-test `chanHash` and AES-CTR encrypt/decrypt against **known
  Meshtastic test vectors** (default key + a named channel) so our bytes match the
  app exactly. This is the single highest-value check — get it green before any RF.
- **Group filter (bench):** two XR2s on channel A, one on channel B → A-nodes drop
  B's beacons (step 3); a `relay-all` 8285 still re-floods both (`relay==rx`);
  a `relay-known` 8285 on A drops B.
- **Crypto round-trip:** XR2(A) encrypts → 8285 relay (no key) re-floods verbatim →
  XR2(B, has key) decrypts → renders in the stock Meshtastic app on channel A.
  Reuse the verified Tier-2→Tier-1→app chain ([09 Phase H](09-poc-roadmap.md)).
- **Nonce-uniqueness soak:** run a node past where a 16-bit seq would have wrapped;
  confirm `packetId` never repeats under one key (no CTR reuse).
- **Negative:** wrong-key node fails to decrypt and drops (no plaintext leak);
  bit-flipped ciphertext → garbage payload dropped by the sanity check (documents
  the no-MIC limitation from §2).

## 11 — Open questions

- Exact `chanHash` / nonce / default-key-expansion bytes vs the **current**
  Meshtastic source (pin a commit alongside the pinned `firmware_version`,
  [11](11-mobile-gateway-meshtastic-compat.md)).
- Multi-channel on one node: how many channels to support OTA vs just at the
  gateway; per-channel beacon cadence.
- Whether presence (header-only) should ever be encrypted/padded for traffic
  analysis resistance, or always clear (§5).
- `packetId` persistence across reboot (NVS counter vs random reseed) to guarantee
  no nonce reuse after a crash under the same key.
- Whether the optional per-node integrity/signature track (§2, §9.4) is ever worth
  the bytes/CPU for our threat model.

## Sources

- [Meshtastic encryption overview](https://meshtastic.org/docs/overview/encryption/)
- [Meshtastic protobufs — channel.proto](https://github.com/meshtastic/protobufs/blob/master/meshtastic/channel.proto)
- [Meshtastic firmware — CryptoEngine / Channels (hash, nonce, default key)](https://github.com/meshtastic/firmware/tree/master/src/mesh)
