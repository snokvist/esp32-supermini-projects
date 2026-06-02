// =============================================================================
// waymesh-8285 — Phase H PoC #0 firmware (ESP8285 + Semtech SX1280, 2.4 GHz)
// Target: BayckRC 7PWM class (ELRS "Generic 2400 PWMP7").
//
// PoC #0 is THE GATE for the heterogeneous mesh: prove the SX1280 here talks
// on-air to the LR1121 on the Tier-1 XR2 (projects/waymesh-node). It mirrors the
// XR2's Phase 0 loopback EXACTLY — same beacon bytes, same radio params — so:
//   - the XR2 RXes this node's beacons and (via the BLE gateway) shows it as a
//     peer in an unmodified Meshtastic app;
//   - this node RXes the XR2's beacons and reports a PDR over serial.
// Success criterion: a third node appears in `meshtastic --ble --info`, and both
// nodes log nonzero rx= with sane RSSI/SNR.
//
// Managed-flood RELAY (Tier 2/3) is compiled in when WAYMESH_RELAY is set (the
// bayck_7pwm env): seen-set dedup + SNR-delay + overhear suppression, verbatim
// rebroadcast, no hop-limit (see board_config.h + docs/hybrid-mesh/05). With
// WAYMESH_RELAY unset this file is the pure PoC #0 interop firmware.
//
// STILL NOT here (later, Tier 2/3): GPS over remapped PWM-pin UART, PWM servo
// outputs. PoC #0 / the relay primitive are pure radio.
//
// Bring-up notes:
//   - begin() prints its return code. -707 (SPI_CMD_TIMEOUT) => BUSY pin wrong;
//     see board_config.h (try RADIOLIB_NC for BUSY/RST = PWMP7 wiring).
//   - If begin() is OK but rx stays 0 with the XR2 beaconing nearby, the on-air
//     sync word / CRC / interleave differs across the chip families — sweep
//     LORA_SYNC_WORD first (that's the documented interop trap).
// =============================================================================

#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>

#include "board_config.h"

// ---- Build composition ------------------------------------------------------
// This firmware is v2/auth-only: the v2 wire codec (wm_beacon) + the wm_config
// store (channels/relay/packetId + EEPROM backend) are ALWAYS compiled. The
// legacy v0/v1 packed-struct PoC path has been removed (it will not return).
// The one optional feature is the WiFi provisioning portal (WAYMESH_WIFI_CONFIG,
// doc 13 §8.4); without it a node just runs on the default LongFast open group.
#if !defined(WAYMESH_WIFI_CONFIG)
#define WAYMESH_WIFI_CONFIG 0
#endif
// Informational only (the firmware is tier-agnostic); surfaced in the boot banner
// so it's no longer an orphan build flag. Guarded so an env that omits it builds.
#if !defined(WAYMESH_TIER)
#define WAYMESH_TIER 0
#endif

#if WAYMESH_GPS
#include <TinyGPSPlus.h>  // vendor-neutral NMEA parser (fed from UART0)
#endif

#include "waymesh_config.h"     // portable channel/relay/packetId store (doc 13 §8)
#include "wm_store_eeprom.h"    // ESP8266 EEPROM backend for wm_store_t
#include "wm_beacon.h"          // v2/auth wire codec + RX/relay decisions (§3/§6)
#if WAYMESH_WIFI_CONFIG
#include <ESP8266WiFi.h>        // STA MAC for a full 32-bit nodeId (see setup())
#include "wm_portal.h"          // SoftAP + captive web form (doc 13 §8.4)
#endif

// ---- Radio ------------------------------------------------------------------
// SX128x Module pin order: (NSS/CS, IRQ/DIO1, RESET, BUSY) — same order as the
// LR11x0 on the XR2, just a different RadioLib class.
static SX1280 gRadio = new Module(PIN_LORA_NSS, PIN_LORA_DIO1, PIN_LORA_RST, PIN_LORA_BUSY);

// ---- Relay MessageID width + verbatim-rebroadcast buffer --------------------
// The managed-flood relay dedups on the 32-bit packetId and re-floods frames up
// to a full v2 TEXT beacon verbatim (same srcId/packetId, so the gateway still
// attributes presence to the true originator).
typedef uint32_t wm_msgid_t;
#define WM_RELAY_RAW_MAX WM_BEACON_FRAME_MAX

// ---- State ------------------------------------------------------------------
static uint32_t gNodeId = 0;
static uint16_t gTxSeq = 0;
static uint32_t gRxCount = 0;
static uint32_t gRxBadCount = 0;
static uint32_t gLastRxId = 0;        // last accepted packetId (single-peer PDR)
static bool gHaveLastRxId = false;
static uint32_t gRxSeqGaps = 0;

static unsigned long gLastBeaconMs = 0;
static unsigned long gLastStatusMs = 0;
static bool gLedOn = false;

static volatile bool gRxFlag = false;
static bool gRadioOk = false;

// ---- Config store (channels/relay/packetId; portal + v2/auth share it) ------
static wm_config_t gCfg;
static wm_store_t gStore;
// ---- WiFi config portal state (-DWAYMESH_WIFI_CONFIG) -----------------------
#if WAYMESH_WIFI_CONFIG
static bool gConfigMode = false;
static unsigned long gBtnDownMs = 0;    // GPIO0 press start (0 = released)
static unsigned long gPortalBlinkMs = 0;
#endif

// ---- GPS / UART0 time-share -------------------------------------------------
#if WAYMESH_GPS
static TinyGPSPlus gGps;
enum GpsSerMode { GPS_SER_DEBUG, GPS_SER_PROBE, GPS_SER_GPS };
static GpsSerMode gSerMode = GPS_SER_DEBUG;
static unsigned long gProbeStartMs = 0;
static uint32_t gProbeBaseline = 0;  // gGps.passedChecksum() when probing began
#if WAYMESH_GPS_DEBUG
static unsigned long gLastGpsDbgMs = 0;
#endif
#endif

// ISR: a packet arrived (or TX finished) on DIO1. ESP8266 ISRs must be in IRAM.
IRAM_ATTR static void onDio1() { gRxFlag = true; }

// ---- Helpers ----------------------------------------------------------------
static void ledWrite(bool on) {
#if LED_ACTIVE_LOW
  digitalWrite(PIN_LED, on ? LOW : HIGH);
#else
  digitalWrite(PIN_LED, on ? HIGH : LOW);
#endif
}

// Structured CSV line — same schema as waymesh-node so the same serial tooling
// works. role = "poc0". No GPS on this build, so lat/lon are always blank.
// ts_ms,nodeId,role,event,plane,srcId,seq,rssi,snr,lat,lon,extra
static void logEvent(const char *event, const char *plane, uint32_t srcId,
                     long seq, float rssi, float snr, const char *extra) {
#if WAYMESH_GPS && !WAYMESH_GPS_DEBUG
  // Once locked to GPS, UART0 belongs to the GPS — stay off the wire. (The
  // _DEBUG build keeps logging so bring-up is observable without a sky fix.)
  if (gSerMode == GPS_SER_GPS) return;
#endif
  Serial.printf("%lu,%08X,poc0,%s,%s,", millis(), gNodeId, event, plane);
  if (srcId)        Serial.printf("%08X,", srcId); else Serial.print(",");
  if (seq >= 0)     Serial.printf("%ld,", seq);    else Serial.print(",");
  if (!isnan(rssi)) Serial.printf("%.1f,", rssi);  else Serial.print(",");
  if (!isnan(snr))  Serial.printf("%.1f,", snr);   else Serial.print(",");
  Serial.print(",,");  // lat,lon (no GPS on PoC #0)
  Serial.println(extra ? extra : "");
}

static void startRx() {
  int16_t st = gRadio.startReceive();
  if (st != RADIOLIB_ERR_NONE) {
    char buf[24];
    snprintf(buf, sizeof(buf), "startRx_err=%d", st);
    logEvent("error", "lrp", 0, -1, NAN, NAN, buf);
  }
}

// ---- Managed-flood relay (Tier 2/3) -----------------------------------------
#if WAYMESH_RELAY
// Dedup seen-set: recent MessageIDs = (srcId, packetId/seq) + the time first
// seen. Linear scan (RELAY_SEEN_SET_SIZE entries). Eviction is time-aware: a
// free slot first, then an expired (> TTL) slot, else the OLDEST entry — so a
// freshly-seen MessageID is never evicted while older ones remain, which a blind
// FIFO could do (re-relaying a still-in-flight flood). Under relay-all this set
// also holds foreign-group MessageIDs (we relay + must dedup them), so it is
// sized for the full offered load, not just the home group.
struct SeenEntry { uint32_t srcId; wm_msgid_t id; unsigned long ms; bool valid; };
static SeenEntry gSeen[RELAY_SEEN_SET_SIZE];

// Rebroadcasts waiting out their SNR-proportional delay (so overhear suppression
// can still cancel them). raw[] holds the VERBATIM bytes to re-send.
struct PendingRelay {
  bool active;
  uint32_t srcId;
  wm_msgid_t id;
  unsigned long dueMs;
  uint8_t len;
  uint8_t raw[WM_RELAY_RAW_MAX];
};
static PendingRelay gPending[RELAY_PENDING_SLOTS];

static uint32_t gRelayTx = 0;          // beacons we rebroadcast
static uint32_t gRelaySuppressed = 0;  // pending rebroadcasts cancelled (overhear)
static uint32_t gRelayDropFull = 0;    // new msgs dropped (no free pending slot)

static bool seenContains(uint32_t srcId, wm_msgid_t id) {
  const unsigned long now = millis();
  for (uint8_t i = 0; i < RELAY_SEEN_SET_SIZE; i++)
    if (gSeen[i].valid && gSeen[i].srcId == srcId && gSeen[i].id == id)
      // Stale (older than the TTL) -> treat as not-seen so a much-later genuine
      // repeat is relayable; the flood it belonged to is long dead.
      return (unsigned long)(now - gSeen[i].ms) < RELAY_SEEN_TTL_MS;
  return false;
}
static void seenAdd(uint32_t srcId, wm_msgid_t id) {
  const unsigned long now = millis();
  uint8_t victim = 0;
  unsigned long oldest = 0;  // largest elapsed time wins as the fallback victim
  for (uint8_t i = 0; i < RELAY_SEEN_SET_SIZE; i++) {
    if (!gSeen[i].valid) { victim = i; break; }            // free slot
    unsigned long elapsed = now - gSeen[i].ms;
    if (elapsed >= RELAY_SEEN_TTL_MS) { victim = i; break; } // expired slot
    if (elapsed >= oldest) { oldest = elapsed; victim = i; } // else evict oldest
  }
  gSeen[victim].srcId = srcId;
  gSeen[victim].id = id;
  gSeen[victim].ms = now;
  gSeen[victim].valid = true;
}

// Overhear suppression: drop any pending rebroadcast of this MessageID because
// someone else already put it on the air. Returns true if one was cancelled.
static bool relaySuppress(uint32_t srcId, wm_msgid_t id) {
  for (uint8_t i = 0; i < RELAY_PENDING_SLOTS; i++)
    if (gPending[i].active && gPending[i].srcId == srcId && gPending[i].id == id) {
      gPending[i].active = false;
      return true;
    }
  return false;
}

// Queue a verbatim rebroadcast after an SNR-proportional delay (+ jitter): a
// weaker-SNR receiver waits longer, so the best-placed relay transmits first and
// suppresses the rest. Drops on a full queue (the seen-set still recorded it).
static void relaySchedule(const uint8_t *raw, uint8_t len, uint32_t srcId,
                          wm_msgid_t id, float snr) {
  for (uint8_t i = 0; i < RELAY_PENDING_SLOTS; i++) {
    if (!gPending[i].active) {
      float s = isnan(snr) ? RELAY_SNR_REF_DB : snr;
      long over = (long)(RELAY_SNR_REF_DB - s);   // dB below reference, if any
      long d = RELAY_DELAY_BASE_MS + (over > 0 ? over * RELAY_DELAY_PER_DB_MS : 0);
      if (d > RELAY_DELAY_MAX_MS) d = RELAY_DELAY_MAX_MS;
      d += (long)random(RELAY_DELAY_JITTER_MS + 1);
      gPending[i].active = true;
      gPending[i].srcId = srcId;
      gPending[i].id = id;
      gPending[i].len = len;
      memcpy(gPending[i].raw, raw, len);
      gPending[i].dueMs = millis() + (unsigned long)d;
      return;
    }
  }
  gRelayDropFull++;
}

// Transmit any rebroadcasts whose delay has elapsed (called from loop()).
static void relayService() {
  const unsigned long now = millis();
  for (uint8_t i = 0; i < RELAY_PENDING_SLOTS; i++) {
    if (!gPending[i].active) continue;
    if ((long)(now - gPending[i].dueMs) < 0) continue;
    int16_t st = gRadio.transmit(gPending[i].raw, gPending[i].len);
    if (st == RADIOLIB_ERR_NONE) {
      gRelayTx++;
      logEvent("relay", "lrp", gPending[i].srcId, (long)gPending[i].id, NAN, NAN, "fwd");
    } else {
      char buf[20];
      snprintf(buf, sizeof(buf), "relay_err=%d", st);
      logEvent("error", "lrp", 0, -1, NAN, NAN, buf);
    }
    gPending[i].active = false;
    // transmit() left us in standby and DIO1 fired TxDone; drop that phantom and
    // re-arm RX (same fix as sendBeacon()).
    gRxFlag = false;
    startRx();
  }
}
#endif  // WAYMESH_RELAY

// ---- v2/auth origination + RX (doc 13 §3/§5/§6) -----------------------------
// Mirrors waymesh-node/src/main.cpp: TX a clear-header v2 beacon on our home
// chanHash (AES-CCM-sealed POS when the channel is keyed and a fix is fresh), and
// on RX apply the §6 acceptance steps alongside the keyless managed-flood relay.
static void sendBeacon() {
  const wm_channel_t *home = wm_config_home(&gCfg);
  uint8_t chanHash = (home && home->hash >= 0) ? (uint8_t)home->hash
                                               : WM_OPEN_GROUP_HASH;
  uint32_t pid;
  if (wm_config_next_packet_id(&gCfg, &pid) != 0) {
    // Ceiling persist failed — can't guarantee a unique CCM nonce across a
    // reboot (§8). Skip this beacon rather than risk reusing a packetId.
    logEvent("error", "lrp", gNodeId, -1, NAN, NAN, "pktid_persist_fail");
    startRx();
    return;
  }

  bool posValid = false;
  int32_t lat = 0, lon = 0;
  uint8_t sats = 0;
#if WAYMESH_GPS
  if (gGps.location.isValid() && gGps.location.age() < GPS_FIX_MAX_AGE_MS) {
    lat = (int32_t)lround(gGps.location.lat() * 1e7);
    lon = (int32_t)lround(gGps.location.lng() * 1e7);
    sats = gGps.satellites.isValid() ? (uint8_t)gGps.satellites.value() : 0;
    posValid = true;
  }
#endif

  // §5: the 12-byte header is always clear (relay/dedup read it keyless); a POS
  // payload on a keyed channel is AES-CCM sealed + a 4-byte MIC. Presence-only, a
  // keyless channel, or a seal failure -> clear beacon.
  uint8_t out[WM_BEACON_V2_MAX];
  size_t n = 0;
  bool enc = false;
  if (posValid && home && home->key_len > 0) {
    n = wm_beacon_build_v2_enc(out, chanHash, gNodeId, pid, lat, lon, sats,
                               home->expanded_key, home->key_len);
    enc = (n > 0);
  }
  if (!enc)
    n = wm_beacon_build_v2_clear(out, chanHash, gNodeId, pid, posValid,
                                 lat, lon, sats);

  int16_t st = gRadio.transmit(out, n);
  if (st == RADIOLIB_ERR_NONE) {
    char ex[24];
    snprintf(ex, sizeof(ex), "beacon ch=%u%s", (unsigned)chanHash, enc ? " enc" : "");
    logEvent("tx", "lrp", gNodeId, (long)(pid & 0xFFFF), NAN, NAN, ex);
    gTxSeq++;
  } else {
    char buf[20];
    snprintf(buf, sizeof(buf), "tx_err=%d", st);
    logEvent("error", "lrp", 0, -1, NAN, NAN, buf);
  }
  // transmit() leaves the radio in standby; the shared DIO1 ISR also fires on
  // TxDone — drop the phantom flag before re-arming RX (same fix as the XR2 fw).
  gRxFlag = false;
  startRx();
}

static void handleRx() {
  // Buffer fits the largest v2 frame (a TEXT beacon: header + text + MIC).
  uint8_t raw[WM_BEACON_FRAME_MAX];
  memset(raw, 0, sizeof(raw));
  size_t plen = gRadio.getPacketLength();
  if (plen > sizeof(raw)) plen = sizeof(raw);
  int16_t st = gRadio.readData(raw, plen);
  if (st != RADIOLIB_ERR_NONE) {
    gRxBadCount++;
    char buf[20];
    snprintf(buf, sizeof(buf), "rx_err=%d", st);
    logEvent("error", "lrp", 0, -1, NAN, NAN, buf);
    startRx();
    return;
  }

  wm_beacon_t b;
  if (wm_beacon_parse(raw, plen, &b) != 0) {
    startRx();  // not a recognized Waymesh beacon — ignore quietly (magic gate)
    return;
  }

  float rssi = gRadio.getRSSI();
  float snr = gRadio.getSNR();

  // §6 step 2: never relay or count our own beacons.
  wm_rx_decision_t d = wm_beacon_accept(&b, gNodeId, &gCfg);
  if (d == WM_RX_DROP_SELF) { startRx(); return; }

#if WAYMESH_RELAY
  // §6 relay: dedup on the wider (srcId, packetId) MessageID across ALL groups —
  // a relay-all relay carries foreign traffic blindly, so it must dedup it too.
  if (seenContains(b.src_id, (wm_msgid_t)b.packet_id)) {
    if (relaySuppress(b.src_id, (wm_msgid_t)b.packet_id)) gRelaySuppressed++;
    startRx();
    return;
  }
  seenAdd(b.src_id, (wm_msgid_t)b.packet_id);
  // Keyless verbatim re-flood, governed by the relay policy on the CLEAR chanHash
  // header — no key needed to filter or forward (the whole point of §6).
  if (wm_beacon_should_relay(&b, &gCfg))
    relaySchedule(raw, (uint8_t)plen, b.src_id, (wm_msgid_t)b.packet_id, snr);
#endif

  // §6 step 3: acceptance group filter. The relay above already carried a foreign
  // group if relay-all; we just don't count/open it.
  if (d == WM_RX_DROP_FOREIGN_GROUP) {
    char ex[24];
    snprintf(ex, sizeof(ex), "foreign_group ch=%u", (unsigned)b.chan_hash);
    logEvent("drop", "lrp", b.src_id, (long)(b.packet_id & 0xFFFF), rssi, snr, ex);
    startRx();
    return;
  }

  // §6 step 5: AEAD-open an encrypted member-group beacon; a bad MIC / wrong key
  // -> DROP with no plaintext exposed. The 8285 relay has no gateway, so an opened
  // frame is verified + counted only (nothing is forwarded to an app).
  uint8_t ptext[WM_BEACON_TEXT_MAX];
  size_t ptext_len = 0;
  if (b.flags & WM_BFLAG_ENCRYPTED) {
    const wm_channel_t *ch = wm_config_channel_by_hash(&gCfg, b.chan_hash);
    if (!ch || wm_beacon_open(&b, ch->expanded_key, ch->key_len, ptext,
                              sizeof(ptext), &ptext_len) != 0) {
      char ex[24];
      snprintf(ex, sizeof(ex), "bad_mic ch=%u", (unsigned)b.chan_hash);
      logEvent("drop", "lrp", b.src_id, (long)(b.packet_id & 0xFFFF), rssi, snr, ex);
      startRx();
      return;
    }
  }

  // §6 step 6: accept a beacon from a group we belong to.
  gRxCount++;
  if (gHaveLastRxId) {
    // Forward gap on a single peer's packetId; ignore reserve-ahead block skips /
    // reboots / reordering (best-effort single-peer PDR, matches the XR2).
    int32_t delta = (int32_t)(b.packet_id - (gLastRxId + 1));
    if (delta > 0 && delta < (int32_t)WM_PKTID_BLOCK) gRxSeqGaps += (uint32_t)delta;
  }
  gLastRxId = b.packet_id;
  gHaveLastRxId = true;
  bool isText = (b.flags & WM_BFLAG_TEXT) && ptext_len > 0;
  char ex[24];
  snprintf(ex, sizeof(ex), "beacon ch=%u%s", (unsigned)b.chan_hash,
           isText ? " text" : "");
  logEvent("rx", "lrp", b.src_id, (long)(b.packet_id & 0xFFFF), rssi, snr, ex);
  startRx();
}


static void printStatus() {
  uint32_t expected = gRxCount + gRxSeqGaps;
  float pdr = expected ? (100.0f * (float)gRxCount / (float)expected) : 0.0f;
  char extra[120];
#if WAYMESH_RELAY
  snprintf(extra, sizeof(extra),
           "tx=%u rx=%u gaps=%u badcrc=%u pdr=%.1f%% relay=%u supp=%u qfull=%u",
           (unsigned)gTxSeq, (unsigned)gRxCount, (unsigned)gRxSeqGaps,
           (unsigned)gRxBadCount, pdr,
           (unsigned)gRelayTx, (unsigned)gRelaySuppressed, (unsigned)gRelayDropFull);
#else
  snprintf(extra, sizeof(extra), "tx=%u rx=%u gaps=%u badcrc=%u pdr=%.1f%%",
           (unsigned)gTxSeq, (unsigned)gRxCount, (unsigned)gRxSeqGaps,
           (unsigned)gRxBadCount, pdr);
#endif
  logEvent("status", "node", 0, -1, NAN, NAN, extra);
}

#if WAYMESH_GPS
// Drain UART0 RX into the NMEA parser.
static void gpsFeed() {
  while (Serial.available()) gGps.encode((char)Serial.read());
}

// UART0 time-share state machine. DEBUG (console) -> PROBE (listen for NMEA after
// the grace window) -> GPS (lock + parse) or back to DEBUG if nothing is heard.
static void gpsService() {
  // Latch once the boot grace has elapsed. Using a flag (not `now >= GRACE`
  // every time) keeps the no-GPS DEBUG<->PROBE re-probe cadence intact AND is
  // millis()-rollover-safe: the absolute compare only runs in the first ~25 s
  // after boot, well before the 49.7-day wrap.
  static bool graceDone = false;
  const unsigned long now = millis();
  switch (gSerMode) {
    case GPS_SER_DEBUG:
      if (graceDone || now >= GPS_GRACE_MS) {
        graceDone = true;
        gSerMode = GPS_SER_PROBE;
        gProbeStartMs = now;
        gProbeBaseline = gGps.passedChecksum();
        logEvent("gps_probe", "node", 0, -1, NAN, NAN, "listening for NMEA");
      }
      break;
    case GPS_SER_PROBE:
      gpsFeed();
      if (gGps.passedChecksum() - gProbeBaseline >= GPS_PROBE_MIN_SENTENCES) {
        // Last CSV line on UART0 in the production build (then it goes silent).
        logEvent("gps_lock", "node", 0, -1, NAN, NAN, "NMEA detected -> GPS mode");
        gSerMode = GPS_SER_GPS;
#if GPS_BAUD != 115200
        Serial.flush();
        Serial.begin(GPS_BAUD);  // only if the GPS baud differs from debug
#endif
      } else if (now - gProbeStartMs >= GPS_PROBE_MS) {
        gSerMode = GPS_SER_DEBUG;
        logEvent("gps_none", "node", 0, -1, NAN, NAN, "no GPS -> debug console");
      }
      break;
    case GPS_SER_GPS:
      gpsFeed();
      break;
  }
}
#endif  // WAYMESH_GPS

// ---- WiFi config portal (-DWAYMESH_WIFI_CONFIG) -----------------------------
#if WAYMESH_WIFI_CONFIG
// Suspend LoRa (both radios are 2.4 GHz) and bring the SoftAP + web form up.
static void enterConfigMode() {
  Serial.println("# portal: entering config mode (suspending LoRa)");
  if (gRadioOk) gRadio.sleep();
  ledWrite(true);
  if (!wmPortalBegin(&gCfg, gNodeId))
    Serial.println("# portal: softAP start FAILED");
  gConfigMode = true;
  gPortalBlinkMs = millis();
}

// Watch for the config-mode trigger: a serial 'c' (bench, button-less) or a
// ~5 s GPIO0 long-press. Called from loop() only while NOT already in config mode.
static void checkPortalTrigger() {
  // Serial 'c' is the button-less bench fallback. On a GPS build UART0 is
  // TIME-SHARED with NMEA — gpsService() drains it in PROBE/GPS modes — so read it
  // for the trigger ONLY while the console still owns the line (GPS in DEBUG: before
  // NMEA lock, or after a no-GPS revert). Otherwise we would steal the GPS's bytes.
  // The GPIO0 long-press always works regardless of who owns UART0.
#if WAYMESH_GPS
  const bool consoleOwnsUart = (gSerMode == GPS_SER_DEBUG);
#else
  const bool consoleOwnsUart = true;
#endif
  if (consoleOwnsUart && Serial.available()) {
    int c = Serial.read();
    if (c == 'c' || c == 'C') { enterConfigMode(); return; }
  }
  const bool pressed = (digitalRead(PIN_PORTAL_BTN) == LOW);
  const unsigned long now = millis();
  if (pressed) {
    if (gBtnDownMs == 0) gBtnDownMs = now;
    else if (now - gBtnDownMs >= WM_PORTAL_BTN_MS) { gBtnDownMs = 0; enterConfigMode(); }
  } else {
    gBtnDownMs = 0;
  }
}

// Pump the portal; fast-blink the LED to signal "AP up". On Save or idle timeout
// the node reboots — the cleanest way to resume LoRa after WiFi held the 2.4 GHz
// front-end (avoids a half-woken SX1280). Returns nothing; reboots in place.
static void serviceConfigMode() {
  const unsigned long now = millis();
  if (now - gPortalBlinkMs >= 150) { gPortalBlinkMs = now; gLedOn = !gLedOn; ledWrite(gLedOn); }
  if (!wmPortalService()) {
    Serial.println(wmPortalRebootRequested() ? "# portal: saved -> rebooting"
                                             : "# portal: idle timeout -> rebooting");
    wmPortalEnd();
    delay(50);
    ESP.restart();
  }
}
#endif  // WAYMESH_WIFI_CONFIG

// ---- Setup / loop -----------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(PIN_LED, OUTPUT);
  ledWrite(false);

  // srcId is a CCM nonce input, so it should use the full per-device MAC entropy
  // and match how the XR2 derives it: (uint32_t)(efuseMac & 0xFFFFFFFF) = the low
  // 32 bits of the 6-byte MAC. getChipId() is only the low 24 bits (zero-extended),
  // which both wastes a byte and gives the two tiers different derivations. Read
  // the full STA MAC and take its low 32 bits so both tiers share one collision
  // domain (no extra entropy beyond the 24-bit NIC part, but a consistent layout).
#if WAYMESH_WIFI_CONFIG
  uint8_t mac[6] = {0};
  WiFi.macAddress(mac);  // reads the chip MAC; no WiFi.begin() needed
  gNodeId = ((uint32_t)mac[2] << 24) | ((uint32_t)mac[3] << 16) |
            ((uint32_t)mac[4] << 8) | (uint32_t)mac[5];
  if (gNodeId == 0) gNodeId = ESP.getChipId();  // paranoia: MAC unreadable
#else
  gNodeId = ESP.getChipId();  // relay-only build has no WiFi linked: 24-bit id
#endif
#if WAYMESH_RELAY
  // Per-node seed so the rebroadcast jitter differs across relays (tie-break).
  randomSeed(gNodeId ^ micros());
#endif

  Serial.println();
  Serial.printf("# waymesh-8285 (" WAYMESH_BOARD_NAME ", Tier %d: ESP8285 + SX1280, 2.4 GHz LoRa)\n",
                WAYMESH_TIER);
  Serial.printf("# nodeId=%08X freq=%.1fMHz bw=%.1fkHz sf=%d cr=4/%d pwr=%ddBm sync=0x%02X\n",
                gNodeId, (double)LORA_FREQ_MHZ, (double)LORA_BW_KHZ, LORA_SF,
                LORA_CR, LORA_POWER_DBM, (unsigned)LORA_SYNC_WORD);
  Serial.println("# MUST match the XR2 LR1121 config for interop (Phase H PoC #0).");
#if WAYMESH_GPS
  Serial.printf("# GPS: UART0 is the debug console for %lus, then auto-detects NMEA "
                "@%d baud (vendor-neutral; provision u-blox via tools/gps_provision.py).\n",
                (unsigned long)(GPS_GRACE_MS / 1000), (int)GPS_BAUD);
#if WAYMESH_GPS_DEBUG
  Serial.println("# GPS_DEBUG build: CSV + a 1 Hz gps line keep printing even in GPS mode.");
#endif
#endif
#if WAYMESH_RELAY
  Serial.printf("# RELAY MODE: managed flood (seen-set=%d, SNR-delay %d..%dms, "
                "overhear suppress); verbatim re-flood, NO hop-limit.\n",
                RELAY_SEEN_SET_SIZE, RELAY_DELAY_BASE_MS, RELAY_DELAY_MAX_MS);
#endif
#if defined(PIN_LORA_RXEN) && defined(PIN_LORA_TXEN)
  Serial.printf("# PA front-end: external PA/LNA gated via RXEN=GPIO%d / "
                "TXEN=GPIO%d (RadioLib RF switch).\n",
                PIN_LORA_RXEN, PIN_LORA_TXEN);
#endif
  Serial.println("ts_ms,nodeId,role,event,plane,srcId,seq,rssi,snr,lat,lon,extra");

  // Load the channel/relay/packetId store (EEPROM), seeding the LongFast/psk=01
  // default (chanHash 8) on fresh flash. The WiFi portal writes this same store;
  // the v2/auth path TXes/filters on its home chanHash + reboot-safe packetId.
  wm_store_eeprom_begin(&gStore);
  wm_config_init(&gCfg, &gStore);
  {
    const wm_channel_t *home = wm_config_home(&gCfg);
    Serial.printf("# config: home '%s' idx=%d chanHash=%d psk_len=%d relay=%s\n",
                  home ? home->name : "(none)", home ? home->index : 0,
                  home ? home->hash : -1, home ? home->psk_len : 0,
                  gCfg.relay_policy == WM_RELAY_KNOWN ? "known" : "all");
  }
#if WAYMESH_WIFI_CONFIG
  pinMode(PIN_PORTAL_BTN, INPUT_PULLUP);
  Serial.printf("# portal: GPIO%d long-press ~%lus or serial 'c' -> WiFi setup\n",
                PIN_PORTAL_BTN, (unsigned long)(WM_PORTAL_BTN_MS / 1000));
#endif

  // ESP8266 hardware SPI is on fixed pins (SCK14/MISO12/MOSI13) — no args.
  SPI.begin();

  // SX128x LoRa begin(): freq, bw, sf, cr, power, preamble. NOTE: unlike the
  // LR11x0/SX126x begin(), SX128x takes NO sync-word and NO tcxo argument.
  int16_t st = gRadio.begin(LORA_FREQ_MHZ, LORA_BW_KHZ, LORA_SF, LORA_CR,
                            LORA_POWER_DBM, LORA_PREAMBLE);
  if (st == RADIOLIB_ERR_NONE) {
    // Force the interop-critical settings explicitly (the LR1121 side relies on
    // these exact values; see board_config.h):
    //  - sync word: SX128x begin() didn't take it, so set it now.
    //  - coding rate with longInterleave=FALSE (the SX1280-only long-interleave
    //    variants are unreadable by the LR1121).
    int16_t sw = gRadio.setSyncWord(LORA_SYNC_WORD);
    int16_t cr = gRadio.setCodingRate(LORA_CR, false);
#if defined(PIN_LORA_RXEN) && defined(PIN_LORA_TXEN)
    // External PA/LNA front-end (BetaFPV Nano): hand RadioLib the RXEN/TXEN lines
    // so it powers the PA on TX and the LNA on RX automatically. MUST be set
    // before startRx() so the first receive enables the LNA (board_config.h).
    gRadio.setRfSwitchPins(PIN_LORA_RXEN, PIN_LORA_TXEN);
#endif
    char buf[56];
#if defined(PIN_LORA_RXEN) && defined(PIN_LORA_TXEN)
    snprintf(buf, sizeof(buf), "radio_ok sw=%d cr=%d pa=rxen%d/txen%d",
             sw, cr, PIN_LORA_RXEN, PIN_LORA_TXEN);
#else
    snprintf(buf, sizeof(buf), "radio_ok sw=%d cr=%d", sw, cr);
#endif
    gRadio.setPacketReceivedAction(onDio1);
    startRx();
    gRadioOk = true;
    logEvent("boot", "node", gNodeId, -1, NAN, NAN, buf);
  } else {
    char buf[28];
    snprintf(buf, sizeof(buf), "begin_err=%d", st);
    logEvent("error", "lrp", 0, -1, NAN, NAN, buf);
    Serial.println("# radio begin() failed — check SPI/BUSY/RST wiring "
                   "(board_config.h). Halting RF.");
  }
}

void loop() {
#if WAYMESH_WIFI_CONFIG
  if (gConfigMode) { serviceConfigMode(); return; }  // AP up: LoRa suspended
  checkPortalTrigger();                              // GPIO0 long-press / serial 'c'
  if (gConfigMode) return;  // just entered this iteration -> skip RF (stay asleep)
#endif
  if (gRadioOk) {
    if (gRxFlag) {
      gRxFlag = false;
      handleRx();
    }
#if WAYMESH_RELAY
    relayService();  // fire any rebroadcasts whose SNR-delay has elapsed
#endif
    const unsigned long now = millis();
    if (now - gLastBeaconMs >= BEACON_PERIOD_MS) {
      gLastBeaconMs = now;
      gLedOn = !gLedOn;
      ledWrite(gLedOn);
      sendBeacon();
    }
  }

#if WAYMESH_GPS
  gpsService();  // independent of the radio (runs even if begin() failed)
#if WAYMESH_GPS_DEBUG
  // Bring-up aid: once past the grace window, report parse state @1 Hz so GPS
  // wiring/lock can be verified on the bench without needing a sky fix.
  if (gSerMode != GPS_SER_DEBUG) {
    const unsigned long nowG = millis();
    if (nowG - gLastGpsDbgMs >= 1000) {
      gLastGpsDbgMs = nowG;
      double la = gGps.location.isValid() ? gGps.location.lat() : 0.0;
      double lo = gGps.location.isValid() ? gGps.location.lng() : 0.0;
      char gb[100];
      snprintf(gb, sizeof(gb),
               "mode=%d fix=%d sats=%lu chars=%lu sent=%lu lat=%.6f lon=%.6f",
               (int)gSerMode, gGps.location.isValid() ? 1 : 0,
               (unsigned long)(gGps.satellites.isValid() ? gGps.satellites.value() : 0),
               (unsigned long)gGps.charsProcessed(),
               (unsigned long)gGps.passedChecksum(), la, lo);
      logEvent("gps", "node", 0, -1, NAN, NAN, gb);
    }
  }
#endif
#endif

  const unsigned long nowStatus = millis();
  if (nowStatus - gLastStatusMs >= STATUS_PERIOD_MS) {
    gLastStatusMs = nowStatus;
    printStatus();
  }
}
