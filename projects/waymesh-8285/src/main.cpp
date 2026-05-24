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
// NOT here (later, Tier 2/3): GPS over remapped PWM-pin UART, PWM servo outputs,
// relay/suppression/dedup. PoC #0 is pure radio interop.
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

// ---- Radio ------------------------------------------------------------------
// SX128x Module pin order: (NSS/CS, IRQ/DIO1, RESET, BUSY) — same order as the
// LR11x0 on the XR2, just a different RadioLib class.
static SX1280 gRadio = new Module(PIN_LORA_NSS, PIN_LORA_DIO1, PIN_LORA_RST, PIN_LORA_BUSY);

// ---- Loopback beacon packet -------------------------------------------------
// BYTE-IDENTICAL to waymesh-node (XR2). v1 carries an optional GPS position; on
// this PoC #0 build there is no GPS, so we send v1 with flags=0 (no position) —
// still a valid beacon the XR2 maps to a peer NodeInfo (sans Position).
static const uint8_t WM_MAGIC = 0x57;  // 'W'
static const uint8_t WM_VERSION = 1;
static const uint8_t BEACON_FLAG_POS = 0x01;  // lat_i/lon_i valid

struct __attribute__((packed)) Beacon {
  uint8_t magic;
  uint8_t version;
  uint32_t srcId;
  uint16_t seq;
  // v1 tail:
  int32_t lat_i;   // degrees * 1e7
  int32_t lon_i;   // degrees * 1e7
  uint8_t sats;
  uint8_t flags;   // BEACON_FLAG_*
};
static const size_t BEACON_V0_SIZE = 8;  // magic+version+srcId+seq

// ---- State ------------------------------------------------------------------
static uint32_t gNodeId = 0;
static uint16_t gTxSeq = 0;
static uint32_t gRxCount = 0;
static uint32_t gRxBadCount = 0;
static uint16_t gLastRxSeq = 0;
static bool gHaveLastRxSeq = false;
static uint32_t gRxSeqGaps = 0;

static unsigned long gLastBeaconMs = 0;
static unsigned long gLastStatusMs = 0;
static bool gLedOn = false;

static volatile bool gRxFlag = false;
static bool gRadioOk = false;

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

static void sendBeacon() {
  Beacon b;
  memset(&b, 0, sizeof(b));
  b.magic = WM_MAGIC;
  b.version = WM_VERSION;
  b.srcId = gNodeId;
  b.seq = gTxSeq;
  // No GPS on PoC #0 -> flags stays 0 (no position). Tier 2 fills the v1 tail.

  int16_t st = gRadio.transmit((uint8_t *)&b, sizeof(b));
  if (st == RADIOLIB_ERR_NONE) {
    logEvent("tx", "lrp", gNodeId, gTxSeq, NAN, NAN, "beacon");
    gTxSeq++;
  } else {
    char buf[20];
    snprintf(buf, sizeof(buf), "tx_err=%d", st);
    logEvent("error", "lrp", 0, -1, NAN, NAN, buf);
  }
  // transmit() leaves the radio in standby; the shared DIO1 ISR also fires on
  // TxDone. Clear the phantom flag before re-arming RX so handleRx() doesn't read
  // an empty FIFO and inflate badcrc (same fix as the XR2 firmware).
  gRxFlag = false;
  startRx();
}

static void handleRx() {
  uint8_t raw[sizeof(Beacon)];
  memset(raw, 0, sizeof(raw));
  size_t plen = gRadio.getPacketLength();
  if (plen > sizeof(raw)) plen = sizeof(raw);
  int16_t st = gRadio.readData(raw, plen);

  Beacon b;
  memset(&b, 0, sizeof(b));
  if (plen >= BEACON_V0_SIZE) memcpy(&b, raw, plen);

  if (st == RADIOLIB_ERR_NONE && plen >= BEACON_V0_SIZE && b.magic == WM_MAGIC &&
      b.srcId != gNodeId) {
    gRxCount++;
    float rssi = gRadio.getRSSI();
    float snr = gRadio.getSNR();
    if (gHaveLastRxSeq) {
      uint16_t expected = (uint16_t)(gLastRxSeq + 1);
      int16_t delta = (int16_t)(b.seq - expected);  // signed modular gap
      if (delta > 0) gRxSeqGaps += (uint16_t)delta;
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
  uint32_t expected = gRxCount + gRxSeqGaps;
  float pdr = expected ? (100.0f * (float)gRxCount / (float)expected) : 0.0f;
  char extra[80];
  snprintf(extra, sizeof(extra), "tx=%u rx=%u gaps=%u badcrc=%u pdr=%.1f%%",
           (unsigned)gTxSeq, (unsigned)gRxCount, (unsigned)gRxSeqGaps,
           (unsigned)gRxBadCount, pdr);
  logEvent("status", "node", 0, -1, NAN, NAN, extra);
}

// ---- Setup / loop -----------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(PIN_LED, OUTPUT);
  ledWrite(false);

  // ESP8266/ESP8285 has no efuse-MAC API; getChipId() (low 24 bits of the STA
  // MAC) is a stable per-board id. Distinct from the XR2's 32-bit nodeId.
  gNodeId = ESP.getChipId();

  Serial.println();
  Serial.println("# waymesh-8285 PoC #0 (BayckRC 7PWM: ESP8285 + SX1280, 2.4 GHz LoRa)");
  Serial.printf("# nodeId=%08X freq=%.1fMHz bw=%.1fkHz sf=%d cr=4/%d pwr=%ddBm sync=0x%02X\n",
                gNodeId, (double)LORA_FREQ_MHZ, (double)LORA_BW_KHZ, LORA_SF,
                LORA_CR, LORA_POWER_DBM, (unsigned)LORA_SYNC_WORD);
  Serial.println("# MUST match the XR2 LR1121 config for interop (Phase H PoC #0).");
  Serial.println("ts_ms,nodeId,role,event,plane,srcId,seq,rssi,snr,lat,lon,extra");

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
    char buf[40];
    snprintf(buf, sizeof(buf), "radio_ok sw=%d cr=%d", sw, cr);
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
  if (gRadioOk) {
    if (gRxFlag) {
      gRxFlag = false;
      handleRx();
    }
    const unsigned long now = millis();
    if (now - gLastBeaconMs >= BEACON_PERIOD_MS) {
      gLastBeaconMs = now;
      gLedOn = !gLedOn;
      ledWrite(gLedOn);
      sendBeacon();
    }
  }

  const unsigned long nowStatus = millis();
  if (nowStatus - gLastStatusMs >= STATUS_PERIOD_MS) {
    gLastStatusMs = nowStatus;
    printStatus();
  }
}
