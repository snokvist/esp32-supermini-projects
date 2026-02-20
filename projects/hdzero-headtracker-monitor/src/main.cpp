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

#ifndef CRSF_UART_TX_PIN
#define CRSF_UART_TX_PIN 21
#endif

#ifndef CRSF_UART_RX_PIN
#define CRSF_UART_RX_PIN 20
#endif

#ifndef CRSF_UART_BAUD
#define CRSF_UART_BAUD 420000
#endif

#ifndef CRSF_UART_ENABLED
#define CRSF_UART_ENABLED 1
#endif

#ifndef SERVO_PWM_1_PIN
#define SERVO_PWM_1_PIN 3
#endif

#ifndef SERVO_PWM_2_PIN
#define SERVO_PWM_2_PIN 4
#endif

#ifndef SERVO_PWM_3_PIN
#define SERVO_PWM_3_PIN 5
#endif

#ifndef SERVO_PWM_FREQUENCY_HZ
#define SERVO_PWM_FREQUENCY_HZ 100
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
constexpr uint8_t kCrsfMaxFrameLength = 62;
constexpr uint16_t kCrsfMinValue = 172;
constexpr uint16_t kCrsfCenterValue = 992;
constexpr uint16_t kCrsfMaxValue = 1811;
constexpr uint16_t kCrsfMapMinUs = 1000;
constexpr uint16_t kCrsfMapMaxUs = 2000;
constexpr uint32_t kCrsfRxTimeoutMs = 500;

constexpr uint8_t kServoCount = 3;
constexpr uint8_t kServoPwmResolutionBits = 14;
constexpr uint16_t kServoPulseMinUs = 1000;
constexpr uint16_t kServoPulseCenterUs = 1500;
constexpr uint16_t kServoPulseMaxUs = 2000;

const uint8_t kServoPins[kServoCount] = {SERVO_PWM_1_PIN, SERVO_PWM_2_PIN, SERVO_PWM_3_PIN};
const uint8_t kServoPwmChannels[kServoCount] = {0, 1, 2};

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
HardwareSerial gCrsfUart(1);
bool gCrsfUartReady = false;
uint8_t gCrsfRxFrameBuffer[kCrsfMaxFrameLength + 2] = {0};
uint8_t gCrsfRxFramePos = 0;
uint8_t gCrsfRxFrameExpected = 0;
uint16_t gCrsfRxChannels[kCrsfChannelCount] = {0};
uint16_t gServoPulseUs[kServoCount] = {kServoPulseCenterUs, kServoPulseCenterUs, kServoPulseCenterUs};
uint32_t gCrsfRxPacketCounter = 0;
uint32_t gCrsfRxInvalidCounter = 0;
uint32_t gLastCrsfRxMs = 0;
bool gHasCrsfRxFrame = false;

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

uint16_t mapCrsfToServoUs(uint16_t crsfValue) {
  uint16_t clamped = crsfValue;
  if (clamped < kCrsfMinValue) {
    clamped = kCrsfMinValue;
  } else if (clamped > kCrsfMaxValue) {
    clamped = kCrsfMaxValue;
  }
  const uint32_t numerator =
      static_cast<uint32_t>(clamped - kCrsfMinValue) * static_cast<uint32_t>(kServoPulseMaxUs - kServoPulseMinUs);
  const uint32_t denominator = static_cast<uint32_t>(kCrsfMaxValue - kCrsfMinValue);
  return static_cast<uint16_t>(kServoPulseMinUs + ((numerator + (denominator / 2U)) / denominator));
}

uint32_t pulseUsToPwmDuty(uint16_t pulseUs) {
  const uint32_t periodUs = 1000000UL / SERVO_PWM_FREQUENCY_HZ;
  const uint32_t maxDuty = (1UL << kServoPwmResolutionBits) - 1UL;
  return (static_cast<uint32_t>(pulseUs) * maxDuty + (periodUs / 2UL)) / periodUs;
}

void setupServoOutputs() {
  for (uint8_t i = 0; i < kServoCount; ++i) {
    ledcSetup(kServoPwmChannels[i], SERVO_PWM_FREQUENCY_HZ, kServoPwmResolutionBits);
    ledcAttachPin(kServoPins[i], kServoPwmChannels[i]);
    ledcWrite(kServoPwmChannels[i], pulseUsToPwmDuty(kServoPulseCenterUs));
  }
}

void writeServoPulseUs(uint8_t servoIndex, uint16_t pulseUs) {
  if (servoIndex >= kServoCount) {
    return;
  }
  gServoPulseUs[servoIndex] = pulseUs;
  ledcWrite(kServoPwmChannels[servoIndex], pulseUsToPwmDuty(pulseUs));
}

void updateServoOutputsFromCrsf() {
  const uint32_t nowMs = millis();
  const bool crsfFresh = gHasCrsfRxFrame && ((nowMs - gLastCrsfRxMs) <= kCrsfRxTimeoutMs);

  if (!crsfFresh) {
    for (uint8_t i = 0; i < kServoCount; ++i) {
      writeServoPulseUs(i, kServoPulseCenterUs);
    }
    return;
  }

  for (uint8_t i = 0; i < kServoCount; ++i) {
    writeServoPulseUs(i, mapCrsfToServoUs(gCrsfRxChannels[i]));
  }
}

bool decodeCrsfRcChannels(const uint8_t *payload) {
  uint32_t bitBuffer = 0;
  uint8_t bitsInBuffer = 0;
  uint8_t payloadIndex = 0;
  uint16_t decoded[kCrsfChannelCount] = {0};

  for (uint8_t i = 0; i < kCrsfChannelCount; ++i) {
    while (bitsInBuffer < 11) {
      if (payloadIndex >= kCrsfPayloadSize) {
        return false;
      }
      bitBuffer |= static_cast<uint32_t>(payload[payloadIndex++]) << bitsInBuffer;
      bitsInBuffer += 8;
    }
    decoded[i] = static_cast<uint16_t>(bitBuffer & 0x07FFU);
    bitBuffer >>= 11;
    bitsInBuffer -= 11;
  }

  for (uint8_t i = 0; i < kCrsfChannelCount; ++i) {
    gCrsfRxChannels[i] = decoded[i];
  }
  gLastCrsfRxMs = millis();
  gCrsfRxPacketCounter++;
  gHasCrsfRxFrame = true;
  return true;
}

void processCrsfFrame(const uint8_t *frame, uint8_t frameSize) {
  if (frameSize < 4) {
    gCrsfRxInvalidCounter++;
    return;
  }

  const uint8_t length = frame[1];
  if (length < 2 || length > kCrsfMaxFrameLength || frameSize != static_cast<uint8_t>(length + 2)) {
    gCrsfRxInvalidCounter++;
    return;
  }

  const uint8_t expectedCrc = frame[1 + length];
  const uint8_t computedCrc = crc8DvbS2(&frame[2], length - 1);
  if (expectedCrc != computedCrc) {
    gCrsfRxInvalidCounter++;
    return;
  }

  const uint8_t type = frame[2];
  if (type == kCrsfFrameTypeRcChannelsPacked && length == static_cast<uint8_t>(kCrsfPayloadSize + 2)) {
    if (!decodeCrsfRcChannels(&frame[3])) {
      gCrsfRxInvalidCounter++;
    }
  }
}

void processCrsfRxByte(uint8_t byteIn) {
  if (gCrsfRxFramePos == 0) {
    gCrsfRxFrameBuffer[gCrsfRxFramePos++] = byteIn;
    return;
  }

  if (gCrsfRxFramePos == 1) {
    if (byteIn < 2 || byteIn > kCrsfMaxFrameLength) {
      gCrsfRxFramePos = 0;
      gCrsfRxInvalidCounter++;
      return;
    }
    gCrsfRxFrameBuffer[gCrsfRxFramePos++] = byteIn;
    gCrsfRxFrameExpected = static_cast<uint8_t>(byteIn + 2);
    return;
  }

  if (gCrsfRxFramePos >= sizeof(gCrsfRxFrameBuffer)) {
    gCrsfRxFramePos = 0;
    gCrsfRxInvalidCounter++;
    return;
  }

  gCrsfRxFrameBuffer[gCrsfRxFramePos++] = byteIn;
  if (gCrsfRxFrameExpected > 0 && gCrsfRxFramePos == gCrsfRxFrameExpected) {
    processCrsfFrame(gCrsfRxFrameBuffer, gCrsfRxFrameExpected);
    gCrsfRxFramePos = 0;
    gCrsfRxFrameExpected = 0;
  }
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
  if (gCrsfUartReady) {
    gCrsfUart.write(packet, sizeof(packet));
  }
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
  const uint32_t crsfRxAgeMs = gHasCrsfRxFrame ? (nowMs - gLastCrsfRxMs) : 0;
  const bool crsfRxFresh = gHasCrsfRxFrame && (crsfRxAgeMs <= kCrsfRxTimeoutMs);

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
  Serial.printf(" | crsf_rx=%s age=%lums pkts=%lu bad=%lu",
                crsfRxFresh ? "ok" : (gHasCrsfRxFrame ? "stale" : "none"),
                static_cast<unsigned long>(crsfRxAgeMs), static_cast<unsigned long>(gCrsfRxPacketCounter),
                static_cast<unsigned long>(gCrsfRxInvalidCounter));
  Serial.printf(" | servo=%u,%u,%u", static_cast<unsigned>(gServoPulseUs[0]), static_cast<unsigned>(gServoPulseUs[1]),
                static_cast<unsigned>(gServoPulseUs[2]));
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

void drainCrsfRxInput() {
#if CRSF_UART_ENABLED
  if (!gCrsfUartReady) {
    return;
  }
  while (gCrsfUart.available() > 0) {
    const int byteRead = gCrsfUart.read();
    if (byteRead >= 0) {
      processCrsfRxByte(static_cast<uint8_t>(byteRead));
    }
  }
#endif
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
#if CRSF_UART_ENABLED
  gCrsfUart.begin(CRSF_UART_BAUD, SERIAL_8N1, CRSF_UART_RX_PIN, CRSF_UART_TX_PIN);
  gCrsfUartReady = true;
#endif
  setupServoOutputs();
  gButtonRawState = digitalRead(MODE_BUTTON_PIN);
  gButtonStableState = gButtonRawState;
  gLastButtonChangeMs = millis();

  attachInterrupt(digitalPinToInterrupt(PPM_INPUT_PIN), onPpmRise, RISING);
}

void loop() {
  uint32_t invalidPulses = 0;
  const bool frameReady = copyFrameFromIsr(invalidPulses);
  drainCrsfRxInput();
  updateServoOutputsFromCrsf();
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
