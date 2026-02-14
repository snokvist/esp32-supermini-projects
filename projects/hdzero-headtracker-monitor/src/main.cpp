#include <Arduino.h>

#ifndef LED_PIN
#define LED_PIN 8
#endif

// Connect HDZero BoxPro+ 3.5mm TS jack tip (HT PPM output) to this pin.
// Sleeve goes to ESP32 GND.
#ifndef PPM_INPUT_PIN
#define PPM_INPUT_PIN 2
#endif

namespace {
constexpr uint8_t kMaxChannels = 12;
constexpr uint8_t kMinChannelsInFrame = 3;
constexpr uint16_t kMinChannelUs = 800;
constexpr uint16_t kMaxChannelUs = 2200;
constexpr uint16_t kSyncGapUs = 3000;
constexpr uint32_t kSignalTimeoutMs = 1000;

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
bool gLedState = false;
uint32_t gLastLedToggleMs = 0;
uint32_t gLastPrintMs = 0;

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

void copyFrameFromIsr() {
  noInterrupts();
  const bool frameReady = gIsrFrameReady;
  const uint8_t frameChannels = gIsrFrameChannelCount;
  const uint32_t invalidPulses = gInvalidPulseCounter;
  if (frameReady) {
    for (uint8_t i = 0; i < frameChannels; ++i) {
      gLatestChannels[i] = gIsrChannels[i];
    }
    gLatestChannelCount = frameChannels;
    gIsrFrameReady = false;
  }
  interrupts();

  if (!frameReady) {
    return;
  }

  const uint32_t nowUs = micros();
  const uint32_t nowMs = millis();
  if (gLastFrameUs > 0) {
    const uint32_t dtUs = nowUs - gLastFrameUs;
    if (dtUs > 0) {
      gFrameHz = 1000000.0f / static_cast<float>(dtUs);
    }
  }
  gLastFrameUs = nowUs;
  gLastFrameMs = nowMs;

  if (nowMs - gLastPrintMs < 200) {
    return;
  }
  gLastPrintMs = nowMs;

  Serial.printf("PPM: ch=%u rate=%.1fHz invalid=%lu", gLatestChannelCount, gFrameHz,
                static_cast<unsigned long>(invalidPulses));
  for (uint8_t i = 0; i < gLatestChannelCount; ++i) {
    Serial.printf(" ch%u=%u", static_cast<unsigned>(i + 1), static_cast<unsigned>(gLatestChannels[i]));
  }
  if (gLatestChannelCount >= 3) {
    // BoxPro+ firmware writes CH1=pan, CH2=roll, CH3=tilt.
    Serial.printf(" | pan=%u roll=%u tilt=%u", static_cast<unsigned>(gLatestChannels[0]),
                  static_cast<unsigned>(gLatestChannels[1]), static_cast<unsigned>(gLatestChannels[2]));
  }
  Serial.println();
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
  Serial.begin(115200);
  delay(300);
  Serial.println("Boot: hdzero-headtracker-monitor");
  Serial.printf("Listening for PPM on GPIO%u (rising-edge decode)\n", static_cast<unsigned>(PPM_INPUT_PIN));
  Serial.println("Wire: BoxPro jack tip -> GPIO, sleeve -> GND");
  attachInterrupt(digitalPinToInterrupt(PPM_INPUT_PIN), onPpmRise, RISING);
}

void loop() {
  copyFrameFromIsr();
  updateLed();

  const uint32_t nowMs = millis();
  if ((nowMs - gLastFrameMs) > kSignalTimeoutMs && (nowMs - gLastPrintMs) > 1000) {
    gLastPrintMs = nowMs;
    Serial.println("No PPM frame detected yet. Check HT enabled + wiring.");
  }
}
