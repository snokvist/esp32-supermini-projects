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
#define WAYMESH_FW_VERSION "2.6.4.waymesh"
#define WAYMESH_HW_MODEL   255u  // HardwareModel.PRIVATE_HW — non-Meshtastic hardware
#define WAYMESH_ROLE       0u    // Config.DeviceConfig.Role.CLIENT
#define MESH_LOC_INTERNAL  2u    // Position.LocSource.LOC_INTERNAL

static_assert(sizeof(WAYMESH_FW_VERSION) <= 24,
              "WAYMESH_FW_VERSION must fit DeviceMetadata.firmware_version[24]");

static NimBLECharacteristic *gFromRadio = nullptr;
static NimBLECharacteristic *gFromNum = nullptr;

static volatile bool gBleConnected = false;
static uint32_t gFromNumVal = 0;

// --- self identity / latest GPS fix + epoch (fed from main.cpp) --------------
static uint32_t gNodeId = 0;
static portMUX_TYPE gPosMux = portMUX_INITIALIZER_UNLOCKED;  // guards the below
static int32_t gLatI = 0, gLonI = 0;
static uint32_t gSats = 0;
static bool gPosValid = false;
static uint32_t gEpoch = 0;  // current UTC epoch seconds (0 = unknown)

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
                               uint32_t sats, float snr, uint32_t lastHeard) {
  *fr = meshtastic_FromRadio_init_zero;
  fr->which_payload_variant = meshtastic_FromRadio_node_info_tag;
  meshtastic_NodeInfo &ni = fr->payload_variant.node_info;
  ni.num = nodeId;
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

  // 3) Self NodeInfo (with the live GPS position, if we have a fix).
  int32_t latI, lonI;
  uint32_t sats, epoch;
  bool posValid;
  portENTER_CRITICAL(&gPosMux);
  latI = gLatI; lonI = gLonI; sats = gSats; posValid = gPosValid; epoch = gEpoch;
  portEXIT_CRITICAL(&gPosMux);
  buildNodeInfoFrame(&fr, gNodeId, posValid, latI, lonI, sats, 0.0f, epoch);
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
                       p.lastHeardEpoch);
    enqueueFromRadio(&fr);
    peers++;
  }

  // 5) config_complete_id — echoes the nonce; the client treats this as "done".
  fr = meshtastic_FromRadio_init_zero;
  fr.which_payload_variant = meshtastic_FromRadio_config_complete_id_tag;
  fr.payload_variant.config_complete_id = wantConfigId;
  enqueueFromRadio(&fr);

  Serial.printf("# BLE handshake queued (self + %d peer(s)) for "
                "want_config_id=%u\n",
                peers, (unsigned)wantConfigId);
}

// --- GATT callbacks ----------------------------------------------------------
class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *) override {
    gBleConnected = true;
    Serial.println("# BLE connect");
  }
  void onDisconnect(NimBLEServer *) override {
    gBleConnected = false;
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

void bleGattSetPosition(int32_t lat_i, int32_t lon_i, uint32_t sats_in_view,
                        bool valid) {
  portENTER_CRITICAL(&gPosMux);
  gLatI = lat_i;
  gLonI = lon_i;
  gSats = sats_in_view;
  gPosValid = valid;
  portEXIT_CRITICAL(&gPosMux);
}

void bleGattSetTime(uint32_t epoch) {
  portENTER_CRITICAL(&gPosMux);
  gEpoch = epoch;
  portEXIT_CRITICAL(&gPosMux);
}

void bleGattOnPeer(uint32_t node_id, int32_t lat_i, int32_t lon_i,
                   uint32_t sats_in_view, bool pos_valid, float snr) {
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
  unsigned long now = millis();
  if (gBleConnected && (p->lastEmitMs == 0 || now - p->lastEmitMs >= kPeerEmitMinMs)) {
    p->lastEmitMs = now;
    snap = *p;
    emit = true;
  }
  portEXIT_CRITICAL(&gDbMux);

  if (emit) {
    meshtastic_FromRadio fr;
    buildNodeInfoFrame(&fr, snap.nodeId, snap.posValid, snap.latI, snap.lonI,
                       snap.sats, snap.snr, snap.lastHeardEpoch);
    enqueueFromRadio(&fr);
    Serial.printf("# BLE peer %08X -> NodeInfo (live)\n", (unsigned)node_id);
  }
}
