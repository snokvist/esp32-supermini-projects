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

// ---- Radio ------------------------------------------------------------------
// LR11x0 Module pin order: (NSS/CS, IRQ/DIO1, RESET, BUSY)
static LR1121 gRadio = new Module(PIN_LORA_NSS, PIN_LORA_DIO1, PIN_LORA_RST, PIN_LORA_BUSY);

// ---- GNSS -------------------------------------------------------------------
static TinyGPSPlus gGps;
static HardwareSerial gGnssSerial(1);

// ---- Loopback beacon packet -------------------------------------------------
static const uint8_t WM_MAGIC = 0x57;  // 'W'
static const uint8_t WM_VERSION = 0;

struct __attribute__((packed)) Beacon {
  uint8_t magic;
  uint8_t version;
  uint32_t srcId;
  uint16_t seq;
};

// ---- State ------------------------------------------------------------------
static uint32_t gNodeId = 0;
static uint16_t gTxSeq = 0;
static uint32_t gRxCount = 0;
static uint32_t gRxBadCount = 0;
static uint16_t gLastRxSeq = 0;
static bool gHaveLastRxSeq = false;
static uint32_t gRxSeqGaps = 0;  // missed beacons inferred from seq jumps

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
  Beacon b;
  b.magic = WM_MAGIC;
  b.version = WM_VERSION;
  b.srcId = gNodeId;
  b.seq = gTxSeq;

  int16_t st = gRadio.transmit((uint8_t *)&b, sizeof(b));
  if (st == RADIOLIB_ERR_NONE) {
    logEvent("tx", "lrp", gNodeId, gTxSeq, NAN, NAN, "beacon");
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
  Beacon b;
  int16_t st = gRadio.readData((uint8_t *)&b, sizeof(b));
  if (st == RADIOLIB_ERR_NONE && b.magic == WM_MAGIC && b.srcId != gNodeId) {
    gRxCount++;
    float rssi = gRadio.getRSSI();
    float snr = gRadio.getSNR();
    if (gHaveLastRxSeq) {
      uint16_t expected = (uint16_t)(gLastRxSeq + 1);
      // Signed modular delta: >0 is a forward gap (missed beacons); <=0 is a
      // duplicate / reordered / post-restart frame and must NOT count as loss.
      // (The old unsigned subtraction wrapped to ~65535 on any dup and exploded
      // the PDR estimate.) Assumes a single peer.
      int16_t delta = (int16_t)(b.seq - expected);
      if (delta > 0) {
        gRxSeqGaps += (uint16_t)delta;
      }
    }
    gLastRxSeq = b.seq;
    gHaveLastRxSeq = true;
    logEvent("rx", "lrp", b.srcId, b.seq, rssi, snr, "beacon");
  } else if (st != RADIOLIB_ERR_NONE) {
    gRxBadCount++;
    char buf[20];
    snprintf(buf, sizeof(buf), "rx_err=%d", st);
    logEvent("error", "lrp", 0, -1, NAN, NAN, buf);
  }
  startRx();
}

static void printStatus() {
  // Rough PDR estimate vs a single peer: received / (received + inferred gaps).
  uint32_t expected = gRxCount + gRxSeqGaps;
  float pdr = expected ? (100.0f * (float)gRxCount / (float)expected) : 0.0f;
  char extra[96];
  snprintf(extra, sizeof(extra),
           "tx=%u rx=%u gaps=%u badcrc=%u pdr=%.1f%% sats=%lu",
           (unsigned)gTxSeq, (unsigned)gRxCount, (unsigned)gRxSeqGaps,
           (unsigned)gRxBadCount, pdr,
           (unsigned long)(gGps.satellites.isValid() ? gGps.satellites.value() : 0));
  logEvent("status", "node", 0, -1, NAN, NAN, extra);
}

// ---- Setup / loop -----------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(PIN_LED, OUTPUT);
  ledWrite(false);

  gNodeId = (uint32_t)(ESP.getEfuseMac() & 0xFFFFFFFFULL);

  Serial.println();
  Serial.println("# waymesh-node Phase 0 (XR2: ESP32-C3 + LR1121, 2.4 GHz LoRa)");
  Serial.printf("# nodeId=%08X freq=%.1fMHz bw=%.1fkHz sf=%d cr=4/%d pwr=%ddBm\n",
                gNodeId, (double)LORA_FREQ_MHZ, (double)LORA_BW_KHZ, LORA_SF,
                LORA_CR, LORA_POWER_DBM);
  Serial.println("# pins verified vs ELRS XR2 target; LR1121 RF-switch/DCDC at "
                 "RadioLib defaults - confirm TX/RX routing before trusting range.");
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
      // Feed the fix to the BLE gateway's self NodeInfo (degrees * 1e7).
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
    }
  }

  bleGattLoop();

  const unsigned long nowStatus = millis();
  if (nowStatus - gLastStatusMs >= STATUS_PERIOD_MS) {
    gLastStatusMs = nowStatus;
    printStatus();
  }
}
