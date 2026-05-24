#include "ble_gatt.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

// Meshtastic client API UUIDs — verified against
// https://meshtastic.org/docs/development/device/client-api/ (2026-05-24).
static const char *MESHTASTIC_SERVICE_UUID = "6ba1b218-15a8-461f-9fa8-5dcae273eafd";
static const char *TORADIO_UUID   = "f75c76d2-129e-4dad-a1dd-7866124401e7"; // write
static const char *FROMRADIO_UUID = "2c55e69e-4993-11ed-b878-0242ac120002"; // read
static const char *FROMNUM_UUID   = "ed9da18c-a800-4f66-a670-aa7547e34453"; // read + notify

static NimBLECharacteristic *gFromRadio = nullptr;
static NimBLECharacteristic *gFromNum = nullptr;

static volatile bool gBleConnected = false;
static uint32_t gFromNumVal = 0;

// Stub FromRadio payload until the protobuf layer (Layer 2) replaces it; lets a
// host confirm a read returns bytes.
static const uint8_t kStubFromRadio[] = {0xDE, 0xAD, 0xBE, 0xEF};

static void logHex(const char *tag, const uint8_t *d, size_t n) {
  Serial.printf("# BLE %s len=%u", tag, (unsigned)n);
  for (size_t i = 0; i < n && i < 32; i++) Serial.printf(" %02X", d[i]);
  if (n > 32) Serial.print(" ...");
  Serial.println();
}

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *) override {
    gBleConnected = true;
    Serial.println("# BLE connect");
  }
  void onDisconnect(NimBLEServer *) override {
    gBleConnected = false;
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
    logHex("ToRadio<-", (const uint8_t *)v.data(), v.size());
    // Layer 2: parse as a Meshtastic ToRadio protobuf (want_config_id, packet, ...).
  }
};

class FromRadioCallbacks : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic *) override {
    Serial.println("# BLE FromRadio-> (stub read)");
    // Layer 2: return the next queued FromRadio protobuf; empty when drained.
  }
};

void bleGattBegin(uint32_t nodeId) {
  char name[20];
  snprintf(name, sizeof(name), "Waymesh_%04X", (unsigned)(nodeId & 0xFFFF));

  NimBLEDevice::init(name);
  NimBLEDevice::setMTU(517);  // Meshtastic FromRadio needs a large MTU (~512).
  // Layer 1: open (no bonding) for easy host bring-up. Layer 2 adds a fixed PIN.

  NimBLEServer *server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  NimBLEService *svc = server->createService(MESHTASTIC_SERVICE_UUID);

  gFromRadio = svc->createCharacteristic(FROMRADIO_UUID, NIMBLE_PROPERTY::READ);
  gFromRadio->setCallbacks(new FromRadioCallbacks());
  gFromRadio->setValue(kStubFromRadio, sizeof(kStubFromRadio));

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

  Serial.printf("# BLE GATT up: name=%s service=%s (Meshtastic, stub)\n", name,
                MESHTASTIC_SERVICE_UUID);
}

void bleGattLoop() {
  static unsigned long last = 0;
  const unsigned long now = millis();
  if (now - last < 5000) return;
  last = now;
  if (!gBleConnected || !gFromNum) return;
  // Layer 2: only bump when new FromRadio data is actually queued; the client
  // then reads FromRadio until it catches up to this counter.
  gFromNumVal++;
  gFromNum->setValue(gFromNumVal);
  gFromNum->notify();
  Serial.printf("# BLE FromNum notify=%u\n", (unsigned)gFromNumVal);
}
