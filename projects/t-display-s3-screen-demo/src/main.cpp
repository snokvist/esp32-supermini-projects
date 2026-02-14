#include <Arduino.h>
#include <Arduino_GFX_Library.h>

// LilyGO T-Display-S3 backlight control pin.
#define GFX_BL 38
#define BTN_BOOT_PIN 0
#define BTN_USER_PIN 14

// Pin map taken from LilyGO's Arduino_GFX demo for T-Display-S3.
Arduino_DataBus *bus = new Arduino_ESP32PAR8Q(
    7 /* DC */, 6 /* CS */, 8 /* WR */, 9 /* RD */,
    39 /* D0 */, 40 /* D1 */, 41 /* D2 */, 42 /* D3 */,
    45 /* D4 */, 46 /* D5 */, 47 /* D6 */, 48 /* D7 */);

Arduino_GFX *gfx = new Arduino_ST7789(
    bus, 5 /* RST */, 0 /* rotation */, true /* IPS */,
    170 /* width */, 320 /* height */, 35 /* col offset1 */, 0 /* row offset1 */,
    35 /* col offset2 */, 0 /* row offset2 */);

// 16x16 1-bit icon (pixel-art face) rendered as a bitmap.
static const uint8_t kIcon16x16[] PROGMEM = {
    0b00111100, 0b00111100,
    0b01000010, 0b01000010,
    0b10011001, 0b10011001,
    0b10100101, 0b10100101,
    0b10000001, 0b10000001,
    0b10100101, 0b10100101,
    0b10011001, 0b10011001,
    0b10000001, 0b10000001,
    0b10000001, 0b10000001,
    0b10011001, 0b10011001,
    0b10100101, 0b10100101,
    0b10000001, 0b10000001,
    0b10000001, 0b10000001,
    0b01000010, 0b01000010,
    0b00111100, 0b00111100,
    0b00000000, 0b00000000,
};

constexpr int kBarX = 12;
constexpr int kBarY = 128;
constexpr int kBarW = 296;
constexpr int kBarH = 10;
constexpr int kStatsY1 = 146;
constexpr int kStatsY2 = 156;

static uint32_t lastUiTickMs = 0;
static uint32_t lastStatsMs = 0;
static uint32_t frame = 0;
static uint32_t uiUpdateCount = 0;
static float uiRefreshHz = 0.0f;
static float cpuPercent = 0.0f;
static int prevFillW = 0;
static uint16_t prevBarColor = GREEN;
static uint64_t uiBusyAccumUs = 0;

static bool bootPressed = false;
static bool userPressed = false;
static bool prevBootPressed = false;
static bool prevUserPressed = false;
static uint32_t bootPressCount = 0;
static uint32_t userPressCount = 0;

static void sampleButtons() {
  bootPressed = (digitalRead(BTN_BOOT_PIN) == LOW);
  userPressed = (digitalRead(BTN_USER_PIN) == LOW);

  if (bootPressed && !prevBootPressed) {
    bootPressCount++;
    Serial.printf("BOOT button press #%lu\n", static_cast<unsigned long>(bootPressCount));
  }
  if (userPressed && !prevUserPressed) {
    userPressCount++;
    Serial.printf("GPIO14 button press #%lu\n", static_cast<unsigned long>(userPressCount));
  }

  prevBootPressed = bootPressed;
  prevUserPressed = userPressed;
}

static void drawStaticUi() {
  gfx->fillScreen(BLACK);

  gfx->setTextColor(CYAN);
  gfx->setTextSize(2);
  gfx->setCursor(8, 10);
  gfx->println("T-Display-S3");

  gfx->setTextColor(WHITE);
  gfx->setTextSize(1);
  gfx->setCursor(8, 36);
  gfx->println("Screen init + text + bitmap");

  gfx->drawRect(8, 58, 304, 84, BLUE);
  gfx->fillRect(10, 60, 300, 80, 0x18E3);

  gfx->drawBitmap(24, 96, kIcon16x16, 16, 16, YELLOW);
  gfx->setCursor(52, 98);
  gfx->setTextColor(YELLOW);
  gfx->println("Hello from ESP32-S3");

  gfx->drawRect(kBarX - 1, kBarY - 1, kBarW + 2, kBarH + 2, WHITE);
  gfx->fillRect(kBarX, kBarY, kBarW, kBarH, BLACK);

  gfx->setTextColor(WHITE, BLACK);
  gfx->setCursor(8, kStatsY1);
  gfx->print("Uptime: 0s  Ref: 0.0Hz        ");
  gfx->setCursor(8, kStatsY2);
  gfx->print("CPU: 0.0% B0:0(0) B14:0(0)    ");
}

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);
  pinMode(BTN_BOOT_PIN, INPUT_PULLUP);
  pinMode(BTN_USER_PIN, INPUT_PULLUP);

  if (!gfx->begin()) {
    Serial.println("Display init failed.");
    while (true) {
      delay(1000);
    }
  }

  gfx->setRotation(1);
  drawStaticUi();
  Serial.println("Display initialized.");
}

void loop() {
  const uint32_t workStartUs = micros();
  const uint32_t nowMs = millis();
  if (nowMs - lastUiTickMs < 100) {
    return;
  }
  lastUiTickMs = nowMs;
  frame++;
  uiUpdateCount++;
  sampleButtons();

  const int fillW = static_cast<int>(frame % (kBarW + 1));
  const uint16_t barColor = (bootPressed || userPressed) ? RED : GREEN;
  if (barColor != prevBarColor && prevFillW > 0) {
    gfx->fillRect(kBarX, kBarY, prevFillW, kBarH, barColor);
    prevBarColor = barColor;
  }

  if (fillW < prevFillW) {
    // Wrapped around: clear bar then draw the new segment.
    gfx->fillRect(kBarX, kBarY, kBarW, kBarH, BLACK);
    if (fillW > 0) {
      gfx->fillRect(kBarX, kBarY, fillW, kBarH, barColor);
    }
  } else if (fillW > prevFillW) {
    // Draw only newly added pixels to reduce visible flicker.
    gfx->fillRect(kBarX + prevFillW, kBarY, fillW - prevFillW, kBarH, barColor);
  }
  prevFillW = fillW;
  uiBusyAccumUs += static_cast<uint64_t>(micros() - workStartUs);

  const uint32_t statsDtMs = nowMs - lastStatsMs;
  if (statsDtMs >= 1000) {
    uiRefreshHz = (uiUpdateCount * 1000.0f) / static_cast<float>(statsDtMs);
    cpuPercent = (100.0f * static_cast<float>(uiBusyAccumUs)) / (static_cast<float>(statsDtMs) * 1000.0f);
    if (cpuPercent > 100.0f) {
      cpuPercent = 100.0f;
    }
    uiUpdateCount = 0;
    uiBusyAccumUs = 0;
    lastStatsMs = nowMs;

    gfx->setTextColor(WHITE, BLACK);
    gfx->setCursor(8, kStatsY1);
    gfx->printf("Uptime: %lus  Ref:%4.1fHz     ",
                static_cast<unsigned long>(nowMs / 1000), uiRefreshHz);
    gfx->setCursor(8, kStatsY2);
    gfx->printf("CPU:%4.1f%% B0:%d(%lu) B14:%d(%lu)   ",
                cpuPercent,
                bootPressed ? 1 : 0, static_cast<unsigned long>(bootPressCount),
                userPressed ? 1 : 0, static_cast<unsigned long>(userPressCount));
  }
}
