#include <Arduino.h>

// Built-in LED pin is board-specific; GPIO8 works for many ESP32-C3 SuperMini boards.
#ifndef LED_PIN
#define LED_PIN 8
#endif

static bool ledState = false;
static unsigned long lastToggleMs = 0;

void setup() {
  pinMode(LED_PIN, OUTPUT);
  Serial.begin(115200);
  delay(200);
  Serial.println("Boot: __PROJECT_NAME__");
}

void loop() {
  const unsigned long now = millis();
  if (now - lastToggleMs >= 500) {
    lastToggleMs = now;
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState ? HIGH : LOW);
    Serial.printf("LED: %s\n", ledState ? "ON" : "OFF");
  }
}
