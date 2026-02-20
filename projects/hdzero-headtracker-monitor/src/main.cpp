#include <Arduino.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

#include <cstdlib>

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
constexpr char kApSsid[] = "waybeam-backpack";
constexpr char kApPassword[] = "waybeam-backpack";
const IPAddress kApIp(10, 100, 0, 1);
const IPAddress kApGateway(10, 100, 0, 1);
const IPAddress kApSubnet(255, 255, 255, 0);

constexpr uint8_t kMaxChannels = 12;
constexpr uint8_t kMinChannelsInFrame = 3;
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

constexpr uint8_t kServoCount = 3;
constexpr uint8_t kServoPwmResolutionBits = 14;
constexpr uint8_t kServoPwmChannels[kServoCount] = {0, 1, 2};
constexpr uint16_t kCrsfRxBytesPerLoopLimit = 256;

constexpr uint16_t kSettingsVersion = 1;
constexpr uint8_t kFirstSupportedGpio = 0;
constexpr uint8_t kLastLowSupportedGpio = 10;
constexpr uint8_t kFirstHighSupportedGpio = 20;
constexpr uint8_t kLastSupportedGpio = 21;

const char kWebUiHtml[] PROGMEM = R"HTML(
<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Waybeam Backpack</title>
<style>
body{font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:#f5f7fb;color:#1f2937;margin:0;padding:16px}
.card{max-width:980px;margin:0 auto;background:#fff;border:1px solid #d9e2ec;border-radius:10px;padding:16px}
h1{font-size:22px;margin:0 0 8px}
h3{margin:0;font-size:15px}
small{color:#4b5563}
.sections{display:grid;gap:12px;margin-top:12px}
.section{border:1px solid #dbe5ef;border-radius:10px;padding:10px;background:#f8fafc}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:10px;margin-top:8px}
.row{display:flex;flex-direction:column}
label{font-size:12px;color:#374151;margin-bottom:4px}
input,select{padding:8px;border:1px solid #cbd5e1;border-radius:8px}
.actions{display:flex;gap:8px;flex-wrap:wrap;margin-top:14px}
button{padding:10px 14px;border:0;border-radius:8px;cursor:pointer}
.apply{background:#0284c7;color:#fff}
.save{background:#166534;color:#fff}
.refresh{background:#334155;color:#fff}
.reset{background:#b91c1c;color:#fff}
pre{background:#0f172a;color:#e2e8f0;padding:10px;border-radius:8px;overflow:auto}
</style>
</head>
<body>
<div class="card">
  <h1>Waybeam Backpack</h1>
  <small>AP: <b>waybeam-backpack</b> / <b>waybeam-backpack</b>, network <b>10.100.0.x</b> (ESP32 is 10.100.0.1)</small>
  <form id="cfg">
    <div class="sections" id="sections"></div>
    <div class="actions">
      <button class="apply" type="button" onclick="submitCfg(false)">Apply Live</button>
      <button class="save" type="button" onclick="submitCfg(true)">Apply + Save</button>
      <button class="reset" type="button" onclick="resetDefaults()">Reset to defaults</button>
      <button class="refresh" type="button" onclick="loadAll()">Refresh</button>
    </div>
  </form>
  <h3>Status</h3>
  <pre id="status">loading...</pre>
</div>
<script>
const sections=[
{title:'Pins', fields:[
['led_pin','LED pin'],['ppm_input_pin','PPM input pin'],['mode_button_pin','Mode button pin'],
['crsf_uart_tx_pin','CRSF UART TX pin'],['crsf_uart_rx_pin','CRSF UART RX pin'],
['servo_pwm_1_pin','Servo PWM 1 pin'],['servo_pwm_2_pin','Servo PWM 2 pin'],['servo_pwm_3_pin','Servo PWM 3 pin']
]},
{title:'Modes', fields:[
['output_mode_live','Output mode live (0=CRSF,1=monitor)'],['output_mode_default','Output mode boot default (0=CRSF,1=monitor)']
]},
{title:'CRSF / UART', fields:[
['crsf_uart_baud','CRSF UART baud'],['crsf_map_min_us','PPM->CRSF map min us'],['crsf_map_max_us','PPM->CRSF map max us'],['crsf_rx_timeout_ms','CRSF RX timeout ms']
]},
{title:'Servo Outputs', fields:[
['servo_pwm_frequency_hz','Servo PWM frequency Hz'],
['servo_map_1','Servo 1 source CH (1..16)'],['servo_map_2','Servo 2 source CH (1..16)'],['servo_map_3','Servo 3 source CH (1..16)'],
['servo_pulse_min_us','Servo min pulse us'],['servo_pulse_center_us','Servo center pulse us'],['servo_pulse_max_us','Servo max pulse us']
]},
{title:'PPM Decode', fields:[
['ppm_min_channel_us','PPM min pulse us'],['ppm_max_channel_us','PPM max pulse us'],['ppm_sync_gap_us','PPM sync gap us']
]},
{title:'Timing / Health', fields:[
['signal_timeout_ms','Signal timeout ms'],['button_debounce_ms','Button debounce ms'],['monitor_print_interval_ms','Monitor print interval ms'],
['expected_min_frame_hz','Expected min frame Hz'],['expected_max_frame_hz','Expected max frame Hz']
]}
];

function buildForm(){
  const root=document.getElementById('sections');
  sections.forEach((section)=>{
    const block=document.createElement('div'); block.className='section';
    const title=document.createElement('h3'); title.textContent=section.title;
    const grid=document.createElement('div'); grid.className='grid';
    section.fields.forEach(([name,label])=>{
      const row=document.createElement('div'); row.className='row';
      const l=document.createElement('label'); l.textContent=label;
      const i=document.createElement('input'); i.name=name; i.type='text';
      row.appendChild(l); row.appendChild(i); grid.appendChild(row);
    });
    block.appendChild(title);
    block.appendChild(grid);
    root.appendChild(block);
  });
}

async function loadSettings(){
  const r=await fetch('/api/settings',{cache:'no-store'});
  if(!r.ok) throw new Error(await r.text());
  const s=await r.json();
  sections.forEach((section)=>{
    section.fields.forEach(([name])=>{
      const el=document.querySelector(`[name="${name}"]`);
      if(el && s[name]!==undefined) el.value=s[name];
    });
  });
}

async function loadStatus(){
  const r=await fetch('/api/status',{cache:'no-store'});
  if(!r.ok) throw new Error(await r.text());
  const s=await r.json();
  document.getElementById('status').textContent=JSON.stringify(s,null,2);
}

async function loadAll(){
  try{
    await loadSettings();
    await loadStatus();
  }catch(err){
    document.getElementById('status').textContent=`Error: ${err.message}`;
  }
}

async function submitCfg(persist){
  const fd=new FormData(document.getElementById('cfg'));
  fd.set('persist', persist ? '1' : '0');
  const body=new URLSearchParams(fd);
  const r=await fetch('/api/settings', {method:'POST', body});
  const text=await r.text();
  alert(r.ok ? text : `Error: ${text}`);
  if(!r.ok) return;
  await loadAll();
}

async function resetDefaults(){
  if(!confirm('Reset all settings to firmware defaults and save?')) return;
  const r=await fetch('/api/reset_defaults', {method:'POST'});
  const text=await r.text();
  alert(r.ok ? text : `Error: ${text}`);
  if(!r.ok) return;
  await loadAll();
}

buildForm();
loadAll();
setInterval(()=>{
  loadStatus().catch((err)=>{
    document.getElementById('status').textContent=`Error: ${err.message}`;
  });
}, 1000);
</script>
</body>
</html>
)HTML";

struct RuntimeSettings {
  uint8_t ledPin;
  uint8_t ppmInputPin;
  uint8_t modeButtonPin;
  uint8_t crsfUartTxPin;
  uint8_t crsfUartRxPin;
  uint32_t crsfUartBaud;

  uint8_t servoPwmPins[kServoCount];
  uint16_t servoPwmFrequencyHz;
  uint8_t servoMap[kServoCount];

  uint16_t ppmMinChannelUs;
  uint16_t ppmMaxChannelUs;
  uint16_t ppmSyncGapUs;

  uint32_t signalTimeoutMs;
  uint32_t buttonDebounceMs;
  uint32_t monitorPrintIntervalMs;

  float expectedMinFrameHz;
  float expectedMaxFrameHz;

  uint16_t crsfMapMinUs;
  uint16_t crsfMapMaxUs;
  uint32_t crsfRxTimeoutMs;

  uint16_t servoPulseMinUs;
  uint16_t servoPulseCenterUs;
  uint16_t servoPulseMaxUs;

  uint8_t outputModeDefault;
};

volatile uint16_t gIsrPpmMinChannelUs = 800;
volatile uint16_t gIsrPpmMaxChannelUs = 2200;
volatile uint16_t gIsrPpmSyncGapUs = 3000;

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
uint16_t gServoPulseUs[kServoCount] = {1500, 1500, 1500};
uint32_t gCrsfRxPacketCounter = 0;
uint32_t gCrsfRxInvalidCounter = 0;
uint32_t gLastCrsfRxMs = 0;
bool gHasCrsfRxFrame = false;

bool gPpmInterruptAttached = false;
uint8_t gAttachedPpmPin = PPM_INPUT_PIN;
uint8_t gAttachedLedPin = LED_PIN;
uint8_t gAttachedButtonPin = MODE_BUTTON_PIN;
uint8_t gAttachedServoPins[kServoCount] = {SERVO_PWM_1_PIN, SERVO_PWM_2_PIN, SERVO_PWM_3_PIN};

Preferences gPrefs;
bool gPrefsReady = false;
WebServer gWeb(80);
RuntimeSettings gSettings;

enum class OutputMode : uint8_t { kCrsfSerial = 0, kPpmMonitor = 1 };
OutputMode gOutputMode = OutputMode::kCrsfSerial;

RuntimeSettings defaultSettings() {
  RuntimeSettings s{};
  s.ledPin = LED_PIN;
  s.ppmInputPin = PPM_INPUT_PIN;
  s.modeButtonPin = MODE_BUTTON_PIN;
  s.crsfUartTxPin = CRSF_UART_TX_PIN;
  s.crsfUartRxPin = CRSF_UART_RX_PIN;
  s.crsfUartBaud = CRSF_UART_BAUD;

  s.servoPwmPins[0] = SERVO_PWM_1_PIN;
  s.servoPwmPins[1] = SERVO_PWM_2_PIN;
  s.servoPwmPins[2] = SERVO_PWM_3_PIN;
  s.servoPwmFrequencyHz = SERVO_PWM_FREQUENCY_HZ;
  s.servoMap[0] = 0;
  s.servoMap[1] = 1;
  s.servoMap[2] = 2;

  s.ppmMinChannelUs = 800;
  s.ppmMaxChannelUs = 2200;
  s.ppmSyncGapUs = 3000;

  s.signalTimeoutMs = 1000;
  s.buttonDebounceMs = 30;
  s.monitorPrintIntervalMs = 200;

  s.expectedMinFrameHz = 45.0f;
  s.expectedMaxFrameHz = 55.0f;

  s.crsfMapMinUs = 1000;
  s.crsfMapMaxUs = 2000;
  s.crsfRxTimeoutMs = 500;

  s.servoPulseMinUs = 1000;
  s.servoPulseCenterUs = 1500;
  s.servoPulseMaxUs = 2000;

  s.outputModeDefault = static_cast<uint8_t>(OutputMode::kCrsfSerial);
  return s;
}

uint8_t outputModeToUint(OutputMode mode) { return static_cast<uint8_t>(mode); }

uint8_t clampU8(uint32_t value) {
  return (value > 255U) ? 255U : static_cast<uint8_t>(value);
}

bool validateSettings(const RuntimeSettings &s, String &error);

bool saveSettingsToNvs(const RuntimeSettings &s) {
  if (!gPrefsReady) {
    return false;
  }

  bool ok = true;
  ok &= (gPrefs.putUShort("ver", kSettingsVersion) == sizeof(uint16_t));
  ok &= (gPrefs.putUChar("led", s.ledPin) == sizeof(uint8_t));
  ok &= (gPrefs.putUChar("ppm", s.ppmInputPin) == sizeof(uint8_t));
  ok &= (gPrefs.putUChar("btn", s.modeButtonPin) == sizeof(uint8_t));
  ok &= (gPrefs.putUChar("utx", s.crsfUartTxPin) == sizeof(uint8_t));
  ok &= (gPrefs.putUChar("urx", s.crsfUartRxPin) == sizeof(uint8_t));
  ok &= (gPrefs.putUInt("ubd", s.crsfUartBaud) == sizeof(uint32_t));

  ok &= (gPrefs.putUChar("sp1", s.servoPwmPins[0]) == sizeof(uint8_t));
  ok &= (gPrefs.putUChar("sp2", s.servoPwmPins[1]) == sizeof(uint8_t));
  ok &= (gPrefs.putUChar("sp3", s.servoPwmPins[2]) == sizeof(uint8_t));
  ok &= (gPrefs.putUShort("sfq", s.servoPwmFrequencyHz) == sizeof(uint16_t));
  ok &= (gPrefs.putUChar("sm1", s.servoMap[0]) == sizeof(uint8_t));
  ok &= (gPrefs.putUChar("sm2", s.servoMap[1]) == sizeof(uint8_t));
  ok &= (gPrefs.putUChar("sm3", s.servoMap[2]) == sizeof(uint8_t));

  ok &= (gPrefs.putUShort("pmin", s.ppmMinChannelUs) == sizeof(uint16_t));
  ok &= (gPrefs.putUShort("pmax", s.ppmMaxChannelUs) == sizeof(uint16_t));
  ok &= (gPrefs.putUShort("psyn", s.ppmSyncGapUs) == sizeof(uint16_t));

  ok &= (gPrefs.putUInt("stmo", s.signalTimeoutMs) == sizeof(uint32_t));
  ok &= (gPrefs.putUInt("bdb", s.buttonDebounceMs) == sizeof(uint32_t));
  ok &= (gPrefs.putUInt("mpri", s.monitorPrintIntervalMs) == sizeof(uint32_t));

  ok &= (gPrefs.putFloat("ehmn", s.expectedMinFrameHz) == sizeof(float));
  ok &= (gPrefs.putFloat("ehmx", s.expectedMaxFrameHz) == sizeof(float));

  ok &= (gPrefs.putUShort("cmn", s.crsfMapMinUs) == sizeof(uint16_t));
  ok &= (gPrefs.putUShort("cmx", s.crsfMapMaxUs) == sizeof(uint16_t));
  ok &= (gPrefs.putUInt("crxt", s.crsfRxTimeoutMs) == sizeof(uint32_t));

  ok &= (gPrefs.putUShort("svmn", s.servoPulseMinUs) == sizeof(uint16_t));
  ok &= (gPrefs.putUShort("svct", s.servoPulseCenterUs) == sizeof(uint16_t));
  ok &= (gPrefs.putUShort("svmx", s.servoPulseMaxUs) == sizeof(uint16_t));

  ok &= (gPrefs.putUChar("omod", s.outputModeDefault) == sizeof(uint8_t));
  return ok;
}

RuntimeSettings loadSettingsFromNvs() {
  RuntimeSettings s = defaultSettings();
  if (!gPrefsReady) {
    return s;
  }

  const uint16_t version = gPrefs.getUShort("ver", 0);
  if (version != kSettingsVersion) {
    if (!saveSettingsToNvs(s)) {
      Serial.println("WARN: Failed to initialize settings in NVS");
    }
    return s;
  }

  s.ledPin = gPrefs.getUChar("led", s.ledPin);
  s.ppmInputPin = gPrefs.getUChar("ppm", s.ppmInputPin);
  s.modeButtonPin = gPrefs.getUChar("btn", s.modeButtonPin);
  s.crsfUartTxPin = gPrefs.getUChar("utx", s.crsfUartTxPin);
  s.crsfUartRxPin = gPrefs.getUChar("urx", s.crsfUartRxPin);
  s.crsfUartBaud = gPrefs.getUInt("ubd", s.crsfUartBaud);

  s.servoPwmPins[0] = gPrefs.getUChar("sp1", s.servoPwmPins[0]);
  s.servoPwmPins[1] = gPrefs.getUChar("sp2", s.servoPwmPins[1]);
  s.servoPwmPins[2] = gPrefs.getUChar("sp3", s.servoPwmPins[2]);
  s.servoPwmFrequencyHz = gPrefs.getUShort("sfq", s.servoPwmFrequencyHz);
  s.servoMap[0] = gPrefs.getUChar("sm1", s.servoMap[0]);
  s.servoMap[1] = gPrefs.getUChar("sm2", s.servoMap[1]);
  s.servoMap[2] = gPrefs.getUChar("sm3", s.servoMap[2]);

  s.ppmMinChannelUs = gPrefs.getUShort("pmin", s.ppmMinChannelUs);
  s.ppmMaxChannelUs = gPrefs.getUShort("pmax", s.ppmMaxChannelUs);
  s.ppmSyncGapUs = gPrefs.getUShort("psyn", s.ppmSyncGapUs);

  s.signalTimeoutMs = gPrefs.getUInt("stmo", s.signalTimeoutMs);
  s.buttonDebounceMs = gPrefs.getUInt("bdb", s.buttonDebounceMs);
  s.monitorPrintIntervalMs = gPrefs.getUInt("mpri", s.monitorPrintIntervalMs);

  s.expectedMinFrameHz = gPrefs.getFloat("ehmn", s.expectedMinFrameHz);
  s.expectedMaxFrameHz = gPrefs.getFloat("ehmx", s.expectedMaxFrameHz);

  s.crsfMapMinUs = gPrefs.getUShort("cmn", s.crsfMapMinUs);
  s.crsfMapMaxUs = gPrefs.getUShort("cmx", s.crsfMapMaxUs);
  s.crsfRxTimeoutMs = gPrefs.getUInt("crxt", s.crsfRxTimeoutMs);

  s.servoPulseMinUs = gPrefs.getUShort("svmn", s.servoPulseMinUs);
  s.servoPulseCenterUs = gPrefs.getUShort("svct", s.servoPulseCenterUs);
  s.servoPulseMaxUs = gPrefs.getUShort("svmx", s.servoPulseMaxUs);

  s.outputModeDefault = gPrefs.getUChar("omod", s.outputModeDefault);
  String validationError;
  if (!validateSettings(s, validationError)) {
    Serial.printf("WARN: Stored settings invalid (%s). Restoring defaults.\n", validationError.c_str());
    s = defaultSettings();
    if (!saveSettingsToNvs(s)) {
      Serial.println("WARN: Failed to persist restored default settings");
    }
  }
  return s;
}

bool pinListHasDuplicates(const RuntimeSettings &s) {
  const uint8_t pins[] = {s.ledPin,          s.ppmInputPin,       s.modeButtonPin,
                          s.crsfUartTxPin,   s.crsfUartRxPin,      s.servoPwmPins[0],
                          s.servoPwmPins[1], s.servoPwmPins[2]};
  constexpr size_t n = sizeof(pins) / sizeof(pins[0]);
  for (size_t i = 0; i < n; ++i) {
    for (size_t j = i + 1; j < n; ++j) {
      if (pins[i] == pins[j]) {
        return true;
      }
    }
  }
  return false;
}

bool isSupportedGpio(uint8_t pin) {
  return ((pin >= kFirstSupportedGpio) && (pin <= kLastLowSupportedGpio)) ||
         ((pin >= kFirstHighSupportedGpio) && (pin <= kLastSupportedGpio));
}

bool validateSettings(const RuntimeSettings &s, String &error) {
  struct PinField {
    const char *name;
    uint8_t pin;
  };

  const PinField pinFields[] = {
      {"led_pin", s.ledPin},
      {"ppm_input_pin", s.ppmInputPin},
      {"mode_button_pin", s.modeButtonPin},
      {"crsf_uart_tx_pin", s.crsfUartTxPin},
      {"crsf_uart_rx_pin", s.crsfUartRxPin},
      {"servo_pwm_1_pin", s.servoPwmPins[0]},
      {"servo_pwm_2_pin", s.servoPwmPins[1]},
      {"servo_pwm_3_pin", s.servoPwmPins[2]},
  };

  for (const auto &field : pinFields) {
    if (!isSupportedGpio(field.pin)) {
      error = String(field.name) + " must be one of GPIO 0..10, 20, or 21";
      return false;
    }
  }

  if (s.ppmMinChannelUs >= s.ppmMaxChannelUs) {
    error = "ppm_min_channel_us must be < ppm_max_channel_us";
    return false;
  }
  if (s.crsfMapMinUs >= s.crsfMapMaxUs) {
    error = "crsf_map_min_us must be < crsf_map_max_us";
    return false;
  }
  if (!(s.expectedMinFrameHz < s.expectedMaxFrameHz)) {
    error = "expected_min_frame_hz must be < expected_max_frame_hz";
    return false;
  }
  if (!(s.servoPulseMinUs < s.servoPulseCenterUs && s.servoPulseCenterUs < s.servoPulseMaxUs)) {
    error = "servo pulse range must satisfy min < center < max";
    return false;
  }
  if (s.servoPwmFrequencyHz < 50 || s.servoPwmFrequencyHz > 400) {
    error = "servo_pwm_frequency_hz must be within 50..400";
    return false;
  }
  for (uint8_t i = 0; i < kServoCount; ++i) {
    if (s.servoMap[i] >= kCrsfChannelCount) {
      error = "servo_map values must be within 1..16";
      return false;
    }
  }
  if (s.outputModeDefault > 1) {
    error = "output_mode_default must be 0 or 1";
    return false;
  }
  if (pinListHasDuplicates(s)) {
    error = "pin assignment conflict: all configured pins must be unique";
    return false;
  }
  return true;
}

uint8_t crc8DvbS2(const uint8_t *data, size_t length) {
  uint8_t crc = 0;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x80U) ? static_cast<uint8_t>((crc << 1U) ^ 0xD5U) : static_cast<uint8_t>(crc << 1U);
    }
  }
  return crc;
}

void IRAM_ATTR onPpmRise() {
  const uint32_t nowUs = micros();
  const uint32_t dt = nowUs - gLastRiseUs;
  gLastRiseUs = nowUs;

  if (dt >= gIsrPpmSyncGapUs) {
    if (gIsrChannelCount >= kMinChannelsInFrame) {
      gIsrFrameChannelCount = gIsrChannelCount;
      gIsrFrameReady = true;
      gFrameCounter++;
    }
    gIsrChannelCount = 0;
    return;
  }

  if (dt < gIsrPpmMinChannelUs || dt > gIsrPpmMaxChannelUs) {
    gInvalidPulseCounter++;
    return;
  }

  if (gIsrChannelCount < kMaxChannels) {
    gIsrChannels[gIsrChannelCount++] = static_cast<uint16_t>(dt);
  }
}

uint16_t mapPulseUsToCrsf(uint16_t pulseUs) {
  uint16_t clampedUs = pulseUs;
  if (clampedUs < gSettings.crsfMapMinUs) {
    clampedUs = gSettings.crsfMapMinUs;
  } else if (clampedUs > gSettings.crsfMapMaxUs) {
    clampedUs = gSettings.crsfMapMaxUs;
  }

  const uint32_t numerator =
      static_cast<uint32_t>(clampedUs - gSettings.crsfMapMinUs) * static_cast<uint32_t>(kCrsfMaxValue - kCrsfMinValue);
  const uint32_t denominator = static_cast<uint32_t>(gSettings.crsfMapMaxUs - gSettings.crsfMapMinUs);
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
      static_cast<uint32_t>(clamped - kCrsfMinValue) * static_cast<uint32_t>(gSettings.servoPulseMaxUs - gSettings.servoPulseMinUs);
  const uint32_t denominator = static_cast<uint32_t>(kCrsfMaxValue - kCrsfMinValue);
  return static_cast<uint16_t>(gSettings.servoPulseMinUs + ((numerator + (denominator / 2U)) / denominator));
}

uint32_t pulseUsToPwmDuty(uint16_t pulseUs) {
  const uint32_t periodUs = 1000000UL / static_cast<uint32_t>(gSettings.servoPwmFrequencyHz);
  const uint32_t maxDuty = (1UL << kServoPwmResolutionBits) - 1UL;
  return (static_cast<uint32_t>(pulseUs) * maxDuty + (periodUs / 2UL)) / periodUs;
}

void writeServoPulseUs(uint8_t servoIndex, uint16_t pulseUs) {
  if (servoIndex >= kServoCount) {
    return;
  }
  gServoPulseUs[servoIndex] = pulseUs;
  ledcWrite(kServoPwmChannels[servoIndex], pulseUsToPwmDuty(pulseUs));
}

void setupServoOutputs(const uint8_t oldPins[kServoCount]) {
  for (uint8_t i = 0; i < kServoCount; ++i) {
    ledcDetachPin(oldPins[i]);
  }

  for (uint8_t i = 0; i < kServoCount; ++i) {
    ledcSetup(kServoPwmChannels[i], gSettings.servoPwmFrequencyHz, kServoPwmResolutionBits);
    ledcAttachPin(gSettings.servoPwmPins[i], kServoPwmChannels[i]);
    writeServoPulseUs(i, gSettings.servoPulseCenterUs);
    gAttachedServoPins[i] = gSettings.servoPwmPins[i];
  }
}

void updateServoOutputsFromCrsf() {
  const uint32_t nowMs = millis();
  const bool crsfFresh = gHasCrsfRxFrame && ((nowMs - gLastCrsfRxMs) <= gSettings.crsfRxTimeoutMs);

  if (!crsfFresh) {
    for (uint8_t i = 0; i < kServoCount; ++i) {
      writeServoPulseUs(i, gSettings.servoPulseCenterUs);
    }
    return;
  }

  for (uint8_t i = 0; i < kServoCount; ++i) {
    const uint8_t sourceCh = gSettings.servoMap[i];
    writeServoPulseUs(i, mapCrsfToServoUs(gCrsfRxChannels[sourceCh]));
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

void processCrsfRxInput() {
#if CRSF_UART_ENABLED
  if (!gCrsfUartReady) {
    return;
  }
  uint16_t budget = kCrsfRxBytesPerLoopLimit;
  while ((budget-- > 0U) && (gCrsfUart.available() > 0)) {
    const int byteRead = gCrsfUart.read();
    if (byteRead >= 0) {
      processCrsfRxByte(static_cast<uint8_t>(byteRead));
    }
  }
#endif
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

void printPpmMonitorFrame(uint32_t invalidPulses) {
  const uint32_t nowMs = millis();
  const uint32_t elapsedMs = nowMs - gLastPrintMs;
  if (elapsedMs < gSettings.monitorPrintIntervalMs) {
    return;
  }
  gLastPrintMs = nowMs;

  const uint32_t frameCountDelta = gLatestFrameCounter - gLastMonitorFrameCounter;
  const float windowHz = (elapsedMs > 0) ? (static_cast<float>(frameCountDelta) * 1000.0f / static_cast<float>(elapsedMs))
                                         : 0.0f;
  const bool rateOk = (windowHz >= gSettings.expectedMinFrameHz) && (windowHz <= gSettings.expectedMaxFrameHz);
  const uint32_t invalidDelta = invalidPulses - gLastMonitorInvalidPulseCounter;
  const uint32_t frameAgeMs = nowMs - gLastFrameMs;
  const uint32_t crsfRxAgeMs = gHasCrsfRxFrame ? (nowMs - gLastCrsfRxMs) : 0;
  const bool crsfRxFresh = gHasCrsfRxFrame && (crsfRxAgeMs <= gSettings.crsfRxTimeoutMs);

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
    Serial.printf(" | pan=%u roll=%u tilt=%u", static_cast<unsigned>(gLatestChannels[0]),
                  static_cast<unsigned>(gLatestChannels[1]), static_cast<unsigned>(gLatestChannels[2]));
  }
  Serial.println();

  gLastMonitorFrameCounter = gLatestFrameCounter;
  gLastMonitorInvalidPulseCounter = invalidPulses;
}

void updateLed() {
  const uint32_t nowMs = millis();
  const bool hasSignal = (nowMs - gLastFrameMs) <= gSettings.signalTimeoutMs;
  const uint32_t blinkPeriodMs = hasSignal ? 100 : 500;
  if (nowMs - gLastLedToggleMs < blinkPeriodMs) {
    return;
  }
  gLastLedToggleMs = nowMs;
  gLedState = !gLedState;
  digitalWrite(gSettings.ledPin, gLedState ? HIGH : LOW);
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
  const bool rawState = digitalRead(gSettings.modeButtonPin);
  if (rawState != gButtonRawState) {
    gButtonRawState = rawState;
    gLastButtonChangeMs = nowMs;
  }

  if ((nowMs - gLastButtonChangeMs) < gSettings.buttonDebounceMs) {
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

void applySettings(const RuntimeSettings &newSettings) {
  const RuntimeSettings oldSettings = gSettings;
  const uint8_t oldServoPins[kServoCount] = {gAttachedServoPins[0], gAttachedServoPins[1], gAttachedServoPins[2]};

  if (gPpmInterruptAttached) {
    detachInterrupt(digitalPinToInterrupt(gAttachedPpmPin));
    gPpmInterruptAttached = false;
  }

  gSettings = newSettings;

  noInterrupts();
  gIsrPpmMinChannelUs = gSettings.ppmMinChannelUs;
  gIsrPpmMaxChannelUs = gSettings.ppmMaxChannelUs;
  gIsrPpmSyncGapUs = gSettings.ppmSyncGapUs;
  gIsrChannelCount = 0;
  gIsrFrameReady = false;
  interrupts();

  if (gAttachedLedPin != gSettings.ledPin) {
    pinMode(gAttachedLedPin, INPUT);
  }
  pinMode(gSettings.ledPin, OUTPUT);
  gAttachedLedPin = gSettings.ledPin;

  pinMode(gSettings.modeButtonPin, INPUT_PULLUP);
  gAttachedButtonPin = gSettings.modeButtonPin;
  gButtonRawState = digitalRead(gSettings.modeButtonPin);
  gButtonStableState = gButtonRawState;
  gLastButtonChangeMs = millis();

  pinMode(gSettings.ppmInputPin, INPUT);
  attachInterrupt(digitalPinToInterrupt(gSettings.ppmInputPin), onPpmRise, RISING);
  gPpmInterruptAttached = true;
  gAttachedPpmPin = gSettings.ppmInputPin;

#if CRSF_UART_ENABLED
  if (gCrsfUartReady) {
    gCrsfUart.end();
  }
  gCrsfUart.begin(gSettings.crsfUartBaud, SERIAL_8N1, gSettings.crsfUartRxPin, gSettings.crsfUartTxPin);
  gCrsfUartReady = true;
#endif

  setupServoOutputs(oldServoPins);

  if (oldSettings.monitorPrintIntervalMs != gSettings.monitorPrintIntervalMs) {
    gLastPrintMs = 0;
  }
}

bool parseUIntArgOptional(const char *name, uint32_t minValue, uint32_t maxValue, uint32_t &target, String &error) {
  if (!gWeb.hasArg(name)) {
    return true;
  }
  const String raw = gWeb.arg(name);
  char *end = nullptr;
  const unsigned long value = strtoul(raw.c_str(), &end, 10);
  if (end == raw.c_str() || *end != '\0') {
    error = String("invalid integer for ") + name;
    return false;
  }
  if (value < minValue || value > maxValue) {
    error = String(name) + " out of range";
    return false;
  }
  target = static_cast<uint32_t>(value);
  return true;
}

bool parseFloatArgOptional(const char *name, float minValue, float maxValue, float &target, String &error) {
  if (!gWeb.hasArg(name)) {
    return true;
  }
  const String raw = gWeb.arg(name);
  char *end = nullptr;
  const float value = strtof(raw.c_str(), &end);
  if (end == raw.c_str() || *end != '\0') {
    error = String("invalid float for ") + name;
    return false;
  }
  if (value < minValue || value > maxValue) {
    error = String(name) + " out of range";
    return false;
  }
  target = value;
  return true;
}

String settingsToJson() {
  String json;
  json.reserve(620);
  json = "{";
  json += "\"led_pin\":" + String(gSettings.ledPin);
  json += ",\"ppm_input_pin\":" + String(gSettings.ppmInputPin);
  json += ",\"mode_button_pin\":" + String(gSettings.modeButtonPin);
  json += ",\"crsf_uart_tx_pin\":" + String(gSettings.crsfUartTxPin);
  json += ",\"crsf_uart_rx_pin\":" + String(gSettings.crsfUartRxPin);
  json += ",\"crsf_uart_baud\":" + String(gSettings.crsfUartBaud);

  json += ",\"servo_pwm_1_pin\":" + String(gSettings.servoPwmPins[0]);
  json += ",\"servo_pwm_2_pin\":" + String(gSettings.servoPwmPins[1]);
  json += ",\"servo_pwm_3_pin\":" + String(gSettings.servoPwmPins[2]);
  json += ",\"servo_pwm_frequency_hz\":" + String(gSettings.servoPwmFrequencyHz);
  json += ",\"servo_map_1\":" + String(gSettings.servoMap[0] + 1);
  json += ",\"servo_map_2\":" + String(gSettings.servoMap[1] + 1);
  json += ",\"servo_map_3\":" + String(gSettings.servoMap[2] + 1);

  json += ",\"ppm_min_channel_us\":" + String(gSettings.ppmMinChannelUs);
  json += ",\"ppm_max_channel_us\":" + String(gSettings.ppmMaxChannelUs);
  json += ",\"ppm_sync_gap_us\":" + String(gSettings.ppmSyncGapUs);

  json += ",\"signal_timeout_ms\":" + String(gSettings.signalTimeoutMs);
  json += ",\"button_debounce_ms\":" + String(gSettings.buttonDebounceMs);
  json += ",\"monitor_print_interval_ms\":" + String(gSettings.monitorPrintIntervalMs);

  json += ",\"expected_min_frame_hz\":" + String(gSettings.expectedMinFrameHz, 2);
  json += ",\"expected_max_frame_hz\":" + String(gSettings.expectedMaxFrameHz, 2);

  json += ",\"crsf_map_min_us\":" + String(gSettings.crsfMapMinUs);
  json += ",\"crsf_map_max_us\":" + String(gSettings.crsfMapMaxUs);
  json += ",\"crsf_rx_timeout_ms\":" + String(gSettings.crsfRxTimeoutMs);

  json += ",\"servo_pulse_min_us\":" + String(gSettings.servoPulseMinUs);
  json += ",\"servo_pulse_center_us\":" + String(gSettings.servoPulseCenterUs);
  json += ",\"servo_pulse_max_us\":" + String(gSettings.servoPulseMaxUs);

  json += ",\"output_mode_live\":" + String(outputModeToUint(gOutputMode));
  json += ",\"output_mode_default\":" + String(gSettings.outputModeDefault);
  json += "}";
  return json;
}

String statusToJson() {
  const uint32_t nowMs = millis();
  const uint32_t crsfAgeMs = gHasCrsfRxFrame ? (nowMs - gLastCrsfRxMs) : 0;

  String json;
  json.reserve(300);
  json = "{";
  json += "\"ap_ssid\":\"" + String(kApSsid) + "\"";
  json += ",\"ap_ip\":\"" + WiFi.softAPIP().toString() + "\"";
  json += ",\"stations\":" + String(WiFi.softAPgetStationNum());
  json += ",\"nvs_ready\":" + String(gPrefsReady ? 1 : 0);
  json += ",\"output_mode\":" + String(outputModeToUint(gOutputMode));
  json += ",\"frame_hz\":" + String(gFrameHz, 2);
  json += ",\"frame_hz_ema\":" + String(gFrameHzEma, 2);
  json += ",\"crsf_rx_packets\":" + String(gCrsfRxPacketCounter);
  json += ",\"crsf_rx_invalid\":" + String(gCrsfRxInvalidCounter);
  json += ",\"crsf_rx_age_ms\":" + String(crsfAgeMs);
  json += ",\"servo_us\":[" + String(gServoPulseUs[0]) + "," + String(gServoPulseUs[1]) + "," + String(gServoPulseUs[2]) + "]";
  json += "}";
  return json;
}

void setNoCacheHeaders() {
  gWeb.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  gWeb.sendHeader("Pragma", "no-cache");
  gWeb.sendHeader("X-Content-Type-Options", "nosniff");
}

void handleRoot() {
  setNoCacheHeaders();
  gWeb.send_P(200, "text/html", kWebUiHtml);
}

void handleGetSettings() {
  setNoCacheHeaders();
  gWeb.send(200, "application/json", settingsToJson());
}

void handleGetStatus() {
  setNoCacheHeaders();
  gWeb.send(200, "application/json", statusToJson());
}

void handlePostSettings() {
  RuntimeSettings candidate = gSettings;
  String error;

  uint32_t tmp = candidate.ledPin;
  if (!parseUIntArgOptional("led_pin", 0, 21, tmp, error)) {
    gWeb.send(400, "text/plain", error);
    return;
  }
  candidate.ledPin = clampU8(tmp);

  tmp = candidate.ppmInputPin;
  if (!parseUIntArgOptional("ppm_input_pin", 0, 21, tmp, error)) {
    gWeb.send(400, "text/plain", error);
    return;
  }
  candidate.ppmInputPin = clampU8(tmp);

  tmp = candidate.modeButtonPin;
  if (!parseUIntArgOptional("mode_button_pin", 0, 21, tmp, error)) {
    gWeb.send(400, "text/plain", error);
    return;
  }
  candidate.modeButtonPin = clampU8(tmp);

  tmp = candidate.crsfUartTxPin;
  if (!parseUIntArgOptional("crsf_uart_tx_pin", 0, 21, tmp, error)) {
    gWeb.send(400, "text/plain", error);
    return;
  }
  candidate.crsfUartTxPin = clampU8(tmp);

  tmp = candidate.crsfUartRxPin;
  if (!parseUIntArgOptional("crsf_uart_rx_pin", 0, 21, tmp, error)) {
    gWeb.send(400, "text/plain", error);
    return;
  }
  candidate.crsfUartRxPin = clampU8(tmp);

  tmp = candidate.crsfUartBaud;
  if (!parseUIntArgOptional("crsf_uart_baud", 9600, 1000000, tmp, error)) {
    gWeb.send(400, "text/plain", error);
    return;
  }
  candidate.crsfUartBaud = tmp;

  tmp = candidate.servoPwmPins[0];
  if (!parseUIntArgOptional("servo_pwm_1_pin", 0, 21, tmp, error)) {
    gWeb.send(400, "text/plain", error);
    return;
  }
  candidate.servoPwmPins[0] = clampU8(tmp);

  tmp = candidate.servoPwmPins[1];
  if (!parseUIntArgOptional("servo_pwm_2_pin", 0, 21, tmp, error)) {
    gWeb.send(400, "text/plain", error);
    return;
  }
  candidate.servoPwmPins[1] = clampU8(tmp);

  tmp = candidate.servoPwmPins[2];
  if (!parseUIntArgOptional("servo_pwm_3_pin", 0, 21, tmp, error)) {
    gWeb.send(400, "text/plain", error);
    return;
  }
  candidate.servoPwmPins[2] = clampU8(tmp);

  tmp = candidate.servoPwmFrequencyHz;
  if (!parseUIntArgOptional("servo_pwm_frequency_hz", 50, 400, tmp, error)) {
    gWeb.send(400, "text/plain", error);
    return;
  }
  candidate.servoPwmFrequencyHz = static_cast<uint16_t>(tmp);

  tmp = static_cast<uint32_t>(candidate.servoMap[0] + 1U);
  if (!parseUIntArgOptional("servo_map_1", 1, 16, tmp, error)) {
    gWeb.send(400, "text/plain", error);
    return;
  }
  candidate.servoMap[0] = static_cast<uint8_t>(tmp - 1U);

  tmp = static_cast<uint32_t>(candidate.servoMap[1] + 1U);
  if (!parseUIntArgOptional("servo_map_2", 1, 16, tmp, error)) {
    gWeb.send(400, "text/plain", error);
    return;
  }
  candidate.servoMap[1] = static_cast<uint8_t>(tmp - 1U);

  tmp = static_cast<uint32_t>(candidate.servoMap[2] + 1U);
  if (!parseUIntArgOptional("servo_map_3", 1, 16, tmp, error)) {
    gWeb.send(400, "text/plain", error);
    return;
  }
  candidate.servoMap[2] = static_cast<uint8_t>(tmp - 1U);

  tmp = candidate.ppmMinChannelUs;
  if (!parseUIntArgOptional("ppm_min_channel_us", 500, 2500, tmp, error)) {
    gWeb.send(400, "text/plain", error);
    return;
  }
  candidate.ppmMinChannelUs = static_cast<uint16_t>(tmp);

  tmp = candidate.ppmMaxChannelUs;
  if (!parseUIntArgOptional("ppm_max_channel_us", 1000, 4000, tmp, error)) {
    gWeb.send(400, "text/plain", error);
    return;
  }
  candidate.ppmMaxChannelUs = static_cast<uint16_t>(tmp);

  tmp = candidate.ppmSyncGapUs;
  if (!parseUIntArgOptional("ppm_sync_gap_us", 1500, 10000, tmp, error)) {
    gWeb.send(400, "text/plain", error);
    return;
  }
  candidate.ppmSyncGapUs = static_cast<uint16_t>(tmp);

  tmp = candidate.signalTimeoutMs;
  if (!parseUIntArgOptional("signal_timeout_ms", 100, 10000, tmp, error)) {
    gWeb.send(400, "text/plain", error);
    return;
  }
  candidate.signalTimeoutMs = tmp;

  tmp = candidate.buttonDebounceMs;
  if (!parseUIntArgOptional("button_debounce_ms", 5, 1000, tmp, error)) {
    gWeb.send(400, "text/plain", error);
    return;
  }
  candidate.buttonDebounceMs = tmp;

  tmp = candidate.monitorPrintIntervalMs;
  if (!parseUIntArgOptional("monitor_print_interval_ms", 50, 2000, tmp, error)) {
    gWeb.send(400, "text/plain", error);
    return;
  }
  candidate.monitorPrintIntervalMs = tmp;

  float tmpF = candidate.expectedMinFrameHz;
  if (!parseFloatArgOptional("expected_min_frame_hz", 1.0f, 500.0f, tmpF, error)) {
    gWeb.send(400, "text/plain", error);
    return;
  }
  candidate.expectedMinFrameHz = tmpF;

  tmpF = candidate.expectedMaxFrameHz;
  if (!parseFloatArgOptional("expected_max_frame_hz", 1.0f, 500.0f, tmpF, error)) {
    gWeb.send(400, "text/plain", error);
    return;
  }
  candidate.expectedMaxFrameHz = tmpF;

  tmp = candidate.crsfMapMinUs;
  if (!parseUIntArgOptional("crsf_map_min_us", 500, 2500, tmp, error)) {
    gWeb.send(400, "text/plain", error);
    return;
  }
  candidate.crsfMapMinUs = static_cast<uint16_t>(tmp);

  tmp = candidate.crsfMapMaxUs;
  if (!parseUIntArgOptional("crsf_map_max_us", 1000, 3000, tmp, error)) {
    gWeb.send(400, "text/plain", error);
    return;
  }
  candidate.crsfMapMaxUs = static_cast<uint16_t>(tmp);

  tmp = candidate.crsfRxTimeoutMs;
  if (!parseUIntArgOptional("crsf_rx_timeout_ms", 100, 5000, tmp, error)) {
    gWeb.send(400, "text/plain", error);
    return;
  }
  candidate.crsfRxTimeoutMs = tmp;

  tmp = candidate.servoPulseMinUs;
  if (!parseUIntArgOptional("servo_pulse_min_us", 500, 2500, tmp, error)) {
    gWeb.send(400, "text/plain", error);
    return;
  }
  candidate.servoPulseMinUs = static_cast<uint16_t>(tmp);

  tmp = candidate.servoPulseCenterUs;
  if (!parseUIntArgOptional("servo_pulse_center_us", 500, 2500, tmp, error)) {
    gWeb.send(400, "text/plain", error);
    return;
  }
  candidate.servoPulseCenterUs = static_cast<uint16_t>(tmp);

  tmp = candidate.servoPulseMaxUs;
  if (!parseUIntArgOptional("servo_pulse_max_us", 500, 2500, tmp, error)) {
    gWeb.send(400, "text/plain", error);
    return;
  }
  candidate.servoPulseMaxUs = static_cast<uint16_t>(tmp);

  tmp = candidate.outputModeDefault;
  if (!parseUIntArgOptional("output_mode_default", 0, 1, tmp, error)) {
    gWeb.send(400, "text/plain", error);
    return;
  }
  candidate.outputModeDefault = static_cast<uint8_t>(tmp);

  uint8_t requestedLiveMode = outputModeToUint(gOutputMode);
  tmp = requestedLiveMode;
  if (!parseUIntArgOptional("output_mode_live", 0, 1, tmp, error)) {
    gWeb.send(400, "text/plain", error);
    return;
  }
  requestedLiveMode = static_cast<uint8_t>(tmp);

  if (!validateSettings(candidate, error)) {
    gWeb.send(400, "text/plain", error);
    return;
  }

  applySettings(candidate);
  gOutputMode = (requestedLiveMode == 0) ? OutputMode::kCrsfSerial : OutputMode::kPpmMonitor;

  const bool persist = gWeb.hasArg("persist") && (gWeb.arg("persist") == "1");
  bool persisted = false;
  if (persist) {
    persisted = saveSettingsToNvs(gSettings);
  }

  setNoCacheHeaders();
  if (persist && !persisted) {
    gWeb.send(500, "text/plain", "Applied live, but failed to save to NVS");
    return;
  }
  gWeb.send(200, "text/plain", persist ? "Applied live and saved" : "Applied live");
}

void handleResetDefaults() {
  RuntimeSettings defaults = defaultSettings();
  String error;
  if (!validateSettings(defaults, error)) {
    gWeb.send(500, "text/plain", String("Default settings invalid: ") + error);
    return;
  }

  applySettings(defaults);
  gOutputMode = (defaults.outputModeDefault == 0) ? OutputMode::kCrsfSerial : OutputMode::kPpmMonitor;
  const bool saved = saveSettingsToNvs(gSettings);
  setNoCacheHeaders();
  if (!saved) {
    gWeb.send(500, "text/plain", "Reset to defaults and applied live, but failed to save to NVS");
    return;
  }
  gWeb.send(200, "text/plain", "Reset to defaults, applied live, and saved");
}

void setupWebUi() {
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAPConfig(kApIp, kApGateway, kApSubnet)) {
    Serial.println("WARN: Failed to configure AP network settings");
  }
  if (!WiFi.softAP(kApSsid, kApPassword)) {
    Serial.println("WARN: Failed to start SoftAP");
  }

  gWeb.on("/", HTTP_GET, handleRoot);
  gWeb.on("/api/settings", HTTP_GET, handleGetSettings);
  gWeb.on("/api/settings", HTTP_POST, handlePostSettings);
  gWeb.on("/api/reset_defaults", HTTP_POST, handleResetDefaults);
  gWeb.on("/api/status", HTTP_GET, handleGetStatus);
  gWeb.onNotFound([]() { gWeb.send(404, "text/plain", "Not Found"); });
  gWeb.begin();
}

} // namespace

void setup() {
  Serial.begin(115200);
  gPrefsReady = gPrefs.begin("waybeam", false);
  if (!gPrefsReady) {
    Serial.println("WARN: Preferences init failed; persistence disabled");
  }

  gSettings = loadSettingsFromNvs();
  applySettings(gSettings);
  gOutputMode = (gSettings.outputModeDefault == 0) ? OutputMode::kCrsfSerial : OutputMode::kPpmMonitor;

  setupWebUi();
}

void loop() {
  gWeb.handleClient();

  uint32_t invalidPulses = 0;
  const bool frameReady = copyFrameFromIsr(invalidPulses);
  processCrsfRxInput();
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
  if (gOutputMode == OutputMode::kPpmMonitor && (nowMs - gLastFrameMs) > gSettings.signalTimeoutMs &&
      (nowMs - gLastPrintMs) > 1000) {
    gLastPrintMs = nowMs;
    Serial.println("No PPM frame detected yet. Check HT enabled + wiring.");
  }
}
