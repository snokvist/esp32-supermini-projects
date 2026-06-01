#include "ble_gatt.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <pb_decode.h>
#include <pb_encode.h>
#include "waymesh_mesh.pb.h"

// Meshtastic client API UUIDs — verified against
// https://meshtastic.org/docs/development/device/client-api/ (2026-05-24).
static const char *MESHTASTIC_SERVICE_UUID = "6ba1b218-15a8-461f-9fa8-5dcae273eafd";
static const char *TORADIO_UUID   = "f75c76d2-129e-4dad-a1dd-7866124401e7"; // write
static const char *FROMRADIO_UUID = "2c55e69e-4993-11ed-b878-0242ac120002"; // read
static const char *FROMNUM_UUID   = "ed9da18c-a800-4f66-a670-aa7547e34453"; // read + notify

// Pinned firmware version reported in DeviceMetadata. This is the field the
// Meshtastic app gates feature/protocol compatibility on, so it is a deliberate
// knob — bump it (and re-test) when chasing app/protobuf changes.
//
// Pinned upstream for byte-compatibility (auth/group work, docs/hybrid-mesh/13):
//   meshtastic/firmware  v2.6.4.b89355f  b89355ffa60b3893417004b07e7b96f04b17022c
//   meshtastic/protobufs v2.6.4          f00e96f12da48abfa9a992f8b5546fd75a370250
// Channel hash + PSK/default-key expansion validated against this ref in
// lib/waymesh_crypto (test/test_channel_hash). AdminMessage field numbers
// locked for the runtime channel-set path (§8.1, lands later): ADMIN_APP
// portnum=6; AdminMessage.set_channel=33, get_channel_request=1,
// get_channel_response=2, session_passkey=101 (bytes, top-level, 300 s expiry);
// ChannelSettings{psk=2,name=3,id=4}; Channel{index=1,settings=2,role=3}.
#define WAYMESH_FW_VERSION "2.6.4.waymesh"
#define WAYMESH_HW_MODEL   255u  // HardwareModel.PRIVATE_HW — non-Meshtastic hardware
#define WAYMESH_ROLE       0u    // Config.DeviceConfig.Role.CLIENT
#define MESH_LOC_INTERNAL  2u    // Position.LocSource.LOC_INTERNAL
#define MESH_PORTNUM_POSITION 3u // PortNum.POSITION_APP
#define MESH_BROADCAST_ADDR 0xFFFFFFFFu
#define MESH_CHANNEL_ROLE_DISABLED  0u
#define MESH_CHANNEL_ROLE_PRIMARY   1u
#define MESH_CHANNEL_ROLE_SECONDARY 2u

static_assert(sizeof(WAYMESH_FW_VERSION) <= 24,
              "WAYMESH_FW_VERSION must fit DeviceMetadata.firmware_version[24]");

static NimBLECharacteristic *gFromRadio = nullptr;
static NimBLECharacteristic *gFromNum = nullptr;

static volatile bool gBleConnected = false;
// True once this connection's want_config handshake has been queued. Gates live
// emits so nothing is pushed before the client asks for config (matches real
// Meshtastic ordering: my_info/metadata/nodedb/complete come first). Written on
// the BLE host task (onConnect/onDisconnect/queueConfigSequence), read on the
// main loop task (bleGattSetPosition).
static volatile bool gConfigDone = false;
static uint32_t gFromNumVal = 0;

// --- provisioned channel set (advertised to the app; read-only this phase) ---
// Set once from main after wm_config_init; process-lifetime pointer. Read on the
// BLE host task (handshake) and the main loop task (peer hash->index); both are
// reads with no concurrent writer in this phase, so no lock is needed.
static const wm_config_t *gCfgRef = nullptr;

// Meshtastic channel index for our home (TX) channel — the primary the app
// shows our own node + same-group peers on. 0 (primary) before channels load.
static uint8_t homeChannelIndex() {
  if (gCfgRef && gCfgRef->channel_count)
    return gCfgRef->channels[gCfgRef->home_slot].index;
  return 0;
}

// Map a heard chanHash to its Meshtastic channel index (the table position the
// app knows). Falls back to 0 (primary) if not a configured channel — accepted
// frames always resolve, but a relayed/edge frame degrades gracefully.
static uint8_t channelIndexForHash(uint8_t hash) {
  if (gCfgRef) {
    const wm_channel_t *c = wm_config_channel_by_hash(gCfgRef, hash);
    if (c) return c->index;
  }
  return 0;
}

// --- self identity / latest GPS fix + epoch (fed from main.cpp) --------------
static uint32_t gNodeId = 0;
static portMUX_TYPE gPosMux = portMUX_INITIALIZER_UNLOCKED;  // guards the below
static int32_t gLatI = 0, gLonI = 0;
static uint32_t gSats = 0;
static bool gPosValid = false;
static uint32_t gEpoch = 0;  // current UTC epoch seconds (0 = unknown)
// Rate-limit for the live SELF NodeInfo emit (main loop task only — set/read
// from bleGattSetPosition). 0 = not yet emitted this connection.
static unsigned long gSelfLastEmitMs = 0;

// --- peer node DB (Phase G 1b) ----------------------------------------------
// Peers heard over LoRa, projected to Meshtastic NodeInfo. Written from the main
// loop task (bleGattOnPeer/handleRx), read from the BLE host task (handshake).
static const int kMaxPeers = 16;
static const unsigned long kPeerEmitMinMs = 3000;  // min gap between live emits
struct Peer {
  uint32_t nodeId;
  int32_t latI, lonI;
  uint32_t sats;
  bool posValid;
  float snr;
  uint32_t lastHeardEpoch;
  uint8_t chanHash;          // group the peer was heard on -> NodeInfo.channel
  unsigned long lastEmitMs;  // 0 = not yet emitted this connection
  bool used;
};
static Peer gPeers[kMaxPeers];
static portMUX_TYPE gDbMux = portMUX_INITIALIZER_UNLOCKED;

// Find a peer slot by id, else a free slot, else evict the stalest. Caller holds
// gDbMux. Never returns null (eviction guarantees a slot).
static Peer *peerSlot(uint32_t nodeId) {
  Peer *freeSlot = nullptr;
  Peer *stalest = &gPeers[0];
  for (int i = 0; i < kMaxPeers; i++) {
    if (gPeers[i].used && gPeers[i].nodeId == nodeId) return &gPeers[i];
    if (!gPeers[i].used && !freeSlot) freeSlot = &gPeers[i];
    if (gPeers[i].lastHeardEpoch < stalest->lastHeardEpoch) stalest = &gPeers[i];
  }
  return freeSlot ? freeSlot : stalest;
}

// --- FromRadio frame queue ---------------------------------------------------
// Each Meshtastic client read pops one serialized FromRadio frame; the client
// reads until it gets an empty frame, then waits for the next FromNum notify.
static const size_t kFrameMax = 384;  // > meshtastic_FromRadio_size (333)
// The whole handshake (my_info + metadata + self + every peer + complete) is
// enqueued atomically on the BLE task before the first read, so the ring must
// hold kMaxPeers + ~4 overhead frames at once.
static const int kQDepth = 24;
struct FrameQ {
  uint8_t buf[kFrameMax];
  uint16_t len;
};
static FrameQ gQ[kQDepth];
static int gQHead = 0, gQTail = 0, gQCount = 0;
static portMUX_TYPE gQMux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool gFromNumDirty = false;
static uint32_t gFrameIdSeq = 0;

static bool qPush(const uint8_t *d, size_t n) {
  if (n > kFrameMax) return false;
  bool ok = false;
  portENTER_CRITICAL(&gQMux);
  if (gQCount < kQDepth) {
    FrameQ &f = gQ[gQTail];
    memcpy(f.buf, d, n);
    f.len = (uint16_t)n;
    gQTail = (gQTail + 1) % kQDepth;
    gQCount++;
    gFromNumDirty = true;  // set under the lock so the consumer can't lose it
    ok = true;
  }
  portEXIT_CRITICAL(&gQMux);
  return ok;
}

// Copies the next frame into out (capacity cap), returns its length (>=0), or
// -1 if empty. Clamps to cap so the copy is bounded at the copy site, not only
// by qPush's push-time check.
static int qPop(uint8_t *out, size_t cap) {
  int n = -1;
  portENTER_CRITICAL(&gQMux);
  if (gQCount > 0) {
    FrameQ &f = gQ[gQHead];
    size_t len = f.len < cap ? f.len : cap;
    memcpy(out, f.buf, len);
    n = (int)len;
    gQHead = (gQHead + 1) % kQDepth;
    gQCount--;
  }
  portEXIT_CRITICAL(&gQMux);
  return n;
}

// Frame production now has two producer tasks: the NimBLE host task (handshake
// in the ToRadio onWrite callback) and the main loop task (live peer NodeInfo
// via bleGattOnPeer/handleRx). gFrameIdSeq is incremented under gQMux to stay
// safe across both.
static bool enqueueFromRadio(meshtastic_FromRadio *fr) {
  portENTER_CRITICAL(&gQMux);
  fr->id = ++gFrameIdSeq;
  portEXIT_CRITICAL(&gQMux);
  uint8_t tmp[kFrameMax];
  pb_ostream_t os = pb_ostream_from_buffer(tmp, sizeof(tmp));
  if (!pb_encode(&os, meshtastic_FromRadio_fields, fr)) {
    Serial.printf("# BLE FromRadio encode failed: %s\n", PB_GET_ERROR(&os));
    return false;
  }
  if (!qPush(tmp, os.bytes_written)) {  // qPush sets gFromNumDirty under the lock
    Serial.println("# BLE FromRadio queue full — frame dropped");
    return false;
  }
  return true;
}

// Fill a FromRadio{node_info} for one node (self or peer). snr 0 for self.
static void buildNodeInfoFrame(meshtastic_FromRadio *fr, uint32_t nodeId,
                               bool posValid, int32_t latI, int32_t lonI,
                               uint32_t sats, float snr, uint32_t lastHeard,
                               uint8_t channelIdx) {
  *fr = meshtastic_FromRadio_init_zero;
  fr->which_payload_variant = meshtastic_FromRadio_node_info_tag;
  meshtastic_NodeInfo &ni = fr->payload_variant.node_info;
  ni.num = nodeId;
  ni.channel = channelIdx;  // the group the app shows this node on (§7)
  ni.has_user = true;
  snprintf(ni.user.id, sizeof(ni.user.id), "!%08x", (unsigned)nodeId);
  snprintf(ni.user.long_name, sizeof(ni.user.long_name), "Waymesh_%04X",
           (unsigned)(nodeId & 0xFFFF));
  snprintf(ni.user.short_name, sizeof(ni.user.short_name), "%04X",
           (unsigned)(nodeId & 0xFFFF));
  ni.user.hw_model = WAYMESH_HW_MODEL;
  ni.user.role = WAYMESH_ROLE;
  ni.snr = snr;
  ni.last_heard = lastHeard;
  if (posValid) {
    ni.has_position = true;
    ni.position.latitude_i = latI;
    ni.position.longitude_i = lonI;
    ni.position.location_source = MESH_LOC_INTERNAL;
    ni.position.sats_in_view = sats;
    ni.position.time = lastHeard;
  }
}

// Build a FromRadio{packet} carrying a POSITION_APP Data payload for one node
// (#2 supplementary). Stock Meshtastic streams live position updates as a
// MeshPacket, not a bare NodeInfo; we send this ALONGSIDE the NodeInfo so a
// stock/strict client and the NodeInfo-only gateway app both work. Each packet
// gets a unique id so the client doesn't dedup successive updates. snr 0 for
// self. Returns false if the inner Position fails to encode. Main-loop task only
// (the pktId counter needs no lock). Live emits only — the want_config nodedb
// dump stays bare NodeInfo, matching how real firmware downloads the node DB.
static bool buildPositionPacketFrame(meshtastic_FromRadio *fr, uint32_t nodeId,
                                     int32_t latI, int32_t lonI, uint32_t sats,
                                     float snr, uint32_t epoch,
                                     uint8_t channelIdx) {
  meshtastic_Position pos = meshtastic_Position_init_zero;
  pos.latitude_i = latI;
  pos.longitude_i = lonI;
  pos.location_source = MESH_LOC_INTERNAL;
  pos.sats_in_view = sats;
  pos.time = epoch;

  *fr = meshtastic_FromRadio_init_zero;
  fr->which_payload_variant = meshtastic_FromRadio_packet_tag;
  meshtastic_MeshPacket &mp = fr->payload_variant.packet;
  mp.from = nodeId;
  mp.to = MESH_BROADCAST_ADDR;
  mp.channel = channelIdx;  // the channel the app renders this position on (§7)
  mp.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
  meshtastic_Data &d = mp.payload_variant.decoded;
  d.portnum = MESH_PORTNUM_POSITION;
  pb_ostream_t ps =
      pb_ostream_from_buffer(d.payload.bytes, sizeof(d.payload.bytes));
  if (!pb_encode(&ps, meshtastic_Position_fields, &pos)) {
    Serial.printf("# BLE Position encode failed: %s\n", PB_GET_ERROR(&ps));
    return false;
  }
  d.payload.size = (pb_size_t)ps.bytes_written;
  static uint32_t pktId = 0;
  mp.id = ++pktId;
  mp.rx_time = epoch;
  mp.rx_snr = snr;
  return true;
}

// Build + queue the connect handshake in response to ToRadio{want_config_id}.
static void queueConfigSequence(uint32_t wantConfigId) {
  // 1) MyNodeInfo
  meshtastic_FromRadio fr = meshtastic_FromRadio_init_zero;
  fr.which_payload_variant = meshtastic_FromRadio_my_info_tag;
  meshtastic_MyNodeInfo &mi = fr.payload_variant.my_info;
  mi.my_node_num = gNodeId;
  mi.reboot_count = 1;
  mi.min_app_version = 30200;
  enqueueFromRadio(&fr);

  // 2) DeviceMetadata — the app's compatibility gate.
  fr = meshtastic_FromRadio_init_zero;
  fr.which_payload_variant = meshtastic_FromRadio_metadata_tag;
  meshtastic_DeviceMetadata &md = fr.payload_variant.metadata;
  snprintf(md.firmware_version, sizeof(md.firmware_version), "%s",
           WAYMESH_FW_VERSION);
  md.device_state_version = 22;
  md.hasBluetooth = true;
  md.role = WAYMESH_ROLE;
  md.hw_model = WAYMESH_HW_MODEL;
  enqueueFromRadio(&fr);

  // 2b) Advertise the provisioned channel set (§7): one FromRadio{channel} per
  // channel so the app adopts our groups. psk is the STORED Meshtastic PSK (not
  // the expanded key) so the app derives the same hash + key we use (§4).
  int chans = 0;
  if (gCfgRef) {
    for (uint8_t i = 0; i < gCfgRef->channel_count; i++) {
      const wm_channel_t *c = &gCfgRef->channels[i];
      fr = meshtastic_FromRadio_init_zero;
      fr.which_payload_variant = meshtastic_FromRadio_channel_tag;
      meshtastic_Channel &ch = fr.payload_variant.channel;
      ch.index = c->index;
      ch.role = (i == gCfgRef->home_slot) ? MESH_CHANNEL_ROLE_PRIMARY
                                          : MESH_CHANNEL_ROLE_SECONDARY;
      ch.has_settings = true;
      size_t pk = c->psk_len <= sizeof(ch.settings.psk.bytes)
                      ? c->psk_len
                      : sizeof(ch.settings.psk.bytes);
      memcpy(ch.settings.psk.bytes, c->psk, pk);
      ch.settings.psk.size = (pb_size_t)pk;
      snprintf(ch.settings.name, sizeof(ch.settings.name), "%s", c->name);
      ch.settings.id = 0;  // app derives the channel from name+psk; id unused
      enqueueFromRadio(&fr);
      chans++;
    }
  }

  // 3) Self NodeInfo (with the live GPS position, if we have a fix).
  int32_t latI, lonI;
  uint32_t sats, epoch;
  bool posValid;
  portENTER_CRITICAL(&gPosMux);
  latI = gLatI; lonI = gLonI; sats = gSats; posValid = gPosValid; epoch = gEpoch;
  portEXIT_CRITICAL(&gPosMux);
  buildNodeInfoFrame(&fr, gNodeId, posValid, latI, lonI, sats, 0.0f, epoch,
                     homeChannelIndex());
  enqueueFromRadio(&fr);

  // 4) One NodeInfo per peer heard over LoRa (snapshot each under the DB lock,
  //    then build/encode outside it).
  int peers = 0;
  for (int i = 0; i < kMaxPeers; i++) {
    Peer p{};
    bool used;
    portENTER_CRITICAL(&gDbMux);
    used = gPeers[i].used;
    if (used) p = gPeers[i];
    portEXIT_CRITICAL(&gDbMux);
    if (!used) continue;
    buildNodeInfoFrame(&fr, p.nodeId, p.posValid, p.latI, p.lonI, p.sats, p.snr,
                       p.lastHeardEpoch, channelIndexForHash(p.chanHash));
    enqueueFromRadio(&fr);
    peers++;
  }

  // 5) config_complete_id — echoes the nonce; the client treats this as "done".
  fr = meshtastic_FromRadio_init_zero;
  fr.which_payload_variant = meshtastic_FromRadio_config_complete_id_tag;
  fr.payload_variant.config_complete_id = wantConfigId;
  enqueueFromRadio(&fr);

  Serial.printf("# BLE handshake queued (%d chan(s) + self + %d peer(s)) for "
                "want_config_id=%u\n",
                chans, peers, (unsigned)wantConfigId);

  // Open the live-emit gate now that the client has its config dump. Stamp the
  // self emit-gate so the first live self update waits a full interval after the
  // handshake's self frame (no near-duplicate right after ConfigComplete).
  gSelfLastEmitMs = millis();
  gConfigDone = true;
}

// --- GATT callbacks ----------------------------------------------------------
class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *) override {
    gBleConnected = true;
    gConfigDone = false;  // hold live emits until this client's want_config
    Serial.println("# BLE connect");
  }
  void onDisconnect(NimBLEServer *) override {
    gBleConnected = false;
    gConfigDone = false;
    // Drop any half-drained handshake so the next client starts clean.
    portENTER_CRITICAL(&gQMux);
    gQHead = gQTail = gQCount = 0;
    gFromNumDirty = false;
    portEXIT_CRITICAL(&gQMux);
    Serial.println("# BLE disconnect; re-advertising");
    NimBLEDevice::startAdvertising();
  }
  void onMTUChange(uint16_t mtu, ble_gap_conn_desc *) override {
    Serial.printf("# BLE mtu=%u\n", (unsigned)mtu);
  }
};

class ToRadioCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c) override {
    std::string v = c->getValue();
    meshtastic_ToRadio tr = meshtastic_ToRadio_init_zero;
    pb_istream_t is =
        pb_istream_from_buffer((const uint8_t *)v.data(), v.size());
    if (!pb_decode(&is, meshtastic_ToRadio_fields, &tr)) {
      Serial.printf("# BLE ToRadio decode err (%u B): %s\n", (unsigned)v.size(),
                    PB_GET_ERROR(&is));
      return;
    }
    switch (tr.which_payload_variant) {
      case meshtastic_ToRadio_want_config_id_tag:
        Serial.printf("# BLE ToRadio want_config_id=%u\n",
                      (unsigned)tr.payload_variant.want_config_id);
        queueConfigSequence(tr.payload_variant.want_config_id);
        break;
      case meshtastic_ToRadio_disconnect_tag:
        Serial.println("# BLE ToRadio disconnect");
        break;
      case meshtastic_ToRadio_packet_tag:
        // Layer 2 increment 2: decrypt with advertised PSK + flood onto LoRa.
        Serial.println("# BLE ToRadio packet (TX) — not yet routed to mesh");
        break;
      default:
        Serial.printf("# BLE ToRadio variant=%u (ignored)\n",
                      (unsigned)tr.which_payload_variant);
        break;
    }
  }
};

class FromRadioCallbacks : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic *c) override {
    uint8_t out[kFrameMax];
    int n = qPop(out, sizeof(out));
    if (n >= 0) {
      c->setValue(out, n);
      Serial.printf("# BLE FromRadio-> %d B\n", n);
    } else {
      c->setValue(out, 0);  // empty: client has caught up
    }
  }
};

void bleGattSetChannels(const wm_config_t *cfg) { gCfgRef = cfg; }

void bleGattBegin(uint32_t nodeId) {
  gNodeId = nodeId;
  char name[20];
  snprintf(name, sizeof(name), "Waymesh_%04X", (unsigned)(nodeId & 0xFFFF));

  NimBLEDevice::init(name);
  NimBLEDevice::setMTU(517);  // Meshtastic FromRadio frames approach ~512 B.
  // Layer 1/2: open (no bonding) for easy host bring-up. Bonding + PIN later.

  NimBLEServer *server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  NimBLEService *svc = server->createService(MESHTASTIC_SERVICE_UUID);

  gFromRadio = svc->createCharacteristic(FROMRADIO_UUID, NIMBLE_PROPERTY::READ);
  gFromRadio->setCallbacks(new FromRadioCallbacks());

  NimBLECharacteristic *toRadio = svc->createCharacteristic(
      TORADIO_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  toRadio->setCallbacks(new ToRadioCallbacks());

  gFromNum = svc->createCharacteristic(
      FROMNUM_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  gFromNum->setValue(gFromNumVal);  // uint32 LE, matches Meshtastic FromNum

  svc->start();

  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(MESHTASTIC_SERVICE_UUID);
  adv->setScanResponse(true);
  NimBLEDevice::startAdvertising();

  Serial.printf("# BLE GATT up: name=%s service=%s (Meshtastic L2, fw=%s)\n",
                name, MESHTASTIC_SERVICE_UUID, WAYMESH_FW_VERSION);
}

void bleGattLoop() {
  if (!gBleConnected || !gFromNum) return;
  bool dirty;
  int qn;
  portENTER_CRITICAL(&gQMux);
  dirty = gFromNumDirty;
  gFromNumDirty = false;
  qn = gQCount;
  portEXIT_CRITICAL(&gQMux);
  if (!dirty) return;
  gFromNumVal++;  // monotonic "new data available" counter (main task only)
  gFromNum->setValue(gFromNumVal);
  gFromNum->notify();
  Serial.printf("# BLE FromNum notify=%u (q=%d)\n", (unsigned)gFromNumVal, qn);
}

// Emit a live SELF NodeInfo to a connected client (rate-limited, gated on the
// want_config handshake). Shared by the GPS-fix path (bleGattSetPosition) and
// the beacon-cadence heartbeat (bleGattHeartbeat) so this node announces itself
// like a LoRa peer even with no fix — a bare NodeInfo (no has_position) when
// valid is false, never a 0,0. Only after the handshake (gConfigDone) so we
// never queue a frame before the client's want_config. Main-loop task only, so
// the emit-gate static needs no lock and enqueueFromRadio runs outside gPosMux.
static void emitSelfNodeInfo(bool valid, int32_t latI, int32_t lonI,
                             uint32_t sats, uint32_t epoch) {
  if (!gBleConnected || !gConfigDone) return;
  const unsigned long now = millis();
  if (gSelfLastEmitMs != 0 && now - gSelfLastEmitMs < kPeerEmitMinMs) return;
  gSelfLastEmitMs = now;
  meshtastic_FromRadio fr;
  uint8_t home = homeChannelIndex();
  buildNodeInfoFrame(&fr, gNodeId, valid, latI, lonI, sats, 0.0f, epoch, home);
  enqueueFromRadio(&fr);
  // #2: the stock-style MeshPacket{POSITION} (supplementary; the NodeInfo above
  // is what the gateway app parses). ONLY with a real fix — a no-fix emit would
  // broadcast 0,0 and teleport the client to null island. Mirrors the per-peer
  // `if (snap.posValid && ...)` guard in bleGattOnPeer.
  if (valid && buildPositionPacketFrame(&fr, gNodeId, latI, lonI, sats, 0.0f,
                                        epoch, home))
    enqueueFromRadio(&fr);
  Serial.printf("# BLE self %08X -> NodeInfo%s (live)\n", (unsigned)gNodeId,
                valid ? " + Position" : "");
}

void bleGattSetPosition(int32_t lat_i, int32_t lon_i, uint32_t sats_in_view,
                        bool valid) {
  uint32_t epoch;
  portENTER_CRITICAL(&gPosMux);
  gLatI = lat_i;
  gLonI = lon_i;
  gSats = sats_in_view;
  gPosValid = valid;
  epoch = gEpoch;  // snapshot for the self NodeInfo (set just before us in loop)
  portEXIT_CRITICAL(&gPosMux);

  // Live SELF NodeInfo on a fresh fix: the want_config handshake emits this node
  // once, but nothing re-streamed it after ConfigComplete (only peers had a live
  // path via bleGattOnPeer). This keeps THIS node's moving position streaming.
  emitSelfNodeInfo(valid, lat_i, lon_i, sats_in_view, epoch);
}

// Periodic self announcement so this node stays in a connected client's node
// list like the LoRa peers do (they re-emit on every beacon heard via
// bleGattOnPeer). Driven by the beacon-TX cadence in loop(); carries the last
// known fix if we have one, else a bare no-pos NodeInfo. Shares the self
// rate-limit gate with bleGattSetPosition, so it never doubles up while a fix is
// actively streaming. Main-loop task only.
void bleGattHeartbeat() {
  int32_t latI, lonI;
  uint32_t sats, epoch;
  bool valid;
  portENTER_CRITICAL(&gPosMux);
  latI = gLatI;
  lonI = gLonI;
  sats = gSats;
  valid = gPosValid;
  epoch = gEpoch;
  portEXIT_CRITICAL(&gPosMux);
  emitSelfNodeInfo(valid, latI, lonI, sats, epoch);
}

void bleGattSetTime(uint32_t epoch) {
  portENTER_CRITICAL(&gPosMux);
  gEpoch = epoch;
  portEXIT_CRITICAL(&gPosMux);
}

void bleGattOnPeer(uint32_t node_id, int32_t lat_i, int32_t lon_i,
                   uint32_t sats_in_view, bool pos_valid, float snr,
                   uint8_t chan_hash) {
  uint32_t epoch;
  portENTER_CRITICAL(&gPosMux);
  epoch = gEpoch;
  portEXIT_CRITICAL(&gPosMux);

  // Upsert the peer, then decide (rate-limited) whether to emit a live NodeInfo.
  bool emit = false;
  Peer snap{};
  portENTER_CRITICAL(&gDbMux);
  Peer *p = peerSlot(node_id);
  if (p->nodeId != node_id || !p->used) {  // (re)claimed slot — reset emit gate
    *p = Peer{};
    p->nodeId = node_id;
    p->used = true;
  }
  if (pos_valid) {
    p->latI = lat_i;
    p->lonI = lon_i;
    p->sats = sats_in_view;
    p->posValid = true;
  }
  p->snr = snr;
  p->lastHeardEpoch = epoch;
  p->chanHash = chan_hash;
  unsigned long now = millis();
  // gConfigDone: like the self path, hold live peer emits until after this
  // connection's want_config handshake so we never queue a NodeInfo ahead of
  // my_info (the handshake already dumps every known peer). No-op when no client.
  if (gBleConnected && gConfigDone &&
      (p->lastEmitMs == 0 || now - p->lastEmitMs >= kPeerEmitMinMs)) {
    p->lastEmitMs = now;
    snap = *p;
    emit = true;
  }
  portEXIT_CRITICAL(&gDbMux);

  if (emit) {
    meshtastic_FromRadio fr;
    uint8_t chIdx = channelIndexForHash(snap.chanHash);
    buildNodeInfoFrame(&fr, snap.nodeId, snap.posValid, snap.latI, snap.lonI,
                       snap.sats, snap.snr, snap.lastHeardEpoch, chIdx);
    enqueueFromRadio(&fr);
    // #2: also the stock-style MeshPacket{POSITION} when we have a fix for this
    // peer (supplementary; the NodeInfo above is what the gateway app parses).
    if (snap.posValid &&
        buildPositionPacketFrame(&fr, snap.nodeId, snap.latI, snap.lonI,
                                 snap.sats, snap.snr, snap.lastHeardEpoch, chIdx))
      enqueueFromRadio(&fr);
    Serial.printf("# BLE peer %08X -> NodeInfo%s (live)\n", (unsigned)node_id,
                  snap.posValid ? " + Position" : "");
  }
}
