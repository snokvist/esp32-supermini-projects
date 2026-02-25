#include <Arduino.h>
#include <driver/gpio.h>

// --- Pin assignments (ESP32-C3 SuperMini) ---
#define CAMERA_TX_PIN 21  // UART1 TX -> Camera RX
#define CAMERA_RX_PIN 20  // UART1 RX <- Camera TX
#define RESET_PIN      3  // Open-drain reset output (active-low)
#define LED_PIN        8  // Onboard status LED

// --- UART config ---
#define CAMERA_BAUD 115200

// --- Escape characters from host ---
#define ESCAPE_RESET  0x12  // Ctrl-R: trigger camera reset
#define ESCAPE_STATUS 0x14  // Ctrl-T: print status line

// --- Reset pulse duration ---
#define RESET_PULSE_MS 200

// --- LED activity timeout ---
#define LED_ACTIVITY_MS 50

// --- State ---
static uint32_t g_bytes_to_camera = 0;
static uint32_t g_bytes_to_host = 0;
static uint32_t g_reset_count = 0;
static bool g_resetting = false;
static unsigned long g_reset_start_ms = 0;
static unsigned long g_led_off_ms = 0;

static void start_reset() {
  g_resetting = true;
  g_reset_start_ms = millis();
  g_reset_count++;
  // Pull reset low (active-low assert)
  pinMode(RESET_PIN, OUTPUT);
  digitalWrite(RESET_PIN, LOW);
  // LED solid during reset
  digitalWrite(LED_PIN, LOW);  // ESP32-C3 SuperMini LED is active-low
  Serial.printf("\r\n[debugger] Reset pulse started (%u ms) [count=%u]\r\n",
                RESET_PULSE_MS, g_reset_count);
}

static void end_reset() {
  // Release to high-Z; camera pull-up brings RESET high
  pinMode(RESET_PIN, INPUT);
  g_resetting = false;
  digitalWrite(LED_PIN, HIGH);  // LED off (active-low)
  Serial.printf("[debugger] Reset released, camera booting\r\n");
}

static void print_status() {
  unsigned long uptime_s = millis() / 1000;
  unsigned long h = uptime_s / 3600;
  unsigned long m = (uptime_s % 3600) / 60;
  unsigned long s = uptime_s % 60;
  Serial.printf("\r\n[debugger] Uptime: %luh%02lum%02lus | "
                "To camera: %u bytes | To host: %u bytes | "
                "Resets: %u\r\n",
                h, m, s, g_bytes_to_camera, g_bytes_to_host, g_reset_count);
}

static void led_blink() {
  if (g_resetting) return;  // LED is solid during reset
  digitalWrite(LED_PIN, LOW);  // ON (active-low)
  g_led_off_ms = millis() + LED_ACTIVITY_MS;
}

void setup() {
  // Status LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);  // OFF (active-low)

  // Reset pin: high-Z by default (camera pull-up keeps RESET high)
  pinMode(RESET_PIN, INPUT);

  // USB CDC serial to host
  Serial.begin(CAMERA_BAUD);

  // Wait for USB CDC to be ready (up to 3s, then continue headless)
  unsigned long waitStart = millis();
  while (!Serial && (millis() - waitStart < 3000)) {
    delay(10);
  }

  // ESP32-C3 bootloader maps UART0 to GPIO20/21 by default.
  // Detach it before we use those pins for UART1, otherwise UART0 TX
  // leaks ESP-IDF log output to the camera and corrupts the link.
  gpio_reset_pin(GPIO_NUM_20);
  gpio_reset_pin(GPIO_NUM_21);

  // Hardware UART1 to camera
  Serial1.begin(CAMERA_BAUD, SERIAL_8N1, CAMERA_RX_PIN, CAMERA_TX_PIN);

  // Drain any noise from floating RX before entering main loop
  delay(50);
  while (Serial1.available()) Serial1.read();

  Serial.println("\r\n[debugger] SSC338Q UART Debugger ready");
  Serial.println("[debugger] Ctrl-R = reset camera | Ctrl-T = status");
}

void loop() {
  unsigned long now = millis();

  // Check reset pulse completion
  if (g_resetting && (now - g_reset_start_ms >= RESET_PULSE_MS)) {
    end_reset();
  }

  // Host -> Camera (with escape handling)
  while (Serial.available()) {
    int b = Serial.read();
    if (b == ESCAPE_RESET) {
      if (!g_resetting) start_reset();
    } else if (b == ESCAPE_STATUS) {
      print_status();
    } else {
      Serial1.write((uint8_t)b);
      g_bytes_to_camera++;
      led_blink();
    }
  }

  // Camera -> Host
  while (Serial1.available()) {
    int b = Serial1.read();
    Serial.write((uint8_t)b);
    g_bytes_to_host++;
    led_blink();
  }

  // LED activity timeout
  if (!g_resetting && g_led_off_ms && now >= g_led_off_ms) {
    digitalWrite(LED_PIN, HIGH);  // OFF (active-low)
    g_led_off_ms = 0;
  }
}
