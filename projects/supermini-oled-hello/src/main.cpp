#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <Wire.h>

namespace {
constexpr int kScreenWidth = 128;
constexpr int kScreenHeight = 64;
constexpr uint8_t kI2cAddress = 0x3C;
constexpr uint8_t kSdaPin = 4;
constexpr uint8_t kSclPin = 5;
constexpr uint8_t kModeButtonPin = 9;  // BOOT button on ESP32-C3 SuperMini (active-low)
constexpr uint32_t kI2cClockHz = 400000;
constexpr uint32_t kUptimeRefreshMs = 1000;
constexpr uint32_t kGraphFrameIntervalMs = 33;  // ~30 FPS
constexpr uint32_t kPixelFlashIntervalMs = 2000;
constexpr uint32_t kRecoveryFrameIntervalMs = 25;  // ~40 FPS target (I2C-limited)
constexpr uint32_t kButtonDebounceMs = 40;
constexpr uint8_t kBarCount = 16;
constexpr int16_t kGraphLeft = 6;
constexpr int16_t kGraphTop = 16;
constexpr int16_t kGraphBottom = 61;
constexpr int16_t kBarWidth = 6;
constexpr int16_t kBarGap = 1;
constexpr int16_t kGraphMaxHeight = (kGraphBottom - kGraphTop + 1);

Adafruit_SSD1306 gDisplay(kScreenWidth, kScreenHeight, &Wire, -1);

enum class DisplayMode : uint8_t {
  kHello = 0,
  kGraph = 1,
  kPixelTest = 2,
  kRecovery = 3
};

DisplayMode gDisplayMode = DisplayMode::kHello;
unsigned long gLastHelloDrawMs = 0;
unsigned long gLastGraphFrameMs = 0;
unsigned long gGraphStatsStartMs = 0;
uint32_t gGraphFramesInWindow = 0;
float gGraphMeasuredFps = 0.0f;
int16_t gBarHeights[kBarCount] = {0};
int16_t gBarPeaks[kBarCount] = {0};
unsigned long gLastPixelFlipMs = 0;
bool gPixelFlashOn = false;
unsigned long gLastRecoveryFrameMs = 0;
unsigned long gRecoveryStatsStartMs = 0;
uint32_t gRecoveryFramesInWindow = 0;
float gRecoveryMeasuredFps = 0.0f;

bool gButtonStablePressed = false;
bool gButtonLastReadingPressed = false;
unsigned long gButtonLastChangeMs = 0;

void drawCenteredText(const char *text, int16_t y, uint8_t size) {
  int16_t x1 = 0;
  int16_t y1 = 0;
  uint16_t w = 0;
  uint16_t h = 0;
  gDisplay.setTextSize(size);
  gDisplay.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  int16_t x = (kScreenWidth - static_cast<int16_t>(w)) / 2;
  if (x < 0) {
    x = 0;
  }
  gDisplay.setCursor(x, y);
  gDisplay.print(text);
}

void drawHelloFrame() {
  gDisplay.clearDisplay();
  gDisplay.setTextColor(SSD1306_WHITE);
  gDisplay.drawRect(0, 0, kScreenWidth, kScreenHeight, SSD1306_WHITE);
  gDisplay.setTextSize(1);
  gDisplay.setCursor(3, 3);
  gDisplay.print("HELLO MODE");
  drawCenteredText("Hello,", 18, 2);
  drawCenteredText("World!", 38, 2);
}

void drawHelloUptime(unsigned long nowMs) {
  gDisplay.fillRect(2, 2, kScreenWidth - 4, 10, SSD1306_BLACK);
  gDisplay.setTextSize(1);
  gDisplay.setTextColor(SSD1306_WHITE);
  gDisplay.setCursor(4, 4);
  gDisplay.printf("Uptime: %lus", nowMs / 1000UL);
  gDisplay.display();
}

void seedGraphBars() {
  for (uint8_t i = 0; i < kBarCount; ++i) {
    const int16_t level = static_cast<int16_t>(random(4, kGraphMaxHeight - 2));
    gBarHeights[i] = level;
    gBarPeaks[i] = level;
  }
}

void updateGraphBars(unsigned long nowMs) {
  for (uint8_t i = 0; i < kBarCount; ++i) {
    const uint32_t phase = (nowMs / 7UL) + static_cast<uint32_t>(i) * 73UL;
    int16_t target = static_cast<int16_t>((phase % (kGraphMaxHeight - 2)) + 2);

    // Add occasional spikes to mimic audio meter transients.
    if ((random(0, 100) < 12) && (target < kGraphMaxHeight - 4)) {
      target += static_cast<int16_t>(random(4, 10));
    }
    if (target > kGraphMaxHeight) {
      target = kGraphMaxHeight;
    }

    int16_t level = gBarHeights[i];
    if (target > level) {
      const int16_t step = static_cast<int16_t>((target - level) / 2);
      level += (step > 0) ? step : 1;
    } else if (target < level) {
      const int16_t step = static_cast<int16_t>((level - target) / 3);
      level -= (step > 0) ? step : 1;
    }

    if (level < 1) {
      level = 1;
    } else if (level > kGraphMaxHeight) {
      level = kGraphMaxHeight;
    }
    gBarHeights[i] = level;

    if (level >= gBarPeaks[i]) {
      gBarPeaks[i] = level;
    } else if (gBarPeaks[i] > 0) {
      gBarPeaks[i]--;
    }
  }
}

void drawGraphFrame(unsigned long nowMs) {
  updateGraphBars(nowMs);

  gDisplay.clearDisplay();
  gDisplay.drawRect(0, 0, kScreenWidth, kScreenHeight, SSD1306_WHITE);
  gDisplay.setTextSize(1);
  gDisplay.setTextColor(SSD1306_WHITE);
  gDisplay.setCursor(3, 3);
  gDisplay.print("GRAPH ~30FPS");
  gDisplay.setCursor(90, 3);
  gDisplay.printf("%2.1f", gGraphMeasuredFps);

  gDisplay.drawFastHLine(kGraphLeft - 1, kGraphBottom, 116, SSD1306_WHITE);

  for (uint8_t i = 0; i < kBarCount; ++i) {
    const int16_t x = kGraphLeft + i * (kBarWidth + kBarGap);
    const int16_t barHeight = gBarHeights[i];
    const int16_t y = kGraphBottom - barHeight + 1;
    gDisplay.fillRect(x, y, kBarWidth, barHeight, SSD1306_WHITE);

    const int16_t peakY = kGraphBottom - gBarPeaks[i];
    gDisplay.drawFastHLine(x, peakY, kBarWidth, SSD1306_WHITE);
  }

  gDisplay.display();
  gGraphFramesInWindow++;

  const unsigned long statsDtMs = nowMs - gGraphStatsStartMs;
  if (statsDtMs >= 1000) {
    gGraphMeasuredFps = (gGraphFramesInWindow * 1000.0f) / static_cast<float>(statsDtMs);
    Serial.printf("Graph FPS: %.1f\n", gGraphMeasuredFps);
    gGraphFramesInWindow = 0;
    gGraphStatsStartMs = nowMs;
  }
}

void drawPixelFlashFrame(bool screenOn) {
  gDisplay.clearDisplay();
  if (screenOn) {
    gDisplay.fillRect(0, 0, kScreenWidth, kScreenHeight, SSD1306_WHITE);
  }
  gDisplay.display();
}

void drawRecoveryFrame(unsigned long nowMs) {
  const uint8_t pattern = static_cast<uint8_t>((nowMs / 240UL) % 4UL);
  gDisplay.clearDisplay();

  if (pattern == 0) {
    gDisplay.fillRect(0, 0, kScreenWidth, kScreenHeight, SSD1306_WHITE);
  } else if (pattern == 1) {
    // Full black frame (already clear).
  } else if (pattern == 2) {
    const uint8_t offset = static_cast<uint8_t>((nowMs / 60UL) & 1UL);
    for (int16_t y = 0; y < kScreenHeight; ++y) {
      const int16_t startX = ((y + offset) & 1) ? 1 : 0;
      for (int16_t x = startX; x < kScreenWidth; x += 2) {
        gDisplay.drawPixel(x, y, SSD1306_WHITE);
      }
    }
  } else {
    for (int16_t y = 0; y < kScreenHeight; y += 2) {
      for (int16_t x = 0; x < kScreenWidth; x += 2) {
        if (random(0, 100) < 50) {
          gDisplay.fillRect(x, y, 2, 2, SSD1306_WHITE);
        }
      }
    }
  }

  gDisplay.display();
  gRecoveryFramesInWindow++;

  const unsigned long statsDtMs = nowMs - gRecoveryStatsStartMs;
  if (statsDtMs >= 1000) {
    gRecoveryMeasuredFps = (gRecoveryFramesInWindow * 1000.0f) / static_cast<float>(statsDtMs);
    Serial.printf("Recovery FPS: %.1f (pattern=%u)\n", gRecoveryMeasuredFps, pattern);
    gRecoveryFramesInWindow = 0;
    gRecoveryStatsStartMs = nowMs;
  }
}

void setDisplayMode(DisplayMode mode, const char *source, unsigned long nowMs) {
  gDisplayMode = mode;
  if (gDisplayMode == DisplayMode::kHello) {
    drawHelloFrame();
    drawHelloUptime(nowMs);
    gLastHelloDrawMs = nowMs;
    Serial.printf("Mode -> HELLO (%s)\n", source);
    return;
  }

  if (gDisplayMode == DisplayMode::kGraph) {
    seedGraphBars();
    gGraphMeasuredFps = 0.0f;
    gGraphFramesInWindow = 0;
    gGraphStatsStartMs = nowMs;
    gLastGraphFrameMs = nowMs;
    drawGraphFrame(nowMs);
    Serial.printf("Mode -> GRAPH (%s)\n", source);
    return;
  }

  gPixelFlashOn = true;
  if (gDisplayMode == DisplayMode::kPixelTest) {
    gLastPixelFlipMs = nowMs;
    drawPixelFlashFrame(gPixelFlashOn);
    Serial.printf("Mode -> PIXEL_TEST (%s)\n", source);
    Serial.println("Pixel flash: ON");
    return;
  }

  gRecoveryMeasuredFps = 0.0f;
  gRecoveryFramesInWindow = 0;
  gRecoveryStatsStartMs = nowMs;
  gLastRecoveryFrameMs = nowMs;
  drawRecoveryFrame(nowMs);
  Serial.printf("Mode -> RECOVERY (%s)\n", source);
  Serial.println("Recovery mode: fast invert/checker/noise pixel exercise");
}

void toggleDisplayMode(const char *source, unsigned long nowMs) {
  if (gDisplayMode == DisplayMode::kHello) {
    setDisplayMode(DisplayMode::kGraph, source, nowMs);
    return;
  }
  if (gDisplayMode == DisplayMode::kGraph) {
    setDisplayMode(DisplayMode::kPixelTest, source, nowMs);
    return;
  }
  if (gDisplayMode == DisplayMode::kPixelTest) {
    setDisplayMode(DisplayMode::kRecovery, source, nowMs);
    return;
  }
  setDisplayMode(DisplayMode::kHello, source, nowMs);
}

void pollModeButton(unsigned long nowMs) {
  const bool pressed = (digitalRead(kModeButtonPin) == LOW);
  if (pressed != gButtonLastReadingPressed) {
    gButtonLastReadingPressed = pressed;
    gButtonLastChangeMs = nowMs;
  }

  if ((nowMs - gButtonLastChangeMs) < kButtonDebounceMs) {
    return;
  }

  if (pressed != gButtonStablePressed) {
    gButtonStablePressed = pressed;
    if (gButtonStablePressed) {
      toggleDisplayMode("button", nowMs);
    }
  }
}

void pollSerialCommands(unsigned long nowMs) {
  while (Serial.available()) {
    const int value = Serial.read();
    if (value == 't' || value == 'T') {
      toggleDisplayMode("serial", nowMs);
    }
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.println("Boot: supermini-oled-hello");
  Serial.printf("I2C pins: SDA=%u, SCL=%u, addr=0x%02X\n", kSdaPin, kSclPin, kI2cAddress);
  Serial.printf("Mode button: GPIO%u (active-low)\n", kModeButtonPin);

  Wire.begin(kSdaPin, kSclPin);
  Wire.setClock(kI2cClockHz);

  if (!gDisplay.begin(SSD1306_SWITCHCAPVCC, kI2cAddress)) {
    Serial.println("ERROR: SSD1306 init failed. Check wiring, power, and I2C address.");
    while (true) {
      delay(1000);
    }
  }

  pinMode(kModeButtonPin, INPUT_PULLUP);
  gButtonStablePressed = (digitalRead(kModeButtonPin) == LOW);
  gButtonLastReadingPressed = gButtonStablePressed;
  gButtonLastChangeMs = millis();
  randomSeed(static_cast<uint32_t>(micros()));

  const unsigned long now = millis();
  setDisplayMode(DisplayMode::kHello, "boot", now);
  Serial.println("Press BOOT to cycle modes: HELLO -> GRAPH -> PIXEL_TEST -> RECOVERY.");
  Serial.println("Serial 't' also cycles modes.");
}

void loop() {
  const unsigned long now = millis();
  pollModeButton(now);
  pollSerialCommands(now);

  if (gDisplayMode == DisplayMode::kHello) {
    if (now - gLastHelloDrawMs >= kUptimeRefreshMs) {
      gLastHelloDrawMs = now;
      drawHelloUptime(now);
      Serial.printf("Uptime: %lus\n", now / 1000UL);
    }
    return;
  }

  if (gDisplayMode == DisplayMode::kGraph) {
    if (now - gLastGraphFrameMs >= kGraphFrameIntervalMs) {
      gLastGraphFrameMs = now;
      drawGraphFrame(now);
    }
    return;
  }

  if (gDisplayMode == DisplayMode::kPixelTest) {
    if (now - gLastPixelFlipMs >= kPixelFlashIntervalMs) {
      gLastPixelFlipMs = now;
      gPixelFlashOn = !gPixelFlashOn;
      drawPixelFlashFrame(gPixelFlashOn);
      Serial.printf("Pixel flash: %s\n", gPixelFlashOn ? "ON" : "OFF");
    }
    return;
  }

  if (now - gLastRecoveryFrameMs >= kRecoveryFrameIntervalMs) {
    gLastRecoveryFrameMs = now;
    drawRecoveryFrame(now);
  }
}
