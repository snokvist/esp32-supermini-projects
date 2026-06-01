// =============================================================================
// waymesh-node — Phase 0 bring-up firmware
// Target: RadioMaster XR2 Nano (ESP32-C3 + Semtech LR1121, 2.4 GHz only)
//
// What this does (see docs/hybrid-mesh/09-poc-roadmap.md, Phase 0):
//   - Brings up the LR1121 over SPI as a 2.4 GHz LoRa radio (RadioLib).
//   - Symmetric loopback: each node periodically transmits a beacon and
//     listens the rest of the time, tracking received count / RSSI / SNR so two
//     nodes give a quick PDR read at a known distance.
//   - Reads a GNSS module on the spare UART (TinyGPSPlus) and logs fixes.
//   - Blinks the LED for liveness and prints structured CSV logs over serial
//     (USB-CDC by default; UART0 on the XR2 bench env — see platformio.ini).
//
// What this does NOT do: ESP-NOW (Phase 1), aggregation, the super-frame.
//
// Hardware-verified single-node on a RadioMaster XR2 Nano (2026-05-24): radio_ok,
// beacon TX, GPS 3D fix, badcrc=0. The pin map + TCXO in board_config.h are
// confirmed against the ELRS XR2 target (no TCXO; XOSC). Still untuned: the
// LR1121 RF-switch/DCDC routing is at RadioLib defaults (TX/RX path + range not
// yet validated), and the 2-node RX/PDR loopback needs a second XR2. begin()
// reports an error over serial if the SPI wiring is wrong.
// =============================================================================

#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include <TinyGPSPlus.h>

#include "board_config.h"
#include "ble_gatt.h"
#include "waymesh_config.h"
#include "wm_beacon.h"
#include "config_nvs.h"

// ---- Radio ------------------------------------------------------------------
// LR11x0 Module pin order: (NSS/CS, IRQ/DIO1, RESET, BUSY)
static LR1121 gRadio = new Module(PIN_LORA_NSS, PIN_LORA_DIO1, PIN_LORA_RST, PIN_LORA_BUSY);

// ---- GNSS -------------------------------------------------------------------
static TinyGPSPlus gGps;
static HardwareSerial gGnssSerial(1);

// ---- Beacon wire format -----------------------------------------------------
// The v2 beacon (chanHash group filter + 32-bit reboot-safe packetId) and its
// v0/v1 back-compat parsing live in the portable, host-tested waymesh_beacon
// lib (lib/waymesh_beacon, doc 13 §3). This node TXes a clear v2 beacon on its
// home channel and applies the group filter on RX (Phase 1 — positions clear;
// AES-CCM lands in Phase 2). Persistent channels + the crash-safe packetId
// counter come from waymesh_config over the C3 NVS backend (config_nvs).

// ---- State ------------------------------------------------------------------
static uint32_t gNodeId = 0;
static uint32_t gTxSeq = 0;       // beacons transmitted (status metric)
static wm_config_t gCfg;          // channels + relay policy + packetId counter
static uint32_t gRxCount = 0;
static uint32_t gRxBadCount = 0;
static uint32_t gLastRxId = 0;    // last accepted packetId (single-peer PDR)
static bool gHaveLastRxId = false;
static uint32_t gRxSeqGaps = 0;  // missed beacons inferred from packetId jumps
#if WAYMESH_RELAY_WITNESS
static uint32_t gRelayedBack = 0;  // our own beacons heard back via a relay (debug)
#endif

static unsigned long gLastBeaconMs = 0;
static unsigned long gLastStatusMs = 0;
static bool gLedOn = false;

static volatile bool gRxFlag = false;
static bool gRadioOk = false;

// ISR: a packet arrived (or TX finished) on DIO1.
IRAM_ATTR static void onDio1() { gRxFlag = true; }

// ---- Helpers ----------------------------------------------------------------
static void ledWrite(bool on) {
#if LED_ACTIVE_LOW
  digitalWrite(PIN_LED, on ? LOW : HIGH);
#else
  digitalWrite(PIN_LED, on ? HIGH : LOW);
#endif
}

// Structured CSV line. Schema mirrors docs/hybrid-mesh/10-experiments-and-metrics.md.
// ts_ms,nodeId,role,event,plane,srcId,seq,rssi,snr,lat,lon,extra
static void logEvent(const char *event, const char *plane, uint32_t srcId,
                     long seq, float rssi, float snr, const char *extra) {
  Serial.printf("%lu,%08X,phase0,%s,%s,", millis(), gNodeId, event, plane);
  if (srcId)        Serial.printf("%08X,", srcId); else Serial.print(",");
  if (seq >= 0)     Serial.printf("%ld,", seq);    else Serial.print(",");
  if (!isnan(rssi)) Serial.printf("%.1f,", rssi);  else Serial.print(",");
  if (!isnan(snr))  Serial.printf("%.1f,", snr);   else Serial.print(",");
  if (gGps.location.isValid()) {
    Serial.printf("%.6f,%.6f,", gGps.location.lat(), gGps.location.lng());
  } else {
    Serial.print(",,");
  }
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

static void sendBeacon() {
  // TX a clear v2 beacon on our home channel (Phase 1: group filter, no crypto).
  const wm_channel_t *home = wm_config_home(&gCfg);
  uint8_t chanHash = (home && home->hash >= 0) ? (uint8_t)home->hash
                                               : WM_OPEN_GROUP_HASH;
  uint32_t pid = wm_config_next_packet_id(&gCfg);

  bool posValid = gGps.location.isValid();
  int32_t lat = 0, lon = 0;
  uint8_t sats = 0;
  if (posValid) {
    lat = (int32_t)lround(gGps.location.lat() * 1e7);
    lon = (int32_t)lround(gGps.location.lng() * 1e7);
    sats = gGps.satellites.isValid() ? (uint8_t)gGps.satellites.value() : 0;
  }

  uint8_t out[WM_BEACON_V2_MAX];
  size_t n = wm_beacon_build_v2_clear(out, chanHash, gNodeId, pid, posValid,
                                      lat, lon, sats);

  int16_t st = gRadio.transmit(out, n);
  if (st == RADIOLIB_ERR_NONE) {
    char ex[20];
    snprintf(ex, sizeof(ex), "beacon ch=%u", (unsigned)chanHash);
    logEvent("tx", "lrp", gNodeId, (long)(pid & 0xFFFF), NAN, NAN, ex);
    gTxSeq++;
  } else {
    char buf[20];
    snprintf(buf, sizeof(buf), "tx_err=%d", st);
    logEvent("error", "lrp", 0, -1, NAN, NAN, buf);
  }
  // transmit() leaves the radio in standby; re-arm RX.
  // The shared DIO1 ISR also fires on TxDone, so it just set gRxFlag for our
  // own transmit. Clear it before re-arming, otherwise loop() treats the TxDone
  // as a phantom RX: handleRx() readData()s an empty FIFO, logs a bogus rx_err,
  // and inflates badcrc/PDR (the Phase 0 metric this firmware exists to collect).
  // Cleared *before* startRx() so a genuine packet arriving after re-arm still
  // sets the flag — the radio is in standby until startRx(), so no RX is lost.
  gRxFlag = false;
  startRx();
}

static void handleRx() {
  // Read exactly the received length; the codec parses v0/v1/v2 by version byte.
  uint8_t raw[WM_BEACON_V2_MAX];
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
    startRx(); // not a recognized Waymesh beacon — ignore quietly (magic gate)
    return;
  }

  float rssi = gRadio.getRSSI();
  float snr = gRadio.getSNR();

#if WAYMESH_RELAY_WITNESS
  // Debug (gated): a beacon carrying our OWN srcId can only be a relay's
  // rebroadcast — we clear gRxFlag after every TX, so we never hear ourselves.
  // Counting it turns "XR2 -> relay -> XR2" into a measurable relay PDR.
  if (b.src_id == gNodeId) {
    gRelayedBack++;
    char buf[28];
    snprintf(buf, sizeof(buf), "relayed_back=%lu", (unsigned long)gRelayedBack);
    logEvent("relay_witness", "lrp", b.src_id, (long)(b.packet_id & 0xFFFF),
             rssi, snr, buf);
    startRx();
    return;
  }
#endif

  // Acceptance steps 2-3 (§6): drop self and foreign groups.
  wm_rx_decision_t d = wm_beacon_accept(&b, gNodeId, &gCfg);
  if (d == WM_RX_DROP_FOREIGN_GROUP) {
    // Logged so the "two groups on one band ignore each other" gate is visible.
    char ex[24];
    snprintf(ex, sizeof(ex), "foreign_group ch=%u", (unsigned)b.chan_hash);
    logEvent("drop", "lrp", b.src_id, (long)(b.packet_id & 0xFFFF), rssi, snr, ex);
    startRx();
    return;
  }
  if (d != WM_RX_ACCEPT) { // DROP_SELF
    startRx();
    return;
  }

  // Accepted: a beacon from a group we belong to.
  gRxCount++;
  if (gHaveLastRxId) {
    // Forward gap on a single peer's packetId; ignore reserve-ahead block skips
    // / reboots / reordering (Phase 0 single-peer PDR metric, best-effort).
    int32_t delta = (int32_t)(b.packet_id - (gLastRxId + 1));
    if (delta > 0 && delta < (int32_t)WM_PKTID_BLOCK)
      gRxSeqGaps += (uint32_t)delta;
  }
  gLastRxId = b.packet_id;
  gHaveLastRxId = true;

  char ex[20];
  snprintf(ex, sizeof(ex), "beacon ch=%u", (unsigned)b.chan_hash);
  logEvent("rx", "lrp", b.src_id, (long)(b.packet_id & 0xFFFF), rssi, snr, ex);

  // Feed the BLE gateway's peer node DB (Phase G 1b); position only when valid.
  bleGattOnPeer(b.src_id, b.lat_i, b.lon_i, b.sats, b.has_pos, snr);
  startRx();
}

static void printStatus() {
  // Rough PDR estimate vs a single peer: received / (received + inferred gaps).
  uint32_t expected = gRxCount + gRxSeqGaps;
  float pdr = expected ? (100.0f * (float)gRxCount / (float)expected) : 0.0f;
  char extra[112];
  unsigned long sats =
      (unsigned long)(gGps.satellites.isValid() ? gGps.satellites.value() : 0);
#if WAYMESH_RELAY_WITNESS
  snprintf(extra, sizeof(extra),
           "tx=%u rx=%u gaps=%u badcrc=%u pdr=%.1f%% sats=%lu relayback=%u",
           (unsigned)gTxSeq, (unsigned)gRxCount, (unsigned)gRxSeqGaps,
           (unsigned)gRxBadCount, pdr, sats, (unsigned)gRelayedBack);
#else
  snprintf(extra, sizeof(extra),
           "tx=%u rx=%u gaps=%u badcrc=%u pdr=%.1f%% sats=%lu",
           (unsigned)gTxSeq, (unsigned)gRxCount, (unsigned)gRxSeqGaps,
           (unsigned)gRxBadCount, pdr, sats);
#endif
  logEvent("status", "node", 0, -1, NAN, NAN, extra);
}

// UTC epoch seconds from the GNSS date/time, or 0 if no valid time fix.
// (Howard Hinnant's days_from_civil.) Used for Meshtastic NodeInfo.last_heard.
static uint32_t gpsEpoch() {
  if (!gGps.date.isValid() || !gGps.time.isValid() || gGps.date.year() < 2024) {
    return 0;
  }
  int y = (int)gGps.date.year();
  unsigned m = gGps.date.month();
  unsigned d = gGps.date.day();
  y -= (m <= 2);
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const long days = (long)era * 146097 + (long)doe - 719468;
  return (uint32_t)(days * 86400L + gGps.time.hour() * 3600L +
                    gGps.time.minute() * 60L + gGps.time.second());
}

#if WAYMESH_TEST_PEER
// Inject a synthetic LoRa peer (no RF) to validate the gateway node-DB ->
// NodeInfo -> Meshtastic path on a single node. Build env esp32c3_xr2_testpeer
// only; never compiled into the production env. The peer drifts each tick so
// live position updates are exercised too.
static void injectTestPeer() {
  static unsigned long last = 0;
  static int step = 0;
  const unsigned long now = millis();
  if (now - last < 2000) return;
  last = now;
  bleGattOnPeer(0xCAFE1234, 587530000 + step * 300L, 168600000 + step * 300L,
                7, true, -8.5f);
  step = (step + 1) % 60;
}
#endif

// ---- Setup / loop -----------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(PIN_LED, OUTPUT);
  ledWrite(false);

  gNodeId = (uint32_t)(ESP.getEfuseMac() & 0xFFFFFFFFULL);

  // Persistent config: channels (seeds the Meshtastic default "LongFast" open
  // group -> chanHash 8 on fresh NVS), relay policy, and the crash-safe
  // packetId counter (reserve-ahead high-water in NVS). See doc 13 §8.
  wm_store_t store = wm_nvs_store();
  wm_config_init(&gCfg, &store);

  Serial.println();
  Serial.println("# waymesh-node Phase 0 (XR2: ESP32-C3 + LR1121, 2.4 GHz LoRa)");
  Serial.printf("# nodeId=%08X freq=%.1fMHz bw=%.1fkHz sf=%d cr=4/%d pwr=%ddBm\n",
                gNodeId, (double)LORA_FREQ_MHZ, (double)LORA_BW_KHZ, LORA_SF,
                LORA_CR, LORA_POWER_DBM);
  Serial.println("# pins verified vs ELRS XR2 target; LR1121 RF-switch/DCDC at "
                 "RadioLib defaults - confirm TX/RX routing before trusting range.");
  {
    const wm_channel_t *home = wm_config_home(&gCfg);
    Serial.printf("# config: %u chan(s), home '%s' chanHash=%d relay=%s\n",
                  (unsigned)gCfg.channel_count, home ? home->name : "-",
                  home ? home->hash : -1,
                  gCfg.relay_policy == WM_RELAY_ALL ? "all" : "known");
  }
  Serial.println("ts_ms,nodeId,role,event,plane,srcId,seq,rssi,snr,lat,lon,extra");

  // GNSS on the spare UART.
  gGnssSerial.begin(GNSS_BAUD, SERIAL_8N1, PIN_GNSS_RX, PIN_GNSS_TX);

  // Custom SPI pin mapping for the LR1121 on the C3.
  SPI.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_NSS);

  // Bring up the LR1121 directly in 2.4 GHz LoRa. tcxoVoltage = LORA_TCXO_V
  // (0 = no TCXO control) until the XR2's real TCXO config is confirmed.
  int16_t st = gRadio.begin(LORA_FREQ_MHZ, LORA_BW_KHZ, LORA_SF, LORA_CR,
                            RADIOLIB_LR11X0_LORA_SYNC_WORD_PRIVATE,
                            LORA_POWER_DBM, LORA_PREAMBLE, LORA_TCXO_V);
  if (st != RADIOLIB_ERR_NONE) {
    char buf[28];
    snprintf(buf, sizeof(buf), "begin_err=%d", st);
    logEvent("error", "lrp", 0, -1, NAN, NAN, buf);
    Serial.println("# radio begin() failed - check SPI pins/wiring. Halting RF.");
  } else {
    gRadio.setIrqAction(onDio1);
    startRx();
    gRadioOk = true;
    logEvent("boot", "node", gNodeId, -1, NAN, NAN, "radio_ok");
  }

  // Meshtastic BLE GATT transport (Phase G, increment 1 — stub). Comes up even if
  // the radio failed, so the BLE path can be brought up/tested independently.
  bleGattBegin(gNodeId);
}

void loop() {
  // Feed the GNSS parser.
  while (gGnssSerial.available()) {
    if (gGps.encode(gGnssSerial.read()) && gGps.location.isUpdated() &&
        gGps.location.isValid()) {
      logEvent("gps_fix", "node", 0, -1, NAN, NAN, "");
      // Feed the fix to the BLE gateway's self NodeInfo (degrees * 1e7) + the
      // current epoch (for NodeInfo.last_heard on self and peers). SetTime FIRST:
      // bleGattSetPosition now also live-emits the self NodeInfo and stamps it
      // with the current epoch, so the epoch must be fresh before that emit.
      bleGattSetTime(gpsEpoch());
      bleGattSetPosition(
          (int32_t)lround(gGps.location.lat() * 1e7),
          (int32_t)lround(gGps.location.lng() * 1e7),
          gGps.satellites.isValid() ? gGps.satellites.value() : 0, true);
    }
  }

  if (gRadioOk) {
    // A DIO1 event fired: a beacon arrived (or RX errored).
    if (gRxFlag) {
      gRxFlag = false;
      handleRx();
    }

    // Time to transmit our own loopback beacon.
    const unsigned long now = millis();
    if (now - gLastBeaconMs >= BEACON_PERIOD_MS) {
      gLastBeaconMs = now;
      gLedOn = !gLedOn;
      ledWrite(gLedOn);
      sendBeacon();
      // Announce ourselves to a connected BLE client on the same cadence the
      // peers get re-announced (bleGattOnPeer on each beacon heard) — keeps this
      // node in the app's list even with no GPS fix. Rate-limited internally, so
      // it no-ops while a live fix is already streaming via bleGattSetPosition.
      bleGattHeartbeat();
    }
  }

#if WAYMESH_TEST_PEER
  injectTestPeer();
#endif

  bleGattLoop();

  const unsigned long nowStatus = millis();
  if (nowStatus - gLastStatusMs >= STATUS_PERIOD_MS) {
    gLastStatusMs = nowStatus;
    printStatus();
  }
}
