#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>

#define GFX_BL 38
#define BTN_BOOT_PIN 0
#define BTN_USER_PIN 14

constexpr int kScreenW = 320;
constexpr int kScreenH = 170;
constexpr int kTabCount = 3;
constexpr uint32_t kLongPressMs = 1000;

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

static lv_obj_t *tabview = nullptr;
static lv_obj_t *tabDash = nullptr;
static lv_obj_t *tabInputs = nullptr;
static lv_obj_t *tabSystem = nullptr;

static lv_obj_t *cardDash = nullptr;
static lv_obj_t *cardInputs = nullptr;
static lv_obj_t *cardSystem = nullptr;

static lv_obj_t *barLoad = nullptr;
static lv_obj_t *arcCpu = nullptr;
static lv_obj_t *labelArcCpu = nullptr;
static lv_obj_t *labelDash1 = nullptr;
static lv_obj_t *labelDash2 = nullptr;

static lv_obj_t *ledBoot = nullptr;
static lv_obj_t *ledUser = nullptr;
static lv_obj_t *labelInputState = nullptr;
static lv_obj_t *labelInputCounts = nullptr;
static lv_obj_t *swAccent = nullptr;

static lv_obj_t *barRefresh = nullptr;
static lv_obj_t *barCpu = nullptr;
static lv_obj_t *labelSys1 = nullptr;
static lv_obj_t *labelSys2 = nullptr;
static lv_obj_t *labelSys3 = nullptr;

static uint32_t lastUiTickMs = 0;
static uint32_t lastStatsMs = 0;
static uint32_t lastStepMs = 0;
static uint32_t frame = 0;
static uint32_t uiUpdateCount = 0;
static uint64_t uiBusyAccumUs = 0;

static float uiRefreshHz = 0.0f;
static float cpuPercent = 0.0f;
static int activeTab = 0;
static bool altAccent = false;

static bool bootPressed = false;
static bool userPressed = false;
static bool bootShortEdge = false;
static bool userShortEdge = false;
static bool bootLongEdge = false;
static bool userLongEdge = false;
static bool bootLongFired = false;
static bool userLongFired = false;
static uint32_t bootPressStartMs = 0;
static uint32_t userPressStartMs = 0;
static uint32_t bootPressCount = 0;
static uint32_t userPressCount = 0;

static lv_color_t accentColor() {
  return altAccent ? lv_palette_main(LV_PALETTE_ORANGE) : lv_palette_main(LV_PALETTE_CYAN);
}

static void flushDisplay(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *colorP) {
  const int32_t w = area->x2 - area->x1 + 1;
  const int32_t h = area->y2 - area->y1 + 1;
  gfx->draw16bitRGBBitmap(area->x1, area->y1, reinterpret_cast<uint16_t *>(&colorP->full), w, h);
  lv_disp_flush_ready(disp);
}

static void sampleButtons(uint32_t nowMs) {
  const bool bootNow = (digitalRead(BTN_BOOT_PIN) == LOW);
  const bool userNow = (digitalRead(BTN_USER_PIN) == LOW);

  const bool bootPressEdge = (bootNow && !bootPressed);
  const bool userPressEdge = (userNow && !userPressed);
  const bool bootReleaseEdge = (!bootNow && bootPressed);
  const bool userReleaseEdge = (!userNow && userPressed);

  bootShortEdge = false;
  userShortEdge = false;
  bootLongEdge = false;
  userLongEdge = false;

  if (bootPressEdge) {
    bootPressStartMs = nowMs;
    bootLongFired = false;
    bootPressCount++;
    Serial.printf("BOOT press #%lu\n", static_cast<unsigned long>(bootPressCount));
  }
  if (userPressEdge) {
    userPressStartMs = nowMs;
    userLongFired = false;
    userPressCount++;
    Serial.printf("GPIO14 press #%lu\n", static_cast<unsigned long>(userPressCount));
  }

  if (bootNow && !bootLongFired && (nowMs - bootPressStartMs >= kLongPressMs)) {
    bootLongFired = true;
    bootLongEdge = true;
    Serial.println("BOOT long press (>1s)");
  }
  if (userNow && !userLongFired && (nowMs - userPressStartMs >= kLongPressMs)) {
    userLongFired = true;
    userLongEdge = true;
    Serial.println("GPIO14 long press (>1s)");
  }

  if (bootReleaseEdge && !bootLongFired) {
    bootShortEdge = true;
  }
  if (userReleaseEdge && !userLongFired) {
    userShortEdge = true;
  }

  if (bootReleaseEdge) {
    bootLongFired = false;
  }
  if (userReleaseEdge) {
    userLongFired = false;
  }

  bootPressed = bootNow;
  userPressed = userNow;
}

static void setActiveTab(int nextTab) {
  while (nextTab < 0) {
    nextTab += kTabCount;
  }
  activeTab = nextTab % kTabCount;
  lv_tabview_set_act(tabview, static_cast<uint32_t>(activeTab), LV_ANIM_OFF);
}

static void styleCard(lv_obj_t *obj) {
  lv_obj_set_style_bg_color(obj, lv_color_hex(0x1D2333), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(obj, 2, LV_PART_MAIN);
  lv_obj_set_style_radius(obj, 6, LV_PART_MAIN);
  lv_obj_set_style_pad_all(obj, 8, LV_PART_MAIN);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

static void styleTabPage(lv_obj_t *tab) {
  lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(tab, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_pad_all(tab, 0, LV_PART_MAIN);
}

static void applyAccentStyles() {
  const lv_color_t accent = accentColor();
  const lv_color_t accentSoft = lv_color_mix(accent, lv_color_white(), LV_OPA_40);

  lv_obj_t *tabBtns = lv_tabview_get_tab_btns(tabview);
  lv_obj_set_style_bg_color(tabBtns, lv_color_hex(0x0D1422), LV_PART_MAIN);
  lv_obj_set_style_border_width(tabBtns, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(tabBtns, 4, LV_PART_MAIN);
  lv_obj_set_style_text_color(tabBtns, lv_color_hex(0xBFCBE6), LV_PART_ITEMS);
  lv_obj_set_style_bg_color(tabBtns, accentSoft, LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_text_color(tabBtns, lv_color_black(), LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_radius(tabBtns, 3, LV_PART_ITEMS);
  lv_obj_set_style_pad_left(tabBtns, 3, LV_PART_MAIN);
  lv_obj_set_style_pad_right(tabBtns, 3, LV_PART_MAIN);
  lv_obj_set_scrollbar_mode(tabBtns, LV_SCROLLBAR_MODE_OFF);

  lv_obj_set_style_border_color(cardDash, accentSoft, LV_PART_MAIN);
  lv_obj_set_style_border_color(cardInputs, accentSoft, LV_PART_MAIN);
  lv_obj_set_style_border_color(cardSystem, accentSoft, LV_PART_MAIN);
  lv_obj_set_style_bg_color(barLoad, accent, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arcCpu, accent, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(barRefresh, accent, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(barCpu, accent, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(swAccent, accent, LV_PART_INDICATOR | LV_STATE_CHECKED);

  if (altAccent) {
    lv_obj_add_state(swAccent, LV_STATE_CHECKED);
  } else {
    lv_obj_clear_state(swAccent, LV_STATE_CHECKED);
  }
}

static void createDashboardTab() {
  cardDash = lv_obj_create(tabDash);
  lv_obj_set_size(cardDash, 296, 116);
  lv_obj_align(cardDash, LV_ALIGN_TOP_MID, 0, 6);
  styleCard(cardDash);

  lv_obj_t *title = lv_label_create(cardDash);
  lv_label_set_text(title, "Live Dashboard");
  lv_obj_set_style_text_color(title, lv_color_hex(0xE6EEF9), 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 6, 0);

  arcCpu = lv_arc_create(cardDash);
  lv_obj_set_size(arcCpu, 58, 58);
  lv_obj_align(arcCpu, LV_ALIGN_TOP_LEFT, 8, 24);
  lv_arc_set_bg_angles(arcCpu, 135, 45);
  lv_arc_set_rotation(arcCpu, 135);
  lv_arc_set_range(arcCpu, 0, 100);
  lv_arc_set_value(arcCpu, 0);
  lv_obj_set_style_arc_width(arcCpu, 7, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arcCpu, 7, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arcCpu, lv_color_hex(0x3B455A), LV_PART_MAIN);
  lv_obj_remove_style(arcCpu, nullptr, LV_PART_KNOB);

  labelArcCpu = lv_label_create(cardDash);
  lv_label_set_text(labelArcCpu, "0%");
  lv_obj_align_to(labelArcCpu, arcCpu, LV_ALIGN_CENTER, 0, 0);

  labelDash1 = lv_label_create(cardDash);
  lv_label_set_text(labelDash1, "Ref:0.0Hz Up:0s");
  lv_obj_align(labelDash1, LV_ALIGN_TOP_LEFT, 82, 30);

  labelDash2 = lv_label_create(cardDash);
  lv_label_set_text(labelDash2, "CPU:0.0% Tab:1/3");
  lv_obj_align(labelDash2, LV_ALIGN_TOP_LEFT, 82, 50);

  barLoad = lv_bar_create(cardDash);
  lv_obj_set_size(barLoad, 194, 12);
  lv_obj_align(barLoad, LV_ALIGN_TOP_LEFT, 82, 74);
  lv_bar_set_range(barLoad, 0, 100);
  lv_bar_set_value(barLoad, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(barLoad, lv_color_hex(0x101820), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(barLoad, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_color(barLoad, lv_color_hex(0x5A6983), LV_PART_MAIN);
  lv_obj_set_style_border_width(barLoad, 1, LV_PART_MAIN);
  lv_obj_set_style_radius(barLoad, 4, LV_PART_MAIN);
  lv_obj_set_style_radius(barLoad, 4, LV_PART_INDICATOR);
}

static void createInputsTab() {
  cardInputs = lv_obj_create(tabInputs);
  lv_obj_set_size(cardInputs, 296, 116);
  lv_obj_align(cardInputs, LV_ALIGN_TOP_MID, 0, 6);
  styleCard(cardInputs);

  lv_obj_t *title = lv_label_create(cardInputs);
  lv_label_set_text(title, "Inputs and Controls");
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 6, 0);

  lv_obj_t *labelBoot = lv_label_create(cardInputs);
  lv_label_set_text(labelBoot, "BOOT");
  lv_obj_align(labelBoot, LV_ALIGN_TOP_LEFT, 8, 20);

  ledBoot = lv_led_create(cardInputs);
  lv_obj_set_size(ledBoot, 16, 16);
  lv_obj_align(ledBoot, LV_ALIGN_TOP_LEFT, 50, 20);
  lv_led_off(ledBoot);

  lv_obj_t *labelUser = lv_label_create(cardInputs);
  lv_label_set_text(labelUser, "GPIO14");
  lv_obj_align(labelUser, LV_ALIGN_TOP_LEFT, 8, 40);

  ledUser = lv_led_create(cardInputs);
  lv_obj_set_size(ledUser, 16, 16);
  lv_obj_align(ledUser, LV_ALIGN_TOP_LEFT, 50, 40);
  lv_led_off(ledUser);

  lv_obj_t *labelAccent = lv_label_create(cardInputs);
  lv_label_set_text(labelAccent, "Theme");
  lv_obj_align(labelAccent, LV_ALIGN_TOP_LEFT, 8, 62);

  swAccent = lv_switch_create(cardInputs);
  lv_obj_set_size(swAccent, 46, 20);
  lv_obj_align(swAccent, LV_ALIGN_TOP_LEFT, 50, 60);
  lv_obj_clear_flag(swAccent, LV_OBJ_FLAG_CLICKABLE);

  labelInputState = lv_label_create(cardInputs);
  lv_label_set_text(labelInputState, "State B0:0 B14:0");
  lv_obj_align(labelInputState, LV_ALIGN_TOP_LEFT, 112, 24);

  labelInputCounts = lv_label_create(cardInputs);
  lv_label_set_text(labelInputCounts, "Count B0:0 B14:0");
  lv_obj_align(labelInputCounts, LV_ALIGN_TOP_LEFT, 112, 44);

  lv_obj_t *hint = lv_label_create(cardInputs);
  lv_label_set_text(hint, "S:B0 next, B14 theme\nL:B0 prev, B14 reset");
  lv_obj_set_style_text_color(hint, lv_color_hex(0xA8B6D6), 0);
  lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 112, 64);
}

static void createSystemTab() {
  cardSystem = lv_obj_create(tabSystem);
  lv_obj_set_size(cardSystem, 296, 116);
  lv_obj_align(cardSystem, LV_ALIGN_TOP_MID, 0, 6);
  styleCard(cardSystem);

  lv_obj_t *title = lv_label_create(cardSystem);
  lv_label_set_text(title, "System Metrics");
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 6, 0);

  lv_obj_t *rLabel = lv_label_create(cardSystem);
  lv_label_set_text(rLabel, "Refresh");
  lv_obj_align(rLabel, LV_ALIGN_TOP_LEFT, 8, 22);

  barRefresh = lv_bar_create(cardSystem);
  lv_obj_set_size(barRefresh, 186, 12);
  lv_obj_align(barRefresh, LV_ALIGN_TOP_LEFT, 78, 22);
  lv_bar_set_range(barRefresh, 0, 80);
  lv_bar_set_value(barRefresh, 0, LV_ANIM_OFF);
  lv_obj_set_style_radius(barRefresh, 4, LV_PART_MAIN);
  lv_obj_set_style_radius(barRefresh, 4, LV_PART_INDICATOR);

  lv_obj_t *cLabel = lv_label_create(cardSystem);
  lv_label_set_text(cLabel, "CPU");
  lv_obj_align(cLabel, LV_ALIGN_TOP_LEFT, 8, 42);

  barCpu = lv_bar_create(cardSystem);
  lv_obj_set_size(barCpu, 186, 12);
  lv_obj_align(barCpu, LV_ALIGN_TOP_LEFT, 78, 42);
  lv_bar_set_range(barCpu, 0, 100);
  lv_bar_set_value(barCpu, 0, LV_ANIM_OFF);
  lv_obj_set_style_radius(barCpu, 4, LV_PART_MAIN);
  lv_obj_set_style_radius(barCpu, 4, LV_PART_INDICATOR);

  labelSys1 = lv_label_create(cardSystem);
  lv_label_set_text(labelSys1, "fps=0.0 cpu=0.0%");
  lv_obj_align(labelSys1, LV_ALIGN_TOP_LEFT, 8, 58);

  labelSys2 = lv_label_create(cardSystem);
  lv_label_set_text(labelSys2, "Count B0:0 B14:0");
  lv_obj_set_style_text_color(labelSys2, lv_color_hex(0xA8B6D6), 0);
  lv_obj_align(labelSys2, LV_ALIGN_TOP_LEFT, 8, 74);

  labelSys3 = lv_label_create(cardSystem);
  lv_label_set_text(labelSys3, "B14 long: reset counters");
  lv_obj_set_style_text_color(labelSys3, lv_color_hex(0x8D9CBA), 0);
  lv_obj_align(labelSys3, LV_ALIGN_TOP_LEFT, 8, 90);
}

static void createUi() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x060B14), LV_PART_MAIN);
  lv_obj_set_style_text_color(scr, lv_color_hex(0xD6E0F2), LV_PART_MAIN);

  tabview = lv_tabview_create(scr, LV_DIR_TOP, 30);
  lv_obj_set_size(tabview, kScreenW - 8, kScreenH - 6);
  lv_obj_align(tabview, LV_ALIGN_TOP_MID, 0, 3);
  lv_obj_set_style_radius(tabview, 4, LV_PART_MAIN);
  lv_obj_set_style_border_width(tabview, 0, LV_PART_MAIN);
  lv_obj_set_scrollbar_mode(tabview, LV_SCROLLBAR_MODE_OFF);

  tabDash = lv_tabview_add_tab(tabview, "Dash");
  tabInputs = lv_tabview_add_tab(tabview, "Inputs");
  tabSystem = lv_tabview_add_tab(tabview, "System");

  lv_obj_t *tabContent = lv_tabview_get_content(tabview);
  lv_obj_set_scrollbar_mode(tabContent, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_pad_all(tabContent, 0, LV_PART_MAIN);

  styleTabPage(tabDash);
  styleTabPage(tabInputs);
  styleTabPage(tabSystem);

  createDashboardTab();
  createInputsTab();
  createSystemTab();
  applyAccentStyles();
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
  lastStepMs = millis();
  Serial.println("LVGL tab demo initialized.");
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

  if (nowMs - lastStepMs < 50) {
    return;
  }
  lastStepMs = nowMs;

  sampleButtons(nowMs);
  frame++;
  uiUpdateCount++;

  if (bootShortEdge) {
    setActiveTab(activeTab + 1);
  }
  if (bootLongEdge) {
    setActiveTab(activeTab - 1);
  }

  if (userShortEdge) {
    altAccent = !altAccent;
    applyAccentStyles();
  }
  if (userLongEdge) {
    bootPressCount = 0;
    userPressCount = 0;
    setActiveTab(1);
    Serial.println("Input counters reset by GPIO14 long press");
  }

  const int wave = static_cast<int>((frame * 3U) % 200U);
  const int loadPct = (wave <= 100) ? wave : (200 - wave);
  lv_bar_set_value(barLoad, loadPct, LV_ANIM_OFF);

  if (bootPressed) {
    lv_led_on(ledBoot);
  } else {
    lv_led_off(ledBoot);
  }
  if (userPressed) {
    lv_led_on(ledUser);
  } else {
    lv_led_off(ledUser);
  }

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
  }

  const int arcValue = ((loadPct * 7) + (static_cast<int>(cpuPercent) * 3)) / 10;
  lv_arc_set_value(arcCpu, static_cast<int16_t>(arcValue));
  lv_bar_set_value(barRefresh, static_cast<int16_t>(uiRefreshHz), LV_ANIM_OFF);
  lv_bar_set_value(barCpu, static_cast<int16_t>(cpuPercent), LV_ANIM_OFF);

  static char lineDash1[40];
  static char lineDash2[40];
  static char lineArc[10];
  static char lineIn1[40];
  static char lineIn2[40];
  static char lineSys1[40];
  static char lineSys2[40];

  snprintf(lineDash1, sizeof(lineDash1), "Ref:%4.1fHz Up:%lus",
           uiRefreshHz, static_cast<unsigned long>(nowMs / 1000U));
  snprintf(lineDash2, sizeof(lineDash2), "CPU:%4.1f%% Tab:%d/%d",
           cpuPercent, activeTab + 1, kTabCount);
  snprintf(lineArc, sizeof(lineArc), "%2d%%", arcValue);
  snprintf(lineIn1, sizeof(lineIn1), "State B0:%d B14:%d", bootPressed ? 1 : 0, userPressed ? 1 : 0);
  snprintf(lineIn2, sizeof(lineIn2), "Count B0:%lu B14:%lu",
           static_cast<unsigned long>(bootPressCount), static_cast<unsigned long>(userPressCount));
  snprintf(lineSys1, sizeof(lineSys1), "fps=%4.1f cpu=%4.1f%%", uiRefreshHz, cpuPercent);
  snprintf(lineSys2, sizeof(lineSys2), "Count B0:%lu B14:%lu",
           static_cast<unsigned long>(bootPressCount), static_cast<unsigned long>(userPressCount));

  lv_label_set_text(labelDash1, lineDash1);
  lv_label_set_text(labelDash2, lineDash2);
  lv_label_set_text(labelArcCpu, lineArc);
  lv_label_set_text(labelInputState, lineIn1);
  lv_label_set_text(labelInputCounts, lineIn2);
  lv_label_set_text(labelSys1, lineSys1);
  lv_label_set_text(labelSys2, lineSys2);
}
