#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_system.h>

#include <cstdlib>

#ifndef LED_PIN
#define LED_PIN 8
#endif

// Connect HDZero BoxPro+ 3.5mm TS jack tip (HT PPM output) to this pin.
// Sleeve goes to ESP32 GND.
#ifndef PPM_INPUT_PIN
#define PPM_INPUT_PIN 10
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
#define SERVO_PWM_1_PIN 0
#endif

#ifndef SERVO_PWM_2_PIN
#define SERVO_PWM_2_PIN 1
#endif

#ifndef SERVO_PWM_3_PIN
#define SERVO_PWM_3_PIN 2
#endif

#ifndef SERVO_PWM_FREQUENCY_HZ
#define SERVO_PWM_FREQUENCY_HZ 100
#endif

#ifndef OLED_SDA_PIN
#define OLED_SDA_PIN 4
#endif

#ifndef OLED_SCL_PIN
#define OLED_SCL_PIN 5
#endif

#ifndef OLED_I2C_ADDRESS
#define OLED_I2C_ADDRESS 0x3C
#endif

namespace {
constexpr char kApSsid[] = "waybeam-backpack";
constexpr uint8_t kApChannel = 6;
const IPAddress kApIp(10, 100, 0, 1);
const IPAddress kApGateway(10, 100, 0, 1);
const IPAddress kApSubnet(255, 255, 255, 0);

constexpr uint8_t kMaxChannels = 12;
constexpr uint8_t kMinChannelsInFrame = 3;

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
constexpr uint8_t kScreenWidth = 128;
constexpr uint8_t kScreenHeight = 64;
constexpr uint32_t kI2cClockHz = 400000;
constexpr uint32_t kOledRefreshIntervalMs = 100;
constexpr uint32_t kButtonLongPressMs = 3000;
constexpr uint32_t kRouteAcquireWindowMs = 150;
constexpr uint32_t kRouteSwitchHoldMs = 250;
constexpr uint32_t kMinCrsfRxRateWindowMs = 200;
constexpr uint8_t kPpmHealthyFrameCount = 3;
constexpr uint8_t kCrsfHealthyPacketCount = 3;
constexpr uint8_t kRouteEventHistorySize = 8;

constexpr uint16_t kSettingsVersion = 3;
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
:root{
  --bg:#eef4f7;
  --panel:#ffffff;
  --panel-alt:#f6fafc;
  --border:#d8e3ea;
  --text:#173042;
  --muted:#587284;
  --accent:#006ea8;
  --accent-2:#0d8f6f;
  --danger:#b64032;
  --shadow:0 14px 36px rgba(26,52,68,.10);
}
*{box-sizing:border-box}
body{
  font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;
  background:linear-gradient(180deg,#edf4f7 0%,#e4eef3 100%);
  color:var(--text);
  margin:0;
  padding:18px;
}
.card{
  max-width:1120px;
  margin:0 auto;
  background:var(--panel);
  border:1px solid var(--border);
  border-radius:18px;
  padding:18px;
  box-shadow:var(--shadow);
}
.hero{
  display:flex;
  justify-content:space-between;
  gap:16px;
  align-items:flex-start;
  flex-wrap:wrap;
}
h1{font-size:24px;margin:0 0 6px}
h2{font-size:16px;margin:0}
h3{margin:0;font-size:15px}
p,small{color:var(--muted)}
.hero-copy{max-width:720px}
.hero-copy p{margin:0}
.hero-chip{
  border:1px solid var(--border);
  background:var(--panel-alt);
  border-radius:999px;
  padding:8px 12px;
  color:var(--muted);
  font-size:12px;
  white-space:nowrap;
}
.flash{
  margin-top:14px;
  padding:10px 12px;
  border-radius:12px;
  border:1px solid var(--border);
  background:#edf7fb;
  color:var(--text);
}
.flash.error{
  background:#fff0ed;
  border-color:#f0bcb5;
  color:#852d21;
}
.summary{
  display:grid;
  grid-template-columns:repeat(auto-fit,minmax(150px,1fr));
  gap:10px;
  margin-top:16px;
}
.stat{
  padding:12px;
  border:1px solid var(--border);
  border-radius:14px;
  background:linear-gradient(180deg,#fbfdfe 0%,#f2f8fb 100%);
}
.stat-label{
  font-size:11px;
  letter-spacing:.08em;
  text-transform:uppercase;
  color:var(--muted);
}
.stat-value{
  margin-top:6px;
  font-size:18px;
  font-weight:700;
  line-height:1.15;
}
.stat-sub{
  margin-top:4px;
  font-size:12px;
  color:var(--muted);
}
.sections{
  display:grid;
  gap:12px;
  margin-top:18px;
}
.section{
  border:1px solid var(--border);
  border-radius:16px;
  padding:14px;
  background:var(--panel-alt);
}
.section-head{
  display:flex;
  justify-content:space-between;
  gap:12px;
  align-items:flex-start;
  flex-wrap:wrap;
}
.section-note{
  margin-top:6px;
  font-size:12px;
  color:var(--muted);
}
.grid{
  display:grid;
  grid-template-columns:repeat(auto-fit,minmax(220px,1fr));
  gap:10px;
  margin-top:10px;
}
.row{display:flex;flex-direction:column}
label{
  font-size:12px;
  color:#314c5f;
  margin-bottom:4px;
  font-weight:600;
}
input,select{
  width:100%;
  padding:10px 12px;
  border:1px solid #c2d3dd;
  border-radius:10px;
  background:#fff;
  color:var(--text);
}
.actions{
  display:flex;
  gap:8px;
  flex-wrap:wrap;
  margin-top:16px;
}
button{
  padding:10px 14px;
  border:0;
  border-radius:10px;
  cursor:pointer;
  font-weight:700;
}
.apply{background:var(--accent);color:#fff}
.save{background:var(--accent-2);color:#fff}
.refresh{background:#385261;color:#fff}
.reset{background:var(--danger);color:#fff}
.status-block{
  margin-top:18px;
  border:1px solid var(--border);
  border-radius:16px;
  background:#f7fbfd;
  padding:14px;
}
.status-top{
  display:flex;
  justify-content:space-between;
  gap:12px;
  align-items:center;
  flex-wrap:wrap;
}
.status-meta{
  font-size:12px;
  color:var(--muted);
}
details{margin-top:12px}
summary{
  cursor:pointer;
  color:var(--accent);
  font-weight:700;
}
pre{
  background:#0f172a;
  color:#e2e8f0;
  padding:12px;
  border-radius:12px;
  overflow:auto;
  margin:10px 0 0;
}
@media (max-width:700px){
  body{padding:12px}
  .card{padding:14px;border-radius:14px}
  .summary{grid-template-columns:repeat(2,minmax(0,1fr))}
}
</style>
</head>
<body>
<div class="card">
  <div class="hero">
    <div class="hero-copy">
      <h1>Waybeam Backpack</h1>
      <p>Debug-only bench UI for pins, routing timeouts, PWM mapping, and live runtime status.</p>
    </div>
    <div class="hero-chip">AP: <b>waybeam-backpack</b> (open, ch 6) at <b>10.100.0.1</b></div>
  </div>
  <div id="flash" class="flash" hidden></div>
  <div class="summary" id="summary"></div>
  <form id="cfg">
    <div class="sections" id="sections"></div>
    <div class="actions">
      <button class="apply" type="button" onclick="submitCfg(false)">Apply Live</button>
      <button class="save" type="button" onclick="submitCfg(true)">Apply + Save</button>
      <button class="reset" type="button" onclick="resetDefaults()">Reset to defaults</button>
      <button class="refresh" type="button" onclick="loadAll()">Refresh</button>
    </div>
  </form>
  <div class="status-block">
    <div class="status-top">
      <h2>Status</h2>
      <div class="status-meta" id="status-meta">Waiting for status...</div>
    </div>
    <details>
      <summary>Advanced Status JSON</summary>
      <pre id="status">loading...</pre>
    </details>
  </div>
</div>
<script>
const sections=[
{title:'Pins', note:'Valid configurable GPIOs are 0..10, 20, and 21. GPIO4/5 are reserved for the OLED. GPIO9 is the onboard BOOT button.', fields:[
{name:'led_pin',label:'LED pin',type:'number'},
{name:'ppm_input_pin',label:'PPM input pin',type:'number'},
{name:'mode_button_pin',label:'Mode button pin',type:'number'},
{name:'crsf_uart_tx_pin',label:'CRSF UART TX pin',type:'number'},
{name:'crsf_uart_rx_pin',label:'CRSF UART RX pin',type:'number'},
{name:'servo_pwm_1_pin',label:'Servo PWM 1 pin',type:'number'},
{name:'servo_pwm_2_pin',label:'Servo PWM 2 pin',type:'number'},
{name:'servo_pwm_3_pin',label:'Servo PWM 3 pin',type:'number'}
]},
{title:'Modes', note:'Live mode changes the active OLED/runtime screen immediately. Default mode selects the boot screen.', fields:[
{name:'output_mode_live',label:'Live screen',type:'select',options:[
['0','HDZero -> CRSF'],
['1','UART -> PWM'],
['3','CRSF TX 1-12'],
['2','Debug / Config']
]},
{name:'output_mode_default',label:'Boot default screen',type:'select',options:[
['0','HDZero -> CRSF'],
['1','UART -> PWM'],
['3','CRSF TX 1-12'],
['2','Debug / Config']
]}
]},
{title:'CRSF / UART', note:'Incoming CRSF drives PWM first. If PPM drops out, healthy CRSF can also take over USB output.', fields:[
{name:'crsf_uart_baud',label:'CRSF UART baud',type:'number'},
{name:'crsf_map_min_us',label:'PPM -> CRSF map min us',type:'number'},
{name:'crsf_map_max_us',label:'PPM -> CRSF map max us',type:'number'},
{name:'crsf_rx_timeout_ms',label:'CRSF RX stale timeout ms',type:'number'}
]},
{title:'Servo Outputs', note:'Servo sources apply to CRSF RX -> PWM routing. PPM fallback remains fixed PAN/ROL/TIL -> S1/S2/S3.', fields:[
{name:'servo_pwm_frequency_hz',label:'Servo PWM frequency Hz',type:'number'},
{name:'servo_map_1',label:'Servo 1 source channel',type:'select',options:[
['1','CH1'],['2','CH2'],['3','CH3'],['4','CH4'],['5','CH5'],['6','CH6'],['7','CH7'],['8','CH8'],
['9','CH9'],['10','CH10'],['11','CH11'],['12','CH12'],['13','CH13'],['14','CH14'],['15','CH15'],['16','CH16']
]},
{name:'servo_map_2',label:'Servo 2 source channel',type:'select',options:[
['1','CH1'],['2','CH2'],['3','CH3'],['4','CH4'],['5','CH5'],['6','CH6'],['7','CH7'],['8','CH8'],
['9','CH9'],['10','CH10'],['11','CH11'],['12','CH12'],['13','CH13'],['14','CH14'],['15','CH15'],['16','CH16']
]},
{name:'servo_map_3',label:'Servo 3 source channel',type:'select',options:[
['1','CH1'],['2','CH2'],['3','CH3'],['4','CH4'],['5','CH5'],['6','CH6'],['7','CH7'],['8','CH8'],
['9','CH9'],['10','CH10'],['11','CH11'],['12','CH12'],['13','CH13'],['14','CH14'],['15','CH15'],['16','CH16']
]},
{name:'servo_pulse_min_us',label:'Servo min pulse us',type:'number'},
{name:'servo_pulse_center_us',label:'Servo center pulse us',type:'number'},
{name:'servo_pulse_max_us',label:'Servo max pulse us',type:'number'}
]},
{title:'PPM Decode', note:'Headtracker PPM is also used for USB CRSF output and as PWM fallback when CRSF RX is stale.', fields:[
{name:'ppm_min_channel_us',label:'PPM min pulse us',type:'number'},
{name:'ppm_max_channel_us',label:'PPM max pulse us',type:'number'},
{name:'ppm_sync_gap_us',label:'PPM sync gap us',type:'number'}
]},
{title:'Timing / Health', note:'A source stays on its current route until it becomes stale. Route reacquire still requires a short burst of clean packets/frames.', fields:[
{name:'signal_timeout_ms',label:'PPM stale timeout ms',type:'number'},
{name:'button_debounce_ms',label:'Button debounce ms',type:'number'},
{name:'monitor_print_interval_ms',label:'Debug print interval ms',type:'number'}
]}
];

const summaryFields=[
  {label:'Screen', value:(s)=>s.output_mode_label||'--', sub:(s)=>`OLED ${s.oled_ready ? 'ready' : 'missing'}`},
  {label:'USB Route', value:(s)=>s.usb_route_label||'--', sub:(s)=>`AP ${s.web_ui_active ? 'active' : 'off'}`},
  {label:'PWM Route', value:(s)=>s.pwm_route_label||'--', sub:()=>`Outputs S1/S2/S3`},
  {label:'PPM Rate', value:(s)=>`${s.ppm_health_label || '--'} / ${formatHz(s.ppm_window_hz)}`, sub:(s)=>`Timeout ${readField('signal_timeout_ms','--')} ms`},
  {label:'CRSF RX', value:(s)=>`${s.crsf_rx_health_label || '--'} / ${s.crsf_rx_health_label==='RXOK' ? formatHz(s.crsf_rx_rate_hz) : '--'}`, sub:(s)=>`${s.crsf_rx_packets ?? 0} pkts / ${(s.crsf_rx_invalid ?? 0)} bad`},
  {label:'Servos', value:(s)=>formatServoSummary(s.servo_us), sub:()=>`0/1/2 default pins`}
];

function readField(name,fallback=''){
  const el=document.querySelector(`[name="${name}"]`);
  return el ? el.value : fallback;
}

function showFlash(message,isError=false){
  const el=document.getElementById('flash');
  el.hidden=!message;
  el.textContent=message || '';
  el.className=isError ? 'flash error' : 'flash';
}

function formatHz(value){
  if(value===undefined || value===null || value==='') return '--';
  const num=Number(value);
  return Number.isFinite(num) ? `${num.toFixed(1)} Hz` : '--';
}

function formatServoSummary(values){
  if(!Array.isArray(values) || values.length<3) return '--';
  return values.slice(0,3).join(' / ');
}

function renderSummary(status){
  const root=document.getElementById('summary');
  root.innerHTML='';
  summaryFields.forEach((field)=>{
    const card=document.createElement('div');
    card.className='stat';
    const label=document.createElement('div');
    label.className='stat-label';
    label.textContent=field.label;
    const value=document.createElement('div');
    value.className='stat-value';
    value.textContent=field.value(status);
    const sub=document.createElement('div');
    sub.className='stat-sub';
    sub.textContent=field.sub(status);
    card.appendChild(label);
    card.appendChild(value);
    card.appendChild(sub);
    root.appendChild(card);
  });
}

function buildForm(){
  const root=document.getElementById('sections');
  sections.forEach((section)=>{
    const block=document.createElement('div'); block.className='section';
    const head=document.createElement('div'); head.className='section-head';
    const titleWrap=document.createElement('div');
    const title=document.createElement('h3'); title.textContent=section.title;
    titleWrap.appendChild(title);
    if(section.note){
      const note=document.createElement('div');
      note.className='section-note';
      note.textContent=section.note;
      titleWrap.appendChild(note);
    }
    head.appendChild(titleWrap);
    const grid=document.createElement('div'); grid.className='grid';
    section.fields.forEach((field)=>{
      const row=document.createElement('div'); row.className='row';
      const l=document.createElement('label'); l.textContent=field.label;
      const i=(field.type==='select') ? document.createElement('select') : document.createElement('input');
      i.name=field.name;
      if(field.type==='select'){
        field.options.forEach(([value,label])=>{
          const opt=document.createElement('option');
          opt.value=value;
          opt.textContent=label;
          i.appendChild(opt);
        });
      }else{
        i.type=field.type || 'text';
        if(field.type==='number'){
          i.inputMode='numeric';
          i.step='1';
        }
      }
      row.appendChild(l); row.appendChild(i); grid.appendChild(row);
    });
    block.appendChild(head);
    block.appendChild(grid);
    root.appendChild(block);
  });
}

async function loadSettings(){
  const r=await fetch('/api/settings',{cache:'no-store'});
  if(!r.ok) throw new Error(await r.text());
  const s=await r.json();
  sections.forEach((section)=>{
    section.fields.forEach((field)=>{
      const el=document.querySelector(`[name="${field.name}"]`);
      if(el && s[field.name]!==undefined) el.value=s[field.name];
    });
  });
}

async function loadStatus(){
  const r=await fetch('/api/status',{cache:'no-store'});
  if(!r.ok) throw new Error(await r.text());
  const s=await r.json();
  renderSummary(s);
  document.getElementById('status-meta').textContent=`Updated ${new Date().toLocaleTimeString()}`;
  document.getElementById('status').textContent=JSON.stringify(s,null,2);
}

async function loadAll(){
  try{
    await loadSettings();
    await loadStatus();
    showFlash('');
  }catch(err){
    renderSummary({});
    document.getElementById('status-meta').textContent='Status unavailable';
    document.getElementById('status').textContent=`Error: ${err.message}`;
    showFlash(`Error: ${err.message}`, true);
  }
}

async function submitCfg(persist){
  const fd=new FormData(document.getElementById('cfg'));
  fd.set('persist', persist ? '1' : '0');
  const body=new URLSearchParams(fd);
  const r=await fetch('/api/settings', {method:'POST', body});
  const text=await r.text();
  showFlash(r.ok ? text : `Error: ${text}`, !r.ok);
  if(!r.ok) return;
  await loadAll();
}

async function resetDefaults(){
  if(!confirm('Reset all settings to firmware defaults and save?')) return;
  const r=await fetch('/api/reset_defaults', {method:'POST'});
  const text=await r.text();
  showFlash(r.ok ? text : `Error: ${text}`, !r.ok);
  if(!r.ok) return;
  await loadAll();
}

buildForm();
loadAll();
setInterval(()=>{
  loadStatus().catch((err)=>{
    renderSummary({});
    document.getElementById('status-meta').textContent='Status unavailable';
    document.getElementById('status').textContent=`Error: ${err.message}`;
    showFlash(`Status refresh failed: ${err.message}`, true);
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
bool gLedState = false;
uint32_t gLastLedToggleMs = 0;
uint32_t gLastDebugMessageMs = 0;
uint32_t gApStartRequestMs = 0;
uint32_t gApStartedMs = 0;
uint32_t gApLastStationJoinMs = 0;
uint32_t gApLastStationIpMs = 0;
uint32_t gApStationJoinCount = 0;
uint32_t gApStationLeaveCount = 0;
bool gButtonRawState = HIGH;
bool gButtonStableState = HIGH;
uint32_t gLastButtonChangeMs = 0;
uint32_t gButtonPressStartMs = 0;
bool gButtonLongPressHandled = false;
uint32_t gLatestFrameCounter = 0;
uint32_t gLatestInvalidPulseCounter = 0;
uint32_t gLastMonitorFrameCounter = 0;
uint32_t gLastMonitorInvalidPulseCounter = 0;
uint32_t gLastMonitorSampleMs = 0;
float gLastMeasuredWindowHz = 0.0f;
uint32_t gLastMeasuredInvalidDelta = 0;

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
uint32_t gLastCrsfRxWindowSampleMs = 0;
uint32_t gLastCrsfRxWindowPacketCounter = 0;
float gCrsfRxWindowHz = 0.0f;
bool gHasCrsfRxFrame = false;
Adafruit_SSD1306 gDisplay(kScreenWidth, kScreenHeight, &Wire, -1);
bool gOledReady = false;
uint32_t gLastOledRefreshMs = 0;

bool gPpmInterruptAttached = false;
uint8_t gAttachedPpmPin = PPM_INPUT_PIN;
uint8_t gAttachedLedPin = LED_PIN;
uint8_t gAttachedButtonPin = MODE_BUTTON_PIN;
uint8_t gAttachedServoPins[kServoCount] = {SERVO_PWM_1_PIN, SERVO_PWM_2_PIN, SERVO_PWM_3_PIN};

Preferences gPrefs;
bool gPrefsReady = false;
WebServer gWeb(80);
RuntimeSettings gSettings;
bool gWebUiActive = false;
bool gWebRoutesConfigured = false;

enum class OutputMode : uint8_t { kHdzeroCrsf = 0, kUartToPwm = 1, kDebugConfig = 2, kCrsfTx12 = 3 };
OutputMode gOutputMode = OutputMode::kCrsfTx12;
OutputMode gLastMainOutputMode = OutputMode::kCrsfTx12;
enum class UsbCrsfRoute : uint8_t { kNone = 0, kPpm = 1, kCrsfRx = 2 };
enum class PwmRoute : uint8_t { kCenter = 0, kCrsfRx = 1, kPpm = 2 };
enum class SourceHealth : uint8_t { kStale = 0, kTentative = 1, kHealthy = 2 };
uint32_t gRecentPpmFrameMs[kRouteEventHistorySize] = {0};
uint8_t gRecentPpmFrameCount = 0;
uint32_t gRecentCrsfPacketMs[kRouteEventHistorySize] = {0};
uint8_t gRecentCrsfPacketCount = 0;
UsbCrsfRoute gActiveUsbCrsfRoute = UsbCrsfRoute::kNone;
PwmRoute gActivePwmRoute = PwmRoute::kCenter;
uint32_t gLastUsbRouteChangeMs = 0;
uint32_t gLastPwmRouteChangeMs = 0;

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

  s.crsfMapMinUs = 1000;
  s.crsfMapMaxUs = 2000;
  s.crsfRxTimeoutMs = 500;

  s.servoPulseMinUs = 1000;
  s.servoPulseCenterUs = 1500;
  s.servoPulseMaxUs = 2000;

  s.outputModeDefault = static_cast<uint8_t>(OutputMode::kCrsfTx12);
  return s;
}

uint8_t outputModeToUint(OutputMode mode) { return static_cast<uint8_t>(mode); }

uint8_t clampU8(uint32_t value) {
  return (value > 255U) ? 255U : static_cast<uint8_t>(value);
}

bool isReservedOledPin(uint8_t pin) { return pin == OLED_SDA_PIN || pin == OLED_SCL_PIN; }

bool isDebugOutputMode(OutputMode mode) { return mode == OutputMode::kDebugConfig; }

bool isPpmFresh(uint32_t nowMs, uint32_t timeoutMs) {
  return gLastFrameMs != 0 && ((nowMs - gLastFrameMs) <= timeoutMs);
}

bool isCrsfFresh(uint32_t nowMs, uint32_t timeoutMs) {
  return gHasCrsfRxFrame && ((nowMs - gLastCrsfRxMs) <= timeoutMs);
}

bool isSourceStale(SourceHealth health) { return health == SourceHealth::kStale; }

uint32_t ppmRouteTimeoutMs() { return gSettings.signalTimeoutMs; }

uint32_t crsfRouteTimeoutMs() { return gSettings.crsfRxTimeoutMs; }

void recordRecentEvent(uint32_t history[kRouteEventHistorySize], uint8_t &count, uint32_t eventMs) {
  for (uint8_t i = kRouteEventHistorySize - 1; i > 0; --i) {
    history[i] = history[i - 1];
  }
  history[0] = eventMs;
  if (count < kRouteEventHistorySize) {
    count++;
  }
}

uint8_t countRecentEvents(const uint32_t history[kRouteEventHistorySize], uint8_t count, uint32_t nowMs, uint32_t windowMs) {
  uint8_t recent = 0;
  for (uint8_t i = 0; i < count; ++i) {
    if ((nowMs - history[i]) > windowMs) {
      break;
    }
    recent++;
  }
  return recent;
}

SourceHealth classifySourceHealth(bool hasSignal, uint32_t lastEventMs, const uint32_t history[kRouteEventHistorySize],
                                  uint8_t historyCount, uint32_t nowMs, uint32_t staleTimeoutMs,
                                  uint8_t healthyEventCount) {
  if (!hasSignal || ((nowMs - lastEventMs) > staleTimeoutMs)) {
    return SourceHealth::kStale;
  }
  return (countRecentEvents(history, historyCount, nowMs, kRouteAcquireWindowMs) >= healthyEventCount)
             ? SourceHealth::kHealthy
             : SourceHealth::kTentative;
}

SourceHealth ppmSourceHealth(uint32_t nowMs) {
  return classifySourceHealth(gLastFrameMs != 0, gLastFrameMs, gRecentPpmFrameMs, gRecentPpmFrameCount, nowMs,
                              ppmRouteTimeoutMs(), kPpmHealthyFrameCount);
}

SourceHealth crsfSourceHealth(uint32_t nowMs) {
  return classifySourceHealth(gHasCrsfRxFrame, gLastCrsfRxMs, gRecentCrsfPacketMs, gRecentCrsfPacketCount, nowMs,
                              crsfRouteTimeoutMs(), kCrsfHealthyPacketCount);
}

bool canKeepUsbRoute(UsbCrsfRoute route, SourceHealth ppmHealth, SourceHealth crsfHealth) {
  switch (route) {
    case UsbCrsfRoute::kPpm:
      return !isSourceStale(ppmHealth);
    case UsbCrsfRoute::kCrsfRx:
      return !isSourceStale(crsfHealth);
    case UsbCrsfRoute::kNone:
      return true;
  }
  return false;
}

bool canAcquireUsbRoute(UsbCrsfRoute route, SourceHealth ppmHealth, SourceHealth crsfHealth) {
  switch (route) {
    case UsbCrsfRoute::kPpm:
      return ppmHealth == SourceHealth::kHealthy;
    case UsbCrsfRoute::kCrsfRx:
      return crsfHealth == SourceHealth::kHealthy;
    case UsbCrsfRoute::kNone:
      return true;
  }
  return false;
}

bool canKeepPwmRoute(PwmRoute route, SourceHealth ppmHealth, SourceHealth crsfHealth) {
  switch (route) {
    case PwmRoute::kCrsfRx:
      return !isSourceStale(crsfHealth);
    case PwmRoute::kPpm:
      return !isSourceStale(ppmHealth);
    case PwmRoute::kCenter:
      return true;
  }
  return false;
}

bool canAcquirePwmRoute(PwmRoute route, SourceHealth ppmHealth, SourceHealth crsfHealth) {
  switch (route) {
    case PwmRoute::kCrsfRx:
      return crsfHealth == SourceHealth::kHealthy;
    case PwmRoute::kPpm:
      return ppmHealth == SourceHealth::kHealthy;
    case PwmRoute::kCenter:
      return true;
  }
  return false;
}

UsbCrsfRoute selectDesiredUsbCrsfRoute(uint32_t nowMs, bool usbTextMode, UsbCrsfRoute activeRoute) {
  if (usbTextMode) {
    return UsbCrsfRoute::kNone;
  }

  const SourceHealth ppmHealth = ppmSourceHealth(nowMs);
  const SourceHealth crsfHealth = crsfSourceHealth(nowMs);

  if (activeRoute == UsbCrsfRoute::kPpm && canKeepUsbRoute(activeRoute, ppmHealth, crsfHealth)) {
    return UsbCrsfRoute::kPpm;
  }
  if (activeRoute == UsbCrsfRoute::kCrsfRx && canKeepUsbRoute(activeRoute, ppmHealth, crsfHealth) &&
      !canAcquireUsbRoute(UsbCrsfRoute::kPpm, ppmHealth, crsfHealth)) {
    return UsbCrsfRoute::kCrsfRx;
  }
  if (canAcquireUsbRoute(UsbCrsfRoute::kPpm, ppmHealth, crsfHealth)) {
    return UsbCrsfRoute::kPpm;
  }
  if (canAcquireUsbRoute(UsbCrsfRoute::kCrsfRx, ppmHealth, crsfHealth)) {
    return UsbCrsfRoute::kCrsfRx;
  }
  if (canKeepUsbRoute(activeRoute, ppmHealth, crsfHealth)) {
    return activeRoute;
  }
  return UsbCrsfRoute::kNone;
}

PwmRoute selectDesiredPwmRoute(uint32_t nowMs, PwmRoute activeRoute) {
  const SourceHealth ppmHealth = ppmSourceHealth(nowMs);
  const SourceHealth crsfHealth = crsfSourceHealth(nowMs);

  if (activeRoute == PwmRoute::kCrsfRx && canKeepPwmRoute(activeRoute, ppmHealth, crsfHealth)) {
    return PwmRoute::kCrsfRx;
  }
  if (activeRoute == PwmRoute::kPpm && canKeepPwmRoute(activeRoute, ppmHealth, crsfHealth) &&
      !canAcquirePwmRoute(PwmRoute::kCrsfRx, ppmHealth, crsfHealth)) {
    return PwmRoute::kPpm;
  }
  if (canAcquirePwmRoute(PwmRoute::kCrsfRx, ppmHealth, crsfHealth)) {
    return PwmRoute::kCrsfRx;
  }
  if (canAcquirePwmRoute(PwmRoute::kPpm, ppmHealth, crsfHealth)) {
    return PwmRoute::kPpm;
  }
  if (canKeepPwmRoute(activeRoute, ppmHealth, crsfHealth)) {
    return activeRoute;
  }
  return PwmRoute::kCenter;
}

UsbCrsfRoute updateUsbCrsfRoute(uint32_t nowMs, bool usbTextMode) {
  const UsbCrsfRoute desiredRoute = selectDesiredUsbCrsfRoute(nowMs, usbTextMode, gActiveUsbCrsfRoute);
  if (desiredRoute == gActiveUsbCrsfRoute) {
    return gActiveUsbCrsfRoute;
  }
  const SourceHealth ppmHealth = ppmSourceHealth(nowMs);
  const SourceHealth crsfHealth = crsfSourceHealth(nowMs);
  const bool activeRouteLostSignal = !canKeepUsbRoute(gActiveUsbCrsfRoute, ppmHealth, crsfHealth);
  if (activeRouteLostSignal || gActiveUsbCrsfRoute == UsbCrsfRoute::kNone ||
      (nowMs - gLastUsbRouteChangeMs) >= kRouteSwitchHoldMs) {
    gActiveUsbCrsfRoute = desiredRoute;
    gLastUsbRouteChangeMs = nowMs;
  }
  return gActiveUsbCrsfRoute;
}

PwmRoute updatePwmRoute(uint32_t nowMs) {
  const PwmRoute desiredRoute = selectDesiredPwmRoute(nowMs, gActivePwmRoute);
  if (desiredRoute == gActivePwmRoute) {
    return gActivePwmRoute;
  }
  const SourceHealth ppmHealth = ppmSourceHealth(nowMs);
  const SourceHealth crsfHealth = crsfSourceHealth(nowMs);
  const bool activeRouteLostSignal = !canKeepPwmRoute(gActivePwmRoute, ppmHealth, crsfHealth);
  if (activeRouteLostSignal || gActivePwmRoute == PwmRoute::kCenter ||
      (nowMs - gLastPwmRouteChangeMs) >= kRouteSwitchHoldMs) {
    gActivePwmRoute = desiredRoute;
    gLastPwmRouteChangeMs = nowMs;
  }
  return gActivePwmRoute;
}

const char *outputModeLabel(OutputMode mode) {
  switch (mode) {
    case OutputMode::kHdzeroCrsf:
      return "HDZ>CRSF";
    case OutputMode::kUartToPwm:
      return "UART>PWM";
    case OutputMode::kCrsfTx12:
      return "CRSF TX12";
    case OutputMode::kDebugConfig:
      return "DEBUG";
  }
  return "UNK";
}

const char *usbCrsfRouteLabel(UsbCrsfRoute route) {
  switch (route) {
    case UsbCrsfRoute::kNone:
      return "NONE";
    case UsbCrsfRoute::kPpm:
      return "PPM";
    case UsbCrsfRoute::kCrsfRx:
      return "CRSF";
  }
  return "UNK";
}

const char *usbRouteReportLabel(UsbCrsfRoute route, bool usbTextMode) {
  return usbTextMode ? "TEXT" : usbCrsfRouteLabel(route);
}

const char *pwmRouteLabel(PwmRoute route) {
  switch (route) {
    case PwmRoute::kCenter:
      return "CENTER";
    case PwmRoute::kCrsfRx:
      return "CRSF";
    case PwmRoute::kPpm:
      return "PPM";
  }
  return "UNK";
}

const char *resetReasonLabel(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_UNKNOWN:
      return "UNKNOWN";
    case ESP_RST_POWERON:
      return "POWERON";
    case ESP_RST_EXT:
      return "EXT";
    case ESP_RST_SW:
      return "SW";
    case ESP_RST_PANIC:
      return "PANIC";
    case ESP_RST_INT_WDT:
      return "INT_WDT";
    case ESP_RST_TASK_WDT:
      return "TASK_WDT";
    case ESP_RST_WDT:
      return "WDT";
    case ESP_RST_DEEPSLEEP:
      return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:
      return "BROWNOUT";
    case ESP_RST_SDIO:
      return "SDIO";
    default:
      return "OTHER";
  }
}

const char *ppmHealthLabel(uint32_t nowMs) {
  if (gLastFrameMs == 0) {
    return "WAIT";
  }
  return isPpmFresh(nowMs, gSettings.signalTimeoutMs) ? "OK" : "STAL";
}

const char *crsfRxHealthLabel(uint32_t nowMs) {
  if (!gHasCrsfRxFrame) {
    return "NONE";
  }
  return isCrsfFresh(nowMs, gSettings.crsfRxTimeoutMs) ? "RXOK" : "STAL";
}

uint32_t crsfRxRateWindowMs() {
  return (gSettings.monitorPrintIntervalMs < kMinCrsfRxRateWindowMs) ? kMinCrsfRxRateWindowMs
                                                                     : gSettings.monitorPrintIntervalMs;
}

const char *wifiPowerLabel(wifi_power_t power) {
  switch (power) {
    case WIFI_POWER_19_5dBm:
      return "19.5dBm";
    case WIFI_POWER_19dBm:
      return "19dBm";
    case WIFI_POWER_18_5dBm:
      return "18.5dBm";
    case WIFI_POWER_17dBm:
      return "17dBm";
    case WIFI_POWER_15dBm:
      return "15dBm";
    case WIFI_POWER_13dBm:
      return "13dBm";
    case WIFI_POWER_11dBm:
      return "11dBm";
    case WIFI_POWER_8_5dBm:
      return "8.5dBm";
    case WIFI_POWER_7dBm:
      return "7dBm";
    case WIFI_POWER_5dBm:
      return "5dBm";
    case WIFI_POWER_2dBm:
      return "2dBm";
    case WIFI_POWER_MINUS_1dBm:
      return "-1dBm";
    default:
      return "unknown";
  }
}

float liveCrsfRxRateHz(uint32_t nowMs) {
  return isCrsfFresh(nowMs, gSettings.crsfRxTimeoutMs) ? gCrsfRxWindowHz : 0.0f;
}

void fillOutgoingCrsfChannelsFromPpm(uint16_t channels[kCrsfChannelCount]);
void fillOutgoingCrsfChannelsFromCrsfRx(uint16_t channels[kCrsfChannelCount]);

int16_t centeredFillPixels(uint16_t value, uint16_t minValue, uint16_t centerValue, uint16_t maxValue,
                           int16_t halfWidth) {
  if (halfWidth <= 0) {
    return 0;
  }
  uint16_t clampedValue = value;
  if (clampedValue < minValue) {
    clampedValue = minValue;
  } else if (clampedValue > maxValue) {
    clampedValue = maxValue;
  }

  if (clampedValue >= centerValue) {
    const uint32_t range = static_cast<uint32_t>(maxValue - centerValue);
    if (range == 0) {
      return 0;
    }
    return static_cast<int16_t>((static_cast<uint32_t>(clampedValue - centerValue) * halfWidth) / range);
  }

  const uint32_t range = static_cast<uint32_t>(centerValue - minValue);
  if (range == 0) {
    return 0;
  }
  return -static_cast<int16_t>((static_cast<uint32_t>(centerValue - clampedValue) * halfWidth) / range);
}

int16_t centeredMarkerOffset(uint16_t value, uint16_t minValue, uint16_t centerValue, uint16_t maxValue,
                             int16_t halfWidth) {
  int16_t offset = centeredFillPixels(value, minValue, centerValue, maxValue, halfWidth);
  if (offset > halfWidth) {
    offset = halfWidth;
  } else if (offset < -halfWidth) {
    offset = -halfWidth;
  }
  return offset;
}

void drawHeaderBar(const char *title, const char *status) {
  gDisplay.fillRect(0, 0, kScreenWidth, 10, SSD1306_WHITE);
  gDisplay.setTextSize(1);
  gDisplay.setTextColor(SSD1306_BLACK);
  gDisplay.setCursor(2, 1);
  gDisplay.print(title);
  if (status != nullptr) {
    int16_t x1 = 0;
    int16_t y1 = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    gDisplay.getTextBounds(status, 0, 0, &x1, &y1, &width, &height);
    gDisplay.setCursor(kScreenWidth - static_cast<int16_t>(width) - 2, 1);
    gDisplay.print(status);
  }
}

void drawCenteredGraphRow(int16_t y, const char *label, bool hasValue, uint16_t value, uint16_t minValue,
                          uint16_t centerValue, uint16_t maxValue) {
  constexpr int16_t kGraphX = 22;
  constexpr int16_t kGraphWidth = 76;
  constexpr int16_t kGraphHeight = 12;
  constexpr int16_t kValueX = 102;
  const int16_t centerX = kGraphX + (kGraphWidth / 2);
  const int16_t halfWidth = (kGraphWidth / 2) - 2;

  gDisplay.setTextColor(SSD1306_WHITE);
  gDisplay.setCursor(0, y + 2);
  gDisplay.print(label);
  gDisplay.drawRect(kGraphX, y, kGraphWidth, kGraphHeight, SSD1306_WHITE);
  gDisplay.drawFastVLine(kGraphX + 1, y + 3, kGraphHeight - 6, SSD1306_WHITE);
  gDisplay.drawFastVLine(kGraphX + (kGraphWidth / 4), y + 4, kGraphHeight - 8, SSD1306_WHITE);
  gDisplay.drawFastVLine(centerX, y + 1, kGraphHeight - 2, SSD1306_WHITE);
  gDisplay.drawFastVLine(kGraphX + ((kGraphWidth * 3) / 4), y + 4, kGraphHeight - 8, SSD1306_WHITE);
  gDisplay.drawFastVLine(kGraphX + kGraphWidth - 2, y + 3, kGraphHeight - 6, SSD1306_WHITE);

  if (hasValue) {
    const int16_t fill = centeredFillPixels(value, minValue, centerValue, maxValue, halfWidth);
    const int16_t markerX = centerX + centeredMarkerOffset(value, minValue, centerValue, maxValue, halfWidth);
    if (fill > 0) {
      gDisplay.fillRect(centerX + 1, y + 2, fill, kGraphHeight - 4, SSD1306_WHITE);
    } else if (fill < 0) {
      gDisplay.fillRect(centerX + fill, y + 2, -fill, kGraphHeight - 4, SSD1306_WHITE);
    }
    gDisplay.drawFastVLine(markerX, y + 1, kGraphHeight - 2, SSD1306_BLACK);
    gDisplay.drawPixel(markerX - 1, y + (kGraphHeight / 2), SSD1306_WHITE);
    gDisplay.drawPixel(markerX + 1, y + (kGraphHeight / 2), SSD1306_WHITE);
    gDisplay.setCursor(kValueX, y + 2);
    gDisplay.printf("%4u", static_cast<unsigned>(value));
    return;
  }

  gDisplay.setCursor(kValueX, y + 2);
  gDisplay.print("----");
}

void drawCompactCrsfChannelRow(int16_t x, int16_t y, uint8_t channelIndex, bool hasValue, uint16_t value) {
  constexpr int16_t kGraphXOffset = 14;
  constexpr int16_t kGraphWidth = 48;
  constexpr int16_t kGraphHeight = 7;
  const int16_t graphX = x + kGraphXOffset;
  const int16_t centerX = graphX + (kGraphWidth / 2);
  const int16_t halfWidth = (kGraphWidth / 2) - 2;

  gDisplay.setTextColor(SSD1306_WHITE);
  gDisplay.setCursor(x, y);
  gDisplay.printf("%02u", static_cast<unsigned>(channelIndex + 1));
  gDisplay.drawRect(graphX, y, kGraphWidth, kGraphHeight, SSD1306_WHITE);
  gDisplay.drawFastVLine(centerX, y + 1, kGraphHeight - 2, SSD1306_WHITE);

  if (!hasValue) {
    return;
  }

  const int16_t fill = centeredFillPixels(value, kCrsfMinValue, kCrsfCenterValue, kCrsfMaxValue, halfWidth);
  const int16_t markerX = centerX + centeredMarkerOffset(value, kCrsfMinValue, kCrsfCenterValue, kCrsfMaxValue, halfWidth);
  if (fill > 0) {
    gDisplay.fillRect(centerX + 1, y + 2, fill, kGraphHeight - 4, SSD1306_WHITE);
  } else if (fill < 0) {
    gDisplay.fillRect(centerX + fill, y + 2, -fill, kGraphHeight - 4, SSD1306_WHITE);
  }
  gDisplay.drawFastVLine(markerX, y + 1, kGraphHeight - 2, SSD1306_BLACK);
}

void drawHdzeroToCrsfScreen(uint32_t nowMs) {
  const uint16_t centerUs = static_cast<uint16_t>((gSettings.crsfMapMinUs + gSettings.crsfMapMaxUs) / 2U);
  drawHeaderBar("HDZ>CRSF", ppmHealthLabel(nowMs));
  drawCenteredGraphRow(14, "PAN", gLatestChannelCount >= 1, gLatestChannelCount >= 1 ? gLatestChannels[0] : 0,
                       gSettings.crsfMapMinUs, centerUs, gSettings.crsfMapMaxUs);
  drawCenteredGraphRow(30, "ROL", gLatestChannelCount >= 2, gLatestChannelCount >= 2 ? gLatestChannels[1] : 0,
                       gSettings.crsfMapMinUs, centerUs, gSettings.crsfMapMaxUs);
  drawCenteredGraphRow(46, "TIL", gLatestChannelCount >= 3, gLatestChannelCount >= 3 ? gLatestChannels[2] : 0,
                       gSettings.crsfMapMinUs, centerUs, gSettings.crsfMapMaxUs);
}

void drawUartToPwmScreen(uint32_t nowMs) {
  drawHeaderBar("UART>PWM", crsfRxHealthLabel(nowMs));
  drawCenteredGraphRow(14, "S1", true, gServoPulseUs[0], gSettings.servoPulseMinUs, gSettings.servoPulseCenterUs,
                       gSettings.servoPulseMaxUs);
  drawCenteredGraphRow(30, "S2", true, gServoPulseUs[1], gSettings.servoPulseMinUs, gSettings.servoPulseCenterUs,
                       gSettings.servoPulseMaxUs);
  drawCenteredGraphRow(46, "S3", true, gServoPulseUs[2], gSettings.servoPulseMinUs, gSettings.servoPulseCenterUs,
                       gSettings.servoPulseMaxUs);
}

void drawCrsfTx12Screen(uint32_t nowMs) {
  const UsbCrsfRoute usbRoute = gActiveUsbCrsfRoute;
  const bool ppmFresh = isPpmFresh(nowMs, gSettings.signalTimeoutMs);
  const bool crsfFresh = isCrsfFresh(nowMs, gSettings.crsfRxTimeoutMs);
  bool hasValue = false;
  uint16_t channels[kCrsfChannelCount] = {0};
  if (usbRoute == UsbCrsfRoute::kPpm && ppmFresh) {
    fillOutgoingCrsfChannelsFromPpm(channels);
    hasValue = true;
  } else if (usbRoute == UsbCrsfRoute::kCrsfRx && crsfFresh) {
    fillOutgoingCrsfChannelsFromCrsfRx(channels);
    hasValue = true;
  }

  drawHeaderBar("CRSF TX12", usbCrsfRouteLabel(usbRoute));
  for (uint8_t i = 0; i < 6; ++i) {
    const int16_t y = 11 + (static_cast<int16_t>(i) * 9);
    drawCompactCrsfChannelRow(0, y, i, hasValue, channels[i]);
    drawCompactCrsfChannelRow(64, y, static_cast<uint8_t>(i + 6), hasValue, channels[i + 6]);
  }
}

void drawDebugConfigScreen(uint32_t nowMs) {
  const bool crsfFresh = isCrsfFresh(nowMs, gSettings.crsfRxTimeoutMs);
  drawHeaderBar("DEBUG CFG", gWebUiActive ? "AP ON" : "AP OFF");
  gDisplay.setTextColor(SSD1306_WHITE);
  gDisplay.setCursor(0, 14);
  gDisplay.printf("PPM %s %4.1fHz win", ppmHealthLabel(nowMs), static_cast<double>(gLastMeasuredWindowHz));
  gDisplay.setCursor(0, 24);
  if (crsfFresh) {
    gDisplay.printf("CRSF %s %3.0fHz", crsfRxHealthLabel(nowMs), static_cast<double>(liveCrsfRxRateHz(nowMs)));
  } else if (gHasCrsfRxFrame) {
    gDisplay.printf("CRSF %s", crsfRxHealthLabel(nowMs));
  } else {
    gDisplay.print("CRSF NONE");
  }
  gDisplay.setCursor(0, 34);
  gDisplay.printf("AP %u.%u.%u.%u", kApIp[0], kApIp[1], kApIp[2], kApIp[3]);
  gDisplay.setCursor(0, 44);
  gDisplay.printf("PPM=%u S=%u,%u,%u", gSettings.ppmInputPin, gSettings.servoPwmPins[0], gSettings.servoPwmPins[1],
                  gSettings.servoPwmPins[2]);
  gDisplay.setCursor(0, 54);
  gDisplay.print("short 1/2/3 long dbg");
}

void refreshOledStatus(uint32_t nowMs) {
  if (!gOledReady) {
    return;
  }
  if ((nowMs - gLastOledRefreshMs) < kOledRefreshIntervalMs) {
    return;
  }
  gLastOledRefreshMs = nowMs;
  gDisplay.clearDisplay();

  if (gOutputMode == OutputMode::kHdzeroCrsf) {
    drawHdzeroToCrsfScreen(nowMs);
  } else if (gOutputMode == OutputMode::kUartToPwm) {
    drawUartToPwmScreen(nowMs);
  } else if (gOutputMode == OutputMode::kCrsfTx12) {
    drawCrsfTx12Screen(nowMs);
  } else {
    drawDebugConfigScreen(nowMs);
  }

  gDisplay.display();
}

void initOled() {
  Serial.printf("OLED I2C: SDA=%u, SCL=%u, addr=0x%02X\n", OLED_SDA_PIN, OLED_SCL_PIN, OLED_I2C_ADDRESS);
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
  Wire.setClock(kI2cClockHz);

  if (!gDisplay.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDRESS)) {
    Serial.println("WARN: SSD1306 init failed. Continuing without OLED.");
    gOledReady = false;
    return;
  }

  gOledReady = true;
  gDisplay.clearDisplay();
  gDisplay.setTextSize(1);
  gDisplay.setTextColor(SSD1306_WHITE);
  gDisplay.setCursor(0, 0);
  gDisplay.println("HDZero OLED ready");
  gDisplay.display();
  Serial.println("OLED status screen ready");
}

bool validateSettings(const RuntimeSettings &s, String &error);
void startDebugServices();
void stopDebugServices();
void setOutputMode(OutputMode mode, const char *source, bool force = false);
void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info);

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
  if (version != 2 && version != kSettingsVersion) {
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
  } else if (version == 2) {
    // Bump older settings records to the current schema while preserving any
    // explicit boot screen the user already saved.
    if (!saveSettingsToNvs(s)) {
      Serial.println("WARN: Failed to persist migrated settings");
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
    if (isReservedOledPin(field.pin)) {
      error = String(field.name) + " cannot use reserved OLED pin GPIO" + String(field.pin);
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
  if (s.outputModeDefault > 3) {
    error = "output_mode_default must be 0, 1, 2, or 3";
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

void fillOutgoingCrsfChannelsFromPpm(uint16_t channels[kCrsfChannelCount]) {
  for (uint8_t i = 0; i < kCrsfChannelCount; ++i) {
    channels[i] = kCrsfCenterValue;
  }

  const uint8_t count = (gLatestChannelCount < kCrsfChannelCount) ? gLatestChannelCount : kCrsfChannelCount;
  for (uint8_t i = 0; i < count; ++i) {
    channels[i] = mapPulseUsToCrsf(gLatestChannels[i]);
  }
}

void fillOutgoingCrsfChannelsFromCrsfRx(uint16_t channels[kCrsfChannelCount]) {
  for (uint8_t i = 0; i < kCrsfChannelCount; ++i) {
    channels[i] = gCrsfRxChannels[i];
  }
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

uint16_t mapPpmToServoUs(uint16_t pulseUs) {
  uint16_t clampedUs = pulseUs;
  if (clampedUs < gSettings.ppmMinChannelUs) {
    clampedUs = gSettings.ppmMinChannelUs;
  } else if (clampedUs > gSettings.ppmMaxChannelUs) {
    clampedUs = gSettings.ppmMaxChannelUs;
  }

  const uint32_t numerator =
      static_cast<uint32_t>(clampedUs - gSettings.ppmMinChannelUs) * static_cast<uint32_t>(gSettings.servoPulseMaxUs - gSettings.servoPulseMinUs);
  const uint32_t denominator = static_cast<uint32_t>(gSettings.ppmMaxChannelUs - gSettings.ppmMinChannelUs);
  return static_cast<uint16_t>(gSettings.servoPulseMinUs + ((numerator + (denominator / 2U)) / denominator));
}

uint32_t pulseUsToPwmDuty(uint16_t pulseUs) {
  const uint32_t periodUs = 1000000UL / static_cast<uint32_t>(gSettings.servoPwmFrequencyHz);
  const uint32_t maxDuty = (1UL << kServoPwmResolutionBits) - 1UL;
  return (static_cast<uint32_t>(pulseUs) * maxDuty + (periodUs / 2UL)) / periodUs;
}

void writeServoPulseUs(uint8_t servoIndex, uint16_t pulseUs, bool force = false) {
  if (servoIndex >= kServoCount) {
    return;
  }
  if (!force && gServoPulseUs[servoIndex] == pulseUs) {
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
    writeServoPulseUs(i, gSettings.servoPulseCenterUs, true);
    gAttachedServoPins[i] = gSettings.servoPwmPins[i];
  }
}

void applyPwmRoute(PwmRoute route) {
  if (route == PwmRoute::kCenter) {
    for (uint8_t i = 0; i < kServoCount; ++i) {
      writeServoPulseUs(i, gSettings.servoPulseCenterUs);
    }
    return;
  }

  for (uint8_t i = 0; i < kServoCount; ++i) {
    if (route == PwmRoute::kCrsfRx) {
      const uint8_t sourceCh = gSettings.servoMap[i];
      writeServoPulseUs(i, mapCrsfToServoUs(gCrsfRxChannels[sourceCh]));
      continue;
    }

    if (i < gLatestChannelCount) {
      writeServoPulseUs(i, mapPpmToServoUs(gLatestChannels[i]));
    } else {
      writeServoPulseUs(i, gSettings.servoPulseCenterUs);
    }
  }
}

void updateCrsfRxRateWindow(uint32_t nowMs) {
  if (gLastCrsfRxWindowSampleMs == 0) {
    gLastCrsfRxWindowSampleMs = nowMs;
    gLastCrsfRxWindowPacketCounter = gCrsfRxPacketCounter;
    return;
  }

  const uint32_t elapsedMs = nowMs - gLastCrsfRxWindowSampleMs;
  if (elapsedMs < crsfRxRateWindowMs()) {
    return;
  }

  const uint32_t packetDelta = gCrsfRxPacketCounter - gLastCrsfRxWindowPacketCounter;
  gCrsfRxWindowHz =
      (elapsedMs > 0) ? (static_cast<float>(packetDelta) * 1000.0f / static_cast<float>(elapsedMs)) : 0.0f;
  gLastCrsfRxWindowPacketCounter = gCrsfRxPacketCounter;
  gLastCrsfRxWindowSampleMs = nowMs;
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
  const uint32_t packetMs = millis();
  gLastCrsfRxMs = packetMs;
  recordRecentEvent(gRecentCrsfPacketMs, gRecentCrsfPacketCount, gLastCrsfRxMs);
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

void packCrsfRcPacket(const uint16_t channels[kCrsfChannelCount], uint8_t packet[kCrsfPacketSize]) {
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

  packet[0] = kCrsfAddress;
  packet[1] = 24; // type + payload + crc
  packet[2] = kCrsfFrameTypeRcChannelsPacked;
  for (uint8_t i = 0; i < kCrsfPayloadSize; ++i) {
    packet[3 + i] = payload[i];
  }
  packet[25] = crc8DvbS2(&packet[2], 1 + kCrsfPayloadSize);
}

void writeCrsfPacket(const uint8_t packet[kCrsfPacketSize], bool writeUsbSerial, bool writeHardwareUart) {
  if (writeUsbSerial) {
    Serial.write(packet, kCrsfPacketSize);
  }
  if (writeHardwareUart && gCrsfUartReady) {
    gCrsfUart.write(packet, kCrsfPacketSize);
  }
}

void sendPpmAsCrsfFrame(bool writeUsbSerial, bool writeHardwareUart) {
  uint16_t channels[kCrsfChannelCount];
  fillOutgoingCrsfChannelsFromPpm(channels);
  uint8_t packet[kCrsfPacketSize] = {0};
  packCrsfRcPacket(channels, packet);
  writeCrsfPacket(packet, writeUsbSerial, writeHardwareUart);
}

void sendCrsfRxToUsbFrame() {
  uint16_t channels[kCrsfChannelCount] = {0};
  for (uint8_t i = 0; i < kCrsfChannelCount; ++i) {
    channels[i] = gCrsfRxChannels[i];
  }
  uint8_t packet[kCrsfPacketSize] = {0};
  packCrsfRcPacket(channels, packet);
  writeCrsfPacket(packet, true, false);
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

  gLastFrameMs = millis();
  return true;
}

bool updatePpmMonitorWindow(uint32_t nowMs, uint32_t invalidPulses) {
  const uint32_t elapsedMs = nowMs - gLastMonitorSampleMs;
  if (elapsedMs < gSettings.monitorPrintIntervalMs) {
    return false;
  }

  const uint32_t frameCountDelta = gLatestFrameCounter - gLastMonitorFrameCounter;
  gLastMeasuredWindowHz =
      (elapsedMs > 0) ? (static_cast<float>(frameCountDelta) * 1000.0f / static_cast<float>(elapsedMs)) : 0.0f;
  gLastMeasuredInvalidDelta = invalidPulses - gLastMonitorInvalidPulseCounter;
  gLastMonitorFrameCounter = gLatestFrameCounter;
  gLastMonitorInvalidPulseCounter = invalidPulses;
  gLastMonitorSampleMs = nowMs;
  return true;
}

void printPpmMonitorFrame(uint32_t invalidPulses) {
  const uint32_t nowMs = millis();
  const uint32_t crsfRxAgeMs = gHasCrsfRxFrame ? (nowMs - gLastCrsfRxMs) : 0;
  const bool crsfRxFresh = isCrsfFresh(nowMs, gSettings.crsfRxTimeoutMs);
  const bool usbTextMode = isDebugOutputMode(gOutputMode);
  const UsbCrsfRoute usbRoute = gActiveUsbCrsfRoute;
  const PwmRoute pwmRoute = gActivePwmRoute;

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

  Serial.printf("PPM: ch=%u hz=%.1fHz invalid=%lu(+%lu) span=%u..%u", gLatestChannelCount,
                static_cast<double>(gLastMeasuredWindowHz),
                static_cast<unsigned long>(invalidPulses),
                static_cast<unsigned long>(gLastMeasuredInvalidDelta), static_cast<unsigned>(minChannelUs),
                static_cast<unsigned>(maxChannelUs));
  Serial.printf(" | crsf_rx=%s age=%lums pkts=%lu bad=%lu",
                crsfRxFresh ? "ok" : (gHasCrsfRxFrame ? "stale" : "none"),
                static_cast<unsigned long>(crsfRxAgeMs), static_cast<unsigned long>(gCrsfRxPacketCounter),
                static_cast<unsigned long>(gCrsfRxInvalidCounter));
  Serial.printf(" | route usb=%s pwm=%s", usbRouteReportLabel(usbRoute, usbTextMode), pwmRouteLabel(pwmRoute));
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
  if (gOutputMode == OutputMode::kDebugConfig) {
    setOutputMode(gLastMainOutputMode, "button-short");
    return;
  }
  OutputMode nextMode = OutputMode::kHdzeroCrsf;
  if (gOutputMode == OutputMode::kHdzeroCrsf) {
    nextMode = OutputMode::kUartToPwm;
  } else if (gOutputMode == OutputMode::kUartToPwm) {
    nextMode = OutputMode::kCrsfTx12;
  }
  setOutputMode(nextMode, "button-short");
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
    gButtonPressStartMs = nowMs;
    gButtonLongPressHandled = false;
    return;
  }

  const uint32_t pressDurationMs = nowMs - gButtonPressStartMs;
  if (!gButtonLongPressHandled && pressDurationMs < kButtonLongPressMs) {
    toggleOutputMode();
  }
}

void pollButtonLongPress() {
  if (gButtonStableState != LOW || gButtonLongPressHandled) {
    return;
  }

  const uint32_t nowMs = millis();
  if ((nowMs - gButtonPressStartMs) >= kButtonLongPressMs) {
    gButtonLongPressHandled = true;
    setOutputMode(OutputMode::kDebugConfig, "button-long");
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
  gButtonPressStartMs = (gButtonStableState == LOW) ? millis() : 0;
  gButtonLongPressHandled = false;

  pinMode(gSettings.ppmInputPin, INPUT_PULLDOWN);
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

  gLatestChannelCount = 0;
  gLastFrameMs = 0;
  gLatestFrameCounter = 0;
  gLatestInvalidPulseCounter = 0;
  gLastMonitorFrameCounter = 0;
  gLastMonitorInvalidPulseCounter = 0;
  gLastMonitorSampleMs = 0;
  gLastMeasuredWindowHz = 0.0f;
  gLastMeasuredInvalidDelta = 0;

  gCrsfRxFramePos = 0;
  gCrsfRxFrameExpected = 0;
  gLastCrsfRxMs = 0;
  gLastCrsfRxWindowSampleMs = 0;
  gLastCrsfRxWindowPacketCounter = 0;
  gCrsfRxWindowHz = 0.0f;
  gHasCrsfRxFrame = false;

  for (uint8_t i = 0; i < kRouteEventHistorySize; ++i) {
    gRecentPpmFrameMs[i] = 0;
    gRecentCrsfPacketMs[i] = 0;
  }
  gRecentPpmFrameCount = 0;
  gRecentCrsfPacketCount = 0;
  gActiveUsbCrsfRoute = UsbCrsfRoute::kNone;
  gActivePwmRoute = PwmRoute::kCenter;
  gLastUsbRouteChangeMs = 0;
  gLastPwmRouteChangeMs = 0;

  if (oldSettings.monitorPrintIntervalMs != gSettings.monitorPrintIntervalMs) {
    gLastMonitorSampleMs = 0;
    gLastMeasuredWindowHz = 0.0f;
    gLastMeasuredInvalidDelta = 0;
    gLastCrsfRxWindowSampleMs = 0;
    gLastCrsfRxWindowPacketCounter = gCrsfRxPacketCounter;
    gCrsfRxWindowHz = 0.0f;
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
  const bool usbTextMode = isDebugOutputMode(gOutputMode);
  const char *ppmHealth = ppmHealthLabel(nowMs);
  const char *crsfHealth = crsfRxHealthLabel(nowMs);
  const float crsfRateHz = liveCrsfRxRateHz(nowMs);
  const uint32_t crsfAgeMs = gHasCrsfRxFrame ? (nowMs - gLastCrsfRxMs) : 0;
  const UsbCrsfRoute usbRoute = gActiveUsbCrsfRoute;
  const PwmRoute pwmRoute = gActivePwmRoute;

  String json;
  json.reserve(560);
  json = "{";
  json += "\"ap_ssid\":\"" + String(kApSsid) + "\"";
  json += ",\"ap_channel\":" + String(kApChannel);
  json += ",\"ap_ip\":\"" + WiFi.softAPIP().toString() + "\"";
  json += ",\"stations\":" + String(WiFi.softAPgetStationNum());
  json += ",\"ap_start_request_ms\":" + String(gApStartRequestMs);
  json += ",\"ap_started_ms\":" + String(gApStartedMs);
  json += ",\"ap_last_station_join_ms\":" + String(gApLastStationJoinMs);
  json += ",\"ap_last_station_ip_ms\":" + String(gApLastStationIpMs);
  json += ",\"web_ui_active\":" + String(gWebUiActive ? 1 : 0);
  json += ",\"nvs_ready\":" + String(gPrefsReady ? 1 : 0);
  json += ",\"oled_ready\":" + String(gOledReady ? 1 : 0);
  json += ",\"oled_sda_pin\":" + String(OLED_SDA_PIN);
  json += ",\"oled_scl_pin\":" + String(OLED_SCL_PIN);
  json += ",\"oled_i2c_addr\":\"0x" + String(OLED_I2C_ADDRESS, HEX) + "\"";
  json += ",\"output_mode\":" + String(outputModeToUint(gOutputMode));
  json += ",\"output_mode_label\":\"" + String(outputModeLabel(gOutputMode)) + "\"";
  json += ",\"ppm_health_label\":\"" + String(ppmHealth) + "\"";
  json += ",\"frame_hz\":" + String(gLastMeasuredWindowHz, 2);
  json += ",\"frame_hz_ema\":" + String(gLastMeasuredWindowHz, 2);
  json += ",\"ppm_window_hz\":" + String(gLastMeasuredWindowHz, 2);
  json += ",\"crsf_rx_health_label\":\"" + String(crsfHealth) + "\"";
  json += ",\"crsf_rx_packets\":" + String(gCrsfRxPacketCounter);
  json += ",\"crsf_rx_invalid\":" + String(gCrsfRxInvalidCounter);
  json += ",\"crsf_rx_age_ms\":" + String(crsfAgeMs);
  json += ",\"crsf_rx_rate_hz\":" + String(crsfRateHz, 2);
  json += ",\"usb_route_label\":\"" + String(usbRouteReportLabel(usbRoute, usbTextMode)) + "\"";
  json += ",\"pwm_route_label\":\"" + String(pwmRouteLabel(pwmRoute)) + "\"";
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
  if (!parseUIntArgOptional("output_mode_default", 0, 3, tmp, error)) {
    gWeb.send(400, "text/plain", error);
    return;
  }
  candidate.outputModeDefault = static_cast<uint8_t>(tmp);

  uint8_t requestedLiveMode = outputModeToUint(gOutputMode);
  tmp = requestedLiveMode;
  if (!parseUIntArgOptional("output_mode_live", 0, 3, tmp, error)) {
    gWeb.send(400, "text/plain", error);
    return;
  }
  requestedLiveMode = static_cast<uint8_t>(tmp);

  if (!validateSettings(candidate, error)) {
    gWeb.send(400, "text/plain", error);
    return;
  }

  applySettings(candidate);
  const OutputMode requestedMode = static_cast<OutputMode>(requestedLiveMode);

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
  setOutputMode(requestedMode, "web", true);
}

void handleResetDefaults() {
  RuntimeSettings defaults = defaultSettings();
  String error;
  if (!validateSettings(defaults, error)) {
    gWeb.send(500, "text/plain", String("Default settings invalid: ") + error);
    return;
  }

  applySettings(defaults);
  const OutputMode defaultMode = static_cast<OutputMode>(defaults.outputModeDefault);
  const bool saved = saveSettingsToNvs(gSettings);
  setNoCacheHeaders();
  if (!saved) {
    gWeb.send(500, "text/plain", "Reset to defaults and applied live, but failed to save to NVS");
    return;
  }
  gWeb.send(200, "text/plain", "Reset to defaults, applied live, and saved");
  setOutputMode(defaultMode, "reset", true);
}

void configureWebUiRoutes() {
  if (gWebRoutesConfigured) {
    return;
  }
  gWeb.on("/", HTTP_GET, handleRoot);
  gWeb.on("/api/settings", HTTP_GET, handleGetSettings);
  gWeb.on("/api/settings", HTTP_POST, handlePostSettings);
  gWeb.on("/api/reset_defaults", HTTP_POST, handleResetDefaults);
  gWeb.on("/api/status", HTTP_GET, handleGetStatus);
  gWeb.onNotFound([]() { gWeb.send(404, "text/plain", "Not Found"); });
  gWebRoutesConfigured = true;
}

void startDebugServices() {
  if (gWebUiActive) {
    return;
  }
  configureWebUiRoutes();
  gApStartRequestMs = millis();
  gApStartedMs = 0;
  gApLastStationJoinMs = 0;
  gApLastStationIpMs = 0;
  const String requestedApIp = kApIp.toString();
  Serial.printf("Debug/config AP start requested: ssid=%s ch=%u ip=%s tx=%s\n", kApSsid,
                static_cast<unsigned>(kApChannel), requestedApIp.c_str(),
                wifiPowerLabel(WIFI_POWER_19_5dBm));
  WiFi.persistent(false);
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(50);
  WiFi.setSleep(false);
  WiFi.mode(WIFI_AP);
  delay(50);
  if (!WiFi.softAPConfig(kApIp, kApGateway, kApSubnet)) {
    Serial.println("WARN: Failed to configure AP network settings");
  }
  if (!WiFi.softAP(kApSsid, nullptr, kApChannel)) {
    Serial.println("WARN: Failed to start SoftAP");
  }
  if (!WiFi.setTxPower(WIFI_POWER_19_5dBm)) {
    Serial.println("WARN: Failed to raise WiFi TX power");
  }
  delay(100);
  gWeb.begin();
  gWebUiActive = true;
  const String liveApIp = WiFi.softAPIP().toString();
  Serial.printf("Debug/config AP configured: ip=%s tx=%s\n", liveApIp.c_str(),
                wifiPowerLabel(WiFi.getTxPower()));
}

void stopDebugServices() {
  if (!gWebUiActive) {
    return;
  }
  gWeb.stop();
  WiFi.softAPdisconnect(true);
  delay(20);
  WiFi.mode(WIFI_OFF);
  gWebUiActive = false;
  Serial.println("Debug/config AP disabled");
}

void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  const uint32_t nowMs = millis();
  switch (event) {
    case ARDUINO_EVENT_WIFI_AP_START:
      gApStartedMs = nowMs;
      Serial.printf("AP event: START after %lums\n", static_cast<unsigned long>(nowMs - gApStartRequestMs));
      break;
    case ARDUINO_EVENT_WIFI_AP_STOP:
      Serial.println("AP event: STOP");
      break;
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
      gApLastStationJoinMs = nowMs;
      gApStationJoinCount++;
      Serial.printf("AP event: STA connected after %lums (joins=%lu)\n",
                    static_cast<unsigned long>(nowMs - gApStartRequestMs),
                    static_cast<unsigned long>(gApStationJoinCount));
      break;
    case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
      gApLastStationIpMs = nowMs;
      Serial.printf("AP event: STA got IP after %lums\n",
                    static_cast<unsigned long>(nowMs - gApStartRequestMs));
      break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
      gApStationLeaveCount++;
      Serial.printf("AP event: STA disconnected (leaves=%lu)\n", static_cast<unsigned long>(gApStationLeaveCount));
      break;
    default:
      break;
  }
}

void setOutputMode(OutputMode mode, const char *source, bool force) {
  if (!force && gOutputMode == mode) {
    return;
  }
  if (!isDebugOutputMode(mode)) {
    gLastMainOutputMode = mode;
  }
  gOutputMode = mode;
  if (isDebugOutputMode(gOutputMode)) {
    gLastMonitorFrameCounter = gLatestFrameCounter;
    gLastMonitorInvalidPulseCounter = gLatestInvalidPulseCounter;
    gLastMonitorSampleMs = millis();
    gLastMeasuredWindowHz = 0.0f;
    gLastMeasuredInvalidDelta = 0;
    gLastDebugMessageMs = millis();
    startDebugServices();
  } else {
    stopDebugServices();
  }
  Serial.printf("Screen -> %s (%s)\n", outputModeLabel(gOutputMode), source);
  gLastOledRefreshMs = 0;
}

} // namespace

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.println("Boot: hdzero-headtracker-monitor");
  const esp_reset_reason_t resetReason = esp_reset_reason();
  Serial.printf("Reset reason: %s (%d)\n", resetReasonLabel(resetReason), static_cast<int>(resetReason));
  Serial.printf("Pins: PPM=%u LED=%u BTN=%u UART_RX=%u UART_TX=%u SERVO=%u,%u,%u\n", PPM_INPUT_PIN, LED_PIN,
                MODE_BUTTON_PIN, CRSF_UART_RX_PIN, CRSF_UART_TX_PIN, SERVO_PWM_1_PIN, SERVO_PWM_2_PIN,
                SERVO_PWM_3_PIN);
  WiFi.onEvent(onWiFiEvent);
  gPrefsReady = gPrefs.begin("waybeam", false);
  if (!gPrefsReady) {
    Serial.println("WARN: Preferences init failed; persistence disabled");
  }

  gSettings = loadSettingsFromNvs();
  applySettings(gSettings);
  setOutputMode(static_cast<OutputMode>(gSettings.outputModeDefault), "boot", true);
  initOled();
  refreshOledStatus(millis());
}

void loop() {
  if (gWebUiActive) {
    gWeb.handleClient();
  }

  uint32_t invalidPulses = 0;
  const bool frameReady = copyFrameFromIsr(invalidPulses);
  if (frameReady) {
    recordRecentEvent(gRecentPpmFrameMs, gRecentPpmFrameCount, gLastFrameMs);
  }
  const uint32_t crsfRxPacketCounterBefore = gCrsfRxPacketCounter;
  processCrsfRxInput();
  handleModeButton();
  pollButtonLongPress();
  const uint32_t nowMs = millis();
  updateCrsfRxRateWindow(nowMs);
  const SourceHealth ppmHealth = ppmSourceHealth(nowMs);
  const bool crsfRxUpdated = (gCrsfRxPacketCounter != crsfRxPacketCounterBefore);
  const bool usbTextMode = isDebugOutputMode(gOutputMode);
  const UsbCrsfRoute usbRoute = updateUsbCrsfRoute(nowMs, usbTextMode);
  const PwmRoute pwmRoute = updatePwmRoute(nowMs);
  applyPwmRoute(pwmRoute);
  const bool monitorWindowUpdated =
      usbTextMode && frameReady && updatePpmMonitorWindow(nowMs, invalidPulses);
  refreshOledStatus(nowMs);

  if (frameReady) {
    sendPpmAsCrsfFrame(usbRoute == UsbCrsfRoute::kPpm, !isSourceStale(ppmHealth));
  }
  if (crsfRxUpdated && usbRoute == UsbCrsfRoute::kCrsfRx) {
    sendCrsfRxToUsbFrame();
  }
  if (monitorWindowUpdated) {
    printPpmMonitorFrame(invalidPulses);
  }

  updateLed();

  if (isDebugOutputMode(gOutputMode) && (nowMs - gLastFrameMs) > gSettings.signalTimeoutMs &&
      (nowMs - gLastDebugMessageMs) > 1000) {
    gLastDebugMessageMs = nowMs;
    Serial.println("No PPM frame detected yet. Check HT enabled + wiring.");
  }

  if (gWebUiActive) {
    delay(1);
  }
}
