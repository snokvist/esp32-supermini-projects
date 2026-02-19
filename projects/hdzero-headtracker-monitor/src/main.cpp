#include <Arduino.h>

#ifndef LED_PIN
#define LED_PIN 8
#endif

// Connect HDZero BoxPro+ 3.5mm TS jack tip (HT PPM output) to this pin.
// Sleeve goes to ESP32 GND.
#ifndef PPM_INPUT_PIN
#define PPM_INPUT_PIN 2
#endif

// On ESP32-C3 SuperMini, BOOT button is on GPIO9 (active low).
#ifndef MODE_BUTTON_PIN
#define MODE_BUTTON_PIN 9
#endif

namespace {
constexpr uint8_t kMaxChannels = 12;
constexpr uint8_t kMinChannelsInFrame = 3;
constexpr uint16_t kMinChannelUs = 800;
constexpr uint16_t kMaxChannelUs = 2200;
constexpr uint16_t kSyncGapUs = 3000;
constexpr uint32_t kSignalTimeoutMs = 1000;
constexpr uint32_t kButtonDebounceMs = 30;
constexpr uint32_t kMonitorPrintIntervalMs = 200;
constexpr float kExpectedMinFrameHz = 45.0f;
constexpr float kExpectedMaxFrameHz = 55.0f;
constexpr float kFrameHzEmaAlpha = 0.1f;

constexpr uint8_t kCrsfAddress = 0xC8;
constexpr uint8_t kCrsfFrameTypeRcChannelsPacked = 0x16;
constexpr uint8_t kCrsfChannelCount = 16;
constexpr uint8_t kCrsfPayloadSize = 22;
constexpr uint8_t kCrsfPacketSize = 26;
constexpr uint16_t kCrsfMinValue = 172;
constexpr uint16_t kCrsfCenterValue = 992;
constexpr uint16_t kCrsfMaxValue = 1811;
constexpr uint16_t kCrsfMapMinUs = 1000;
constexpr uint16_t kCrsfMapMaxUs = 2000;

volatile uint16_t gIsrChannels[kMaxChannels] = {0};
volatile uint8_t gIsrChannelCount = 0;
volatile uint8_t gIsrFrameChannelCount = 0;
volatile bool gIsrFrameReady = false;
volatile uint32_t gLastRiseUs = 0;
volatile uint32_t gFrameCounter = 0;
volatile uint32_t gInvalidPulseCounter = 0;

uint16_t gLatestChannels[kMaxChannels] = {0};
uint8_t gLatestChannelCount = 0;
uint32_t gLastFrameMs = 0;
uint32_t gLastFrameUs = 0;
float gFrameHz = 0.0f;
float gFrameHzEma = 0.0f;
bool gHasFrameHzEma = false;
bool gLedState = false;
uint32_t gLastLedToggleMs = 0;
uint32_t gLastPrintMs = 0;
bool gButtonRawState = HIGH;
bool gButtonStableState = HIGH;
uint32_t gLastButtonChangeMs = 0;
uint32_t gLatestFrameCounter = 0;
uint32_t gLatestInvalidPulseCounter = 0;
uint32_t gLastMonitorFrameCounter = 0;
uint32_t gLastMonitorInvalidPulseCounter = 0;

enum class OutputMode : uint8_t { kCrsfSerial, kPpmMonitor };
OutputMode gOutputMode = OutputMode::kCrsfSerial;

void IRAM_ATTR onPpmRise() {
  const uint32_t nowUs = micros();
  const uint32_t dt = nowUs - gLastRiseUs;
  gLastRiseUs = nowUs;

  if (dt >= kSyncGapUs) {
    if (gIsrChannelCount >= kMinChannelsInFrame) {
      gIsrFrameChannelCount = gIsrChannelCount;
      gIsrFrameReady = true;
      gFrameCounter++;
    }
    gIsrChannelCount = 0;
    return;
  }

  if (dt < kMinChannelUs || dt > kMaxChannelUs) {
    gInvalidPulseCounter++;
    return;
  }

  if (gIsrChannelCount < kMaxChannels) {
    gIsrChannels[gIsrChannelCount++] = static_cast<uint16_t>(dt);
  }
}

uint8_t crc8DvbS2(const uint8_t *data, size_t length) {
  uint8_t crc = 0;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0xD5) : static_cast<uint8_t>(crc << 1);
    }
  }
  return crc;
}

uint16_t mapPulseUsToCrsf(uint16_t pulseUs) {
  uint16_t clampedUs = pulseUs;
  if (clampedUs < kCrsfMapMinUs) {
    clampedUs = kCrsfMapMinUs;
  } else if (clampedUs > kCrsfMapMaxUs) {
    clampedUs = kCrsfMapMaxUs;
  }

  const uint32_t numerator = static_cast<uint32_t>(clampedUs - kCrsfMapMinUs) * (kCrsfMaxValue - kCrsfMinValue);
  const uint32_t denominator = (kCrsfMapMaxUs - kCrsfMapMinUs);
  return static_cast<uint16_t>(kCrsfMinValue + ((numerator + (denominator / 2U)) / denominator));
}

void sendCrsfRcFrame() {
  uint16_t channels[kCrsfChannelCount];
  for (uint8_t i = 0; i < kCrsfChannelCount; ++i) {
    channels[i] = kCrsfCenterValue;
  }

  const uint8_t count = (gLatestChannelCount < kCrsfChannelCount) ? gLatestChannelCount : kCrsfChannelCount;
  for (uint8_t i = 0; i < count; ++i) {
    channels[i] = mapPulseUsToCrsf(gLatestChannels[i]);
  }

  uint8_t payload[kCrsfPayloadSize] = {0};
  uint32_t bitBuffer = 0;
  uint8_t bitsInBuffer = 0;
  uint8_t payloadIndex = 0;

  for (uint8_t i = 0; i < kCrsfChannelCount; ++i) {
    bitBuffer |= (static_cast<uint32_t>(channels[i]) & 0x07FFU) << bitsInBuffer;
    bitsInBuffer += 11;
    while (bitsInBuffer >= 8 && payloadIndex < kCrsfPayloadSize) {
      payload[payloadIndex++] = static_cast<uint8_t>(bitBuffer & 0xFFU);
      bitBuffer >>= 8;
      bitsInBuffer -= 8;
    }
  }

  if (bitsInBuffer > 0 && payloadIndex < kCrsfPayloadSize) {
    payload[payloadIndex++] = static_cast<uint8_t>(bitBuffer & 0xFFU);
  }

  uint8_t packet[kCrsfPacketSize] = {0};
  packet[0] = kCrsfAddress;
  packet[1] = 24; // type + payload + crc
  packet[2] = kCrsfFrameTypeRcChannelsPacked;
  for (uint8_t i = 0; i < kCrsfPayloadSize; ++i) {
    packet[3 + i] = payload[i];
  }
  packet[25] = crc8DvbS2(&packet[2], 1 + kCrsfPayloadSize);
  Serial.write(packet, sizeof(packet));
}

void printPpmMonitorFrame(uint32_t invalidPulses) {
  const uint32_t nowMs = millis();
  const uint32_t elapsedMs = nowMs - gLastPrintMs;
  if (elapsedMs < kMonitorPrintIntervalMs) {
    return;
  }
  gLastPrintMs = nowMs;

  const uint32_t frameCountDelta = gLatestFrameCounter - gLastMonitorFrameCounter;
  const float windowHz = (elapsedMs > 0) ? (static_cast<float>(frameCountDelta) * 1000.0f / static_cast<float>(elapsedMs))
                                         : 0.0f;
  const bool rateOk = (windowHz >= kExpectedMinFrameHz) && (windowHz <= kExpectedMaxFrameHz);
  const uint32_t invalidDelta = invalidPulses - gLastMonitorInvalidPulseCounter;
  const uint32_t frameAgeMs = nowMs - gLastFrameMs;

  uint16_t minChannelUs = 0;
  uint16_t maxChannelUs = 0;
  if (gLatestChannelCount > 0) {
    minChannelUs = gLatestChannels[0];
    maxChannelUs = gLatestChannels[0];
    for (uint8_t i = 1; i < gLatestChannelCount; ++i) {
      if (gLatestChannels[i] < minChannelUs) {
        minChannelUs = gLatestChannels[i];
      }
      if (gLatestChannels[i] > maxChannelUs) {
        maxChannelUs = gLatestChannels[i];
      }
    }
  }

  Serial.printf("PPM: ch=%u inst=%.1fHz ema=%.1fHz win=%.1fHz(%s) age=%lums invalid=%lu(+%lu) span=%u..%u",
                gLatestChannelCount, gFrameHz, gFrameHzEma, windowHz, rateOk ? "ok" : "warn",
                static_cast<unsigned long>(frameAgeMs), static_cast<unsigned long>(invalidPulses),
                static_cast<unsigned long>(invalidDelta), static_cast<unsigned>(minChannelUs),
                static_cast<unsigned>(maxChannelUs));
  for (uint8_t i = 0; i < gLatestChannelCount; ++i) {
    Serial.printf(" ch%u=%u", static_cast<unsigned>(i + 1), static_cast<unsigned>(gLatestChannels[i]));
  }
  if (gLatestChannelCount >= 3) {
    // BoxPro+ firmware writes CH1=pan, CH2=roll, CH3=tilt.
    Serial.printf(" | pan=%u roll=%u tilt=%u", static_cast<unsigned>(gLatestChannels[0]),
                  static_cast<unsigned>(gLatestChannels[1]), static_cast<unsigned>(gLatestChannels[2]));
  }
  Serial.println();

  gLastMonitorFrameCounter = gLatestFrameCounter;
  gLastMonitorInvalidPulseCounter = invalidPulses;
}

bool copyFrameFromIsr(uint32_t &invalidPulses) {
  noInterrupts();
  const bool frameReady = gIsrFrameReady;
  const uint8_t frameChannels = gIsrFrameChannelCount;
  const uint32_t frameCounter = gFrameCounter;
  invalidPulses = gInvalidPulseCounter;
  gLatestFrameCounter = frameCounter;
  gLatestInvalidPulseCounter = invalidPulses;
  if (frameReady) {
    for (uint8_t i = 0; i < frameChannels; ++i) {
      gLatestChannels[i] = gIsrChannels[i];
    }
    gLatestChannelCount = frameChannels;
    gIsrFrameReady = false;
  }
  interrupts();

  if (!frameReady) {
    return false;
  }

  const uint32_t nowUs = micros();
  const uint32_t nowMs = millis();
  if (gLastFrameUs > 0) {
    const uint32_t dtUs = nowUs - gLastFrameUs;
    if (dtUs > 0) {
      gFrameHz = 1000000.0f / static_cast<float>(dtUs);
      if (!gHasFrameHzEma) {
        gFrameHzEma = gFrameHz;
        gHasFrameHzEma = true;
      } else {
        gFrameHzEma += kFrameHzEmaAlpha * (gFrameHz - gFrameHzEma);
      }
    }
  }
  gLastFrameUs = nowUs;
  gLastFrameMs = nowMs;
  return true;
}

void toggleOutputMode() {
  if (gOutputMode == OutputMode::kCrsfSerial) {
    gOutputMode = OutputMode::kPpmMonitor;
    gLastMonitorFrameCounter = gLatestFrameCounter;
    gLastMonitorInvalidPulseCounter = gLatestInvalidPulseCounter;
    gLastPrintMs = millis();
    Serial.println("Mode switched: PPM monitor text output");
    return;
  }
  gOutputMode = OutputMode::kCrsfSerial;
}

void handleModeButton() {
  const uint32_t nowMs = millis();
  const bool rawState = digitalRead(MODE_BUTTON_PIN);
  if (rawState != gButtonRawState) {
    gButtonRawState = rawState;
    gLastButtonChangeMs = nowMs;
  }

  if ((nowMs - gLastButtonChangeMs) < kButtonDebounceMs) {
    return;
  }

  if (gButtonStableState == gButtonRawState) {
    return;
  }

  gButtonStableState = gButtonRawState;
  if (gButtonStableState == LOW) {
    toggleOutputMode();
  }
}

void updateLed() {
  const uint32_t nowMs = millis();
  const bool hasSignal = (nowMs - gLastFrameMs) <= kSignalTimeoutMs;
  const uint32_t blinkPeriodMs = hasSignal ? 100 : 500;
  if (nowMs - gLastLedToggleMs < blinkPeriodMs) {
    return;
  }
  gLastLedToggleMs = nowMs;
  gLedState = !gLedState;
  digitalWrite(LED_PIN, gLedState ? HIGH : LOW);
}
} // namespace

void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(PPM_INPUT_PIN, INPUT);
  pinMode(MODE_BUTTON_PIN, INPUT_PULLUP);
  Serial.begin(115200);
  gButtonRawState = digitalRead(MODE_BUTTON_PIN);
  gButtonStableState = gButtonRawState;
  gLastButtonChangeMs = millis();

  attachInterrupt(digitalPinToInterrupt(PPM_INPUT_PIN), onPpmRise, RISING);
}

void loop() {
  uint32_t invalidPulses = 0;
  const bool frameReady = copyFrameFromIsr(invalidPulses);
  handleModeButton();

  if (frameReady) {
    if (gOutputMode == OutputMode::kCrsfSerial) {
      sendCrsfRcFrame();
    } else {
      printPpmMonitorFrame(invalidPulses);
    }
  }

  updateLed();

  const uint32_t nowMs = millis();
  if (gOutputMode == OutputMode::kPpmMonitor && (nowMs - gLastFrameMs) > kSignalTimeoutMs &&
      (nowMs - gLastPrintMs) > 1000) {
    gLastPrintMs = nowMs;
    Serial.println("No PPM frame detected yet. Check HT enabled + wiring.");
  }
}
