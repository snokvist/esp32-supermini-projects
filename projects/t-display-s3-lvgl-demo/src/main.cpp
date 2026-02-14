#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>

#define GFX_BL 38
#define BTN_BOOT_PIN 0
#define BTN_USER_PIN 14

constexpr int kScreenW = 320;
constexpr int kScreenH = 170;
constexpr int kBarW = 296;

// T-Display-S3 bus/panel mapping from LilyGO Arduino_GFX demo.
Arduino_DataBus *bus = new Arduino_ESP32PAR8Q(
    7 /* DC */, 6 /* CS */, 8 /* WR */, 9 /* RD */,
    39 /* D0 */, 40 /* D1 */, 41 /* D2 */, 42 /* D3 */,
    45 /* D4 */, 46 /* D5 */, 47 /* D6 */, 48 /* D7 */);

Arduino_GFX *gfx = new Arduino_ST7789(
    bus, 5 /* RST */, 0 /* rotation */, true /* IPS */,
    170 /* width */, 320 /* height */, 35 /* col offset1 */, 0 /* row offset1 */,
    35 /* col offset2 */, 0 /* row offset2 */);

static lv_disp_draw_buf_t drawBuf;
static lv_color_t drawBufMem[kScreenW * 20];

static lv_obj_t *bar = nullptr;
static lv_obj_t *labelStatus1 = nullptr;
static lv_obj_t *labelStatus2 = nullptr;
static lv_obj_t *canvasIcon = nullptr;
static lv_obj_t *iconLabel = nullptr;

static uint32_t lastUiTickMs = 0;
static uint32_t lastStatsMs = 0;
static uint32_t frame = 0;
static uint32_t uiUpdateCount = 0;
static uint64_t uiBusyAccumUs = 0;
static float uiRefreshHz = 0.0f;
static float cpuPercent = 0.0f;

static bool bootPressed = false;
static bool userPressed = false;
static bool prevBootPressed = false;
static bool prevUserPressed = false;
static uint32_t bootPressCount = 0;
static uint32_t userPressCount = 0;

static lv_color_t iconBuf[16 * 16];

static void flushDisplay(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *colorP) {
  const int32_t w = area->x2 - area->x1 + 1;
  const int32_t h = area->y2 - area->y1 + 1;
  gfx->draw16bitRGBBitmap(area->x1, area->y1, reinterpret_cast<uint16_t *>(&colorP->full), w, h);
  lv_disp_flush_ready(disp);
}

static void sampleButtons() {
  bootPressed = (digitalRead(BTN_BOOT_PIN) == LOW);
  userPressed = (digitalRead(BTN_USER_PIN) == LOW);

  if (bootPressed && !prevBootPressed) {
    bootPressCount++;
    Serial.printf("BOOT press #%lu\n", static_cast<unsigned long>(bootPressCount));
  }
  if (userPressed && !prevUserPressed) {
    userPressCount++;
    Serial.printf("GPIO14 press #%lu\n", static_cast<unsigned long>(userPressCount));
  }

  prevBootPressed = bootPressed;
  prevUserPressed = userPressed;
}

static void drawIconToCanvas() {
  lv_canvas_set_buffer(canvasIcon, iconBuf, 16, 16, LV_IMG_CF_TRUE_COLOR);
  lv_canvas_fill_bg(canvasIcon, lv_palette_main(LV_PALETTE_AMBER), LV_OPA_COVER);

  // Simple face pixels.
  for (int y = 0; y < 16; ++y) {
    for (int x = 0; x < 16; ++x) {
      const bool border = (x == 0 || y == 0 || x == 15 || y == 15);
      if (border) {
        lv_canvas_set_px(canvasIcon, x, y, lv_color_white());
      }
    }
  }
  lv_canvas_set_px(canvasIcon, 4, 5, lv_color_black());
  lv_canvas_set_px(canvasIcon, 11, 5, lv_color_black());
  for (int x = 4; x <= 11; ++x) {
    if (x != 7 && x != 8) {
      lv_canvas_set_px(canvasIcon, x, 11, lv_color_black());
    }
  }
}

static void createUi() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_text_color(scr, lv_color_white(), 0);

  lv_obj_t *title = lv_label_create(scr);
  lv_obj_set_style_text_color(title, lv_palette_main(LV_PALETTE_CYAN), 0);
  lv_label_set_text(title, "T-Display-S3 LVGL");
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 8, 8);

  lv_obj_t *subtitle = lv_label_create(scr);
  lv_label_set_text(subtitle, "LVGL + Arduino_GFX bridge");
  lv_obj_align(subtitle, LV_ALIGN_TOP_LEFT, 8, 30);

  lv_obj_t *panel = lv_obj_create(scr);
  lv_obj_set_size(panel, 304, 82);
  lv_obj_align(panel, LV_ALIGN_TOP_LEFT, 8, 52);
  lv_obj_set_style_bg_color(panel, lv_color_hex(0x1863), 0);
  lv_obj_set_style_border_color(panel, lv_palette_main(LV_PALETTE_BLUE), 0);
  lv_obj_set_style_radius(panel, 0, 0);
  lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

  canvasIcon = lv_canvas_create(scr);
  lv_obj_align(canvasIcon, LV_ALIGN_TOP_LEFT, 24, 88);
  drawIconToCanvas();

  iconLabel = lv_label_create(scr);
  lv_obj_set_style_text_color(iconLabel, lv_palette_main(LV_PALETTE_YELLOW), 0);
  lv_label_set_text(iconLabel, "Hello from LVGL");
  lv_obj_align(iconLabel, LV_ALIGN_TOP_LEFT, 52, 92);

  bar = lv_bar_create(scr);
  lv_obj_set_size(bar, kBarW, 10);
  lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 12, 120);
  lv_bar_set_range(bar, 0, kBarW);
  lv_bar_set_value(bar, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(bar, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, lv_palette_main(LV_PALETTE_GREEN), LV_PART_INDICATOR);
  lv_obj_set_style_border_width(bar, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(bar, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(bar, 0, LV_PART_INDICATOR);

  labelStatus1 = lv_label_create(scr);
  lv_label_set_text(labelStatus1, "Uptime: 0s  Ref:0.0Hz");
  lv_obj_align(labelStatus1, LV_ALIGN_TOP_LEFT, 8, 138);

  labelStatus2 = lv_label_create(scr);
  lv_label_set_text(labelStatus2, "CPU:0.0% B0:0(0) B14:0(0)");
  lv_obj_align(labelStatus2, LV_ALIGN_TOP_LEFT, 8, 152);
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

  lv_init();
  lv_disp_draw_buf_init(&drawBuf, drawBufMem, nullptr, static_cast<uint32_t>(kScreenW * 20));

  static lv_disp_drv_t dispDrv;
  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res = kScreenW;
  dispDrv.ver_res = kScreenH;
  dispDrv.flush_cb = flushDisplay;
  dispDrv.draw_buf = &drawBuf;
  lv_disp_drv_register(&dispDrv);

  createUi();
  lastUiTickMs = millis();
  lastStatsMs = millis();
  Serial.println("LVGL demo initialized.");
}

void loop() {
  const uint32_t workStartUs = micros();
  const uint32_t nowMs = millis();

  const uint32_t tickDelta = nowMs - lastUiTickMs;
  if (tickDelta > 0) {
    lv_tick_inc(tickDelta);
    lastUiTickMs = nowMs;
  }

  lv_timer_handler();

  static uint32_t lastStepMs = 0;
  if (nowMs - lastStepMs < 100) {
    return;
  }
  lastStepMs = nowMs;

  frame++;
  uiUpdateCount++;
  sampleButtons();

  const int fillW = static_cast<int>(frame % (kBarW + 1));
  lv_bar_set_value(bar, fillW, LV_ANIM_OFF);

  const lv_color_t barColor =
      (bootPressed || userPressed) ? lv_palette_main(LV_PALETTE_RED) : lv_palette_main(LV_PALETTE_GREEN);
  lv_obj_set_style_bg_color(bar, barColor, LV_PART_INDICATOR);

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

    static char line1[48];
    static char line2[64];
    snprintf(line1, sizeof(line1), "Uptime:%lus Ref:%4.1fHz",
             static_cast<unsigned long>(nowMs / 1000), uiRefreshHz);
    snprintf(line2, sizeof(line2), "CPU:%4.1f%% B0:%d(%lu) B14:%d(%lu)",
             cpuPercent,
             bootPressed ? 1 : 0, static_cast<unsigned long>(bootPressCount),
             userPressed ? 1 : 0, static_cast<unsigned long>(userPressCount));
    lv_label_set_text(labelStatus1, line1);
    lv_label_set_text(labelStatus2, line2);
  }
}
