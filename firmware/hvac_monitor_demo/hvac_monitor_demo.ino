/*
  HVAC Filter Health Monitor - SINGLE BMP280 version
  ESP32 + 1x BMP280

  How it works:
    The sensor measures pressure at ONE point (downstream of the filter,
    on the fan's suction side). With the fan OFF that pressure equals room
    pressure, and it is stored as the baseline. With the fan ON, a dirtier
    filter pulls the downstream pressure further below the baseline:
        deltaP = baseline - pressure
    (If your sensor is on the fan's pressure side, set SENSOR_BELOW_AMBIENT = false.)

  Wiring (3V3 only):
    ESP32 3V3 -> VCC     ESP32 GND -> GND
    ESP32 GPIO25 -> SDA  ESP32 GPIO26 -> SCL
    SDO -> GND (address 0x76)    CSB -> 3V3 if exposed

  Limitation: room pressure drifts with weather (100-300 Pa over a day).
  Re-tare (fan OFF) whenever the dashboard shows an old baseline age,
  and always before judging the filter.

  Serial Monitor: 115200. Dashboard: http://<IP printed>/
*/

#include <Wire.h>
#include <Adafruit_BMP280.h>
#include <WiFi.h>
#include <WebServer.h>
#include "secrets.h"   // copy secrets.example.h to secrets.h and fill in your WiFi

// ---------------- CONFIG ----------------
// WIFI_SSID and WIFI_PASSWORD come from secrets.h (not committed to git)

const uint8_t SENSOR_ADDR = 0x76;
const int SDA_PIN = 25;
const int SCL_PIN = 26;

const bool SENSOR_BELOW_AMBIENT = true;  // true: sensor downstream of filter on fan suction side

const float DP_WARNING  = 400.0;   // Pa
const float DP_CRITICAL = 500.0;   // Pa
const float DP_HYST     = 20.0;    // Pa

const bool TARE_AT_BOOT = true;    // fan must be OFF at power-up
const int  TARE_SAMPLES = 60;

const int  MA_WINDOW = 20;
const unsigned long SAMPLE_MS = 100;
const unsigned long RETRY_MS  = 5000;
const unsigned long STALE_BASELINE_S = 6UL * 3600UL;  // warn after 6 h
// ----------------------------------------

Adafruit_BMP280 bmp;
WebServer server(80);

bool sensorOk = false;
bool tared = false;
bool tareRequested = false;
float baseline = 0.0;
unsigned long tareTimeMs = 0;

float pressure = 0.0;
float smoothedDP = 0.0;
int   statusLevel = 0;   // 0 CLEAN, 1 WARNING, 2 CRITICAL

float maBuffer[MA_WINDOW];
int   maIndex = 0;
int   maCount = 0;

unsigned long lastSample = 0;
unsigned long lastRetry  = 0;

// ---------------- Smoothing ----------------
void resetAverage() { maIndex = 0; maCount = 0; }

float movingAvg(float val) {
  maBuffer[maIndex] = val;
  maIndex = (maIndex + 1) % MA_WINDOW;
  if (maCount < MA_WINDOW) maCount++;
  float sum = 0;
  for (int i = 0; i < maCount; i++) sum += maBuffer[i];
  return sum / maCount;
}

// ---------------- Status ----------------
void updateStatus() {
  float dp = smoothedDP;
  switch (statusLevel) {
    case 0: if (dp >= DP_WARNING) statusLevel = 1; break;
    case 1:
      if (dp >= DP_CRITICAL) statusLevel = 2;
      else if (dp < DP_WARNING - DP_HYST) statusLevel = 0;
      break;
    case 2: if (dp < DP_CRITICAL - DP_HYST) statusLevel = 1; break;
  }
}

String getStatus() {
  if (!sensorOk) return "ERROR";
  if (!tared) return "NOTARE";
  if (statusLevel == 2) return "CRITICAL";
  if (statusLevel == 1) return "WARNING";
  return "CLEAN";
}

// ---------------- Sensor ----------------
bool beginSensor() {
  const uint8_t ids[] = {0x58, 0x60, 0x56, 0x57};
  for (uint8_t id : ids) {
    if (bmp.begin(SENSOR_ADDR, id)) {
      Serial.printf("[OK] Sensor at 0x%02X ready (chip ID 0x%02X)\n", SENSOR_ADDR, id);
      bmp.setSampling(
        Adafruit_BMP280::MODE_NORMAL,
        Adafruit_BMP280::SAMPLING_X2,
        Adafruit_BMP280::SAMPLING_X16,
        Adafruit_BMP280::FILTER_X16,
        Adafruit_BMP280::STANDBY_MS_63
      );
      return true;
    }
  }
  return false;
}

void tareSensor() {
  Serial.println("[TARE] Measuring room baseline (keep fan OFF)...");
  float sum = 0;
  for (int i = 0; i < TARE_SAMPLES; i++) {
    sum += bmp.readPressure();
    delay(40);
  }
  baseline = sum / TARE_SAMPLES;
  tared = true;
  tareTimeMs = millis();
  resetAverage();
  statusLevel = 0;
  smoothedDP = 0;
  Serial.printf("[TARE] Baseline = %.2f Pa\n", baseline);
}

// ---------------- Web handlers ----------------
void handleData() {
  unsigned long baseAge = tared ? (millis() - tareTimeMs) / 1000 : 0;
  String json = "{";
  json += "\"pressure\":" + String(pressure, 2) + ",";
  json += "\"baseline\":" + String(baseline, 2) + ",";
  json += "\"delta_p\":" + String(smoothedDP, 2) + ",";
  json += "\"status\":\"" + getStatus() + "\",";
  json += "\"sensor_ok\":" + String(sensorOk ? "true" : "false") + ",";
  json += "\"tared\":" + String(tared ? "true" : "false") + ",";
  json += "\"baseline_age_s\":" + String(baseAge) + ",";
  json += "\"stale_after_s\":" + String(STALE_BASELINE_S) + ",";
  json += "\"wifi_rssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"fan_control\":\"MANUAL\",";
  json += "\"uptime_s\":" + String(millis() / 1000);
  json += "}";
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

void handleTare() {
  tareRequested = true;
  server.send(200, "application/json", "{\"tare\":\"requested\"}");
}

void handleRoot() {
  static const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1.0">
<title>HVAC Filter Health Monitor</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:Arial,sans-serif;background:#0f1117;color:#e0e0e0;min-height:100vh}
header{background:#1a1d27;border-bottom:2px solid #2563eb;padding:16px 24px;display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:8px}
header h1{font-size:1.1rem;color:#fff}
.dot{width:9px;height:9px;border-radius:50%;background:#22c55e;display:inline-block;margin-right:7px}
#ts{font-size:.75rem;color:#6b7280}
main{padding:20px 24px;max-width:900px;margin:auto}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:14px;margin-bottom:20px}
.card{background:#1a1d27;border:1px solid #2a2d3e;border-radius:12px;padding:18px}
.card label{display:block;font-size:.68rem;text-transform:uppercase;letter-spacing:1px;color:#6b7280;margin-bottom:8px}
.val{font-size:1.9rem;font-weight:700;color:#fff}
.unit{font-size:.85rem;color:#9ca3af;margin-left:3px}
.badge{display:inline-block;padding:5px 16px;border-radius:999px;font-weight:700;font-size:.95rem;letter-spacing:1px;margin-top:4px}
.CLEAN{background:#14532d;color:#4ade80;border:1px solid #16a34a}
.WARNING{background:#713f12;color:#fbbf24;border:1px solid #d97706}
.CRITICAL{background:#7f1d1d;color:#f87171;border:1px solid #dc2626}
.ERROR,.NOTARE{background:#3f3f46;color:#e4e4e7;border:1px solid #71717a}
.manual{background:#1e3a8a;color:#93c5fd;font-weight:700;padding:5px 12px;border-radius:6px;font-size:.85rem}
.chart-box,.thresh{background:#1a1d27;border:1px solid #2a2d3e;border-radius:12px;padding:20px}
.thresh{margin-top:16px}
h2{font-size:.78rem;color:#9ca3af;text-transform:uppercase;letter-spacing:1px;margin-bottom:14px}
canvas{width:100%!important}
table{width:100%;border-collapse:collapse;font-size:.83rem}
td,th{padding:8px 12px;text-align:left;border-bottom:1px solid #2a2d3e}
th{color:#6b7280;font-weight:400}
tr:last-child td{border-bottom:none}
.note{margin-top:15px;padding:12px;background:#172033;border-left:3px solid #2563eb;color:#9ca3af;font-size:.85rem}
button{background:#2563eb;color:#fff;border:0;border-radius:8px;padding:9px 16px;font-size:.85rem;cursor:pointer;margin-top:12px}
button:hover{background:#1d4ed8}
#warn{display:none;margin-bottom:16px;padding:12px;background:#3f3f46;border-left:3px solid #f87171;font-size:.9rem}
.hr{display:flex;align-items:center;gap:16px;flex-wrap:wrap}
.sw{display:flex;align-items:center;gap:6px;font-size:.85rem;color:#e0e0e0;cursor:pointer}
.sw input{width:16px;height:16px;accent-color:#2563eb}
#demoBanner{display:none;margin-bottom:16px;padding:12px;background:#172033;border-left:3px solid #fbbf24;font-size:.85rem}
</style>
</head>
<body>
<header>
<h1><span class="dot"></span>HVAC Filter Health Monitor</h1>
<div class="hr"><label class="sw"><input type="checkbox" id="demoToggle">Demo mode</label><span id="ts">Connecting...</span></div>
</header>
<main>
<div id="demoBanner">Demo mode is ON. These values are simulated dusty readings (WARNING and CRITICAL range), not from the sensor. Turn Demo mode off to see the real readings.</div>
<div id="warn"></div>
<div class="grid">
<div class="card"><label>Differential Pressure</label><span class="val" id="dp">&mdash;</span><span class="unit">Pa</span></div>
<div class="card"><label>Downstream Pressure</label><span class="val" id="p">&mdash;</span><span class="unit">Pa</span></div>
<div class="card"><label>Room Baseline</label><span class="val" id="bl">&mdash;</span><span class="unit">Pa</span></div>
<div class="card"><label>Filter Status</label><span class="badge CLEAN" id="status">&mdash;</span></div>
<div class="card"><label>Baseline Age</label><span class="val" id="age">&mdash;</span></div>
<div class="card"><label>Fan Control</label><span class="manual">MANUAL</span></div>
</div>
<div class="chart-box">
<h2>&Delta;P Live Chart</h2>
<canvas id="chart" height="200"></canvas>
</div>
<div class="thresh">
<h2>Threshold Reference</h2>
<table>
<tr><th>&Delta;P Range</th><th>Status</th><th>Action</th></tr>
<tr><td>&lt; 400 Pa</td><td style="color:#4ade80">CLEAN</td><td>Normal operation</td></tr>
<tr><td>400 &ndash; 500 Pa</td><td style="color:#fbbf24">WARNING</td><td>Inspect filter</td></tr>
<tr><td>&gt; 500 Pa</td><td style="color:#f87171">CRITICAL</td><td>Switch fan OFF manually and inspect filter</td></tr>
</table>
<div class="note">Single-sensor mode: &Delta;P = room baseline &minus; downstream pressure. Re-tare with the fan OFF whenever the baseline is old, because weather changes room pressure.</div>
<button onclick="doTare()">Re-tare (fan must be OFF)</button>
<span id="tareMsg" style="font-size:.8rem;color:#9ca3af;margin-left:10px"></span>
</div>
</main>
<script>
const canvas=document.getElementById('chart');
const ctx=canvas.getContext('2d');
const MAX_PTS=60;
const data=[];

function fmtAge(s){
  if(s<120)return s+' s';
  if(s<7200)return Math.floor(s/60)+' min';
  return (s/3600).toFixed(1)+' h';
}

function drawChart(){
  const W=canvas.width=canvas.offsetWidth;
  const H=canvas.height=200;
  ctx.clearRect(0,0,W,H);
  if(data.length<2)return;
  const maxVal=Math.max(600,...data);
  const pad={t:10,b:20,l:55,r:10};
  const gW=W-pad.l-pad.r, gH=H-pad.t-pad.b;
  [0,200,400,500,600].forEach(v=>{
    const y=pad.t+gH-(v/maxVal)*gH;
    ctx.strokeStyle=v===400?'#713f12':v===500?'#7f1d1d':'#1f2937';
    ctx.lineWidth=(v===400||v===500)?1.5:1;
    ctx.setLineDash(v===400||v===500?[4,3]:[]);
    ctx.beginPath();ctx.moveTo(pad.l,y);ctx.lineTo(W-pad.r,y);ctx.stroke();
    ctx.setLineDash([]);
    ctx.fillStyle='#6b7280';ctx.font='10px Arial';ctx.textAlign='right';
    ctx.fillText(v+' Pa',pad.l-4,y+4);
  });
  for(let i=1;i<data.length;i++){
    const x1=pad.l+((i-1)/(MAX_PTS-1))*gW;
    const y1=pad.t+gH-(Math.max(0,data[i-1])/maxVal)*gH;
    const x2=pad.l+(i/(MAX_PTS-1))*gW;
    const y2=pad.t+gH-(Math.max(0,data[i])/maxVal)*gH;
    ctx.strokeStyle=data[i]>=500?'#ef4444':data[i]>=400?'#f59e0b':'#3b82f6';
    ctx.lineWidth=2;
    ctx.beginPath();ctx.moveTo(x1,y1);ctx.lineTo(x2,y2);ctx.stroke();
  }
}

let demo=false,demoI=0,demoEma=null,demoLevel=0;
function demoStep(){
  const k=demoI++;
  const c=500+50*Math.sin(k/6);
  const r=14*Math.sin(k/2.3)+8*Math.sin(k/1.1)+(Math.random()-.5)*8;
  const v=Math.max(410,c+r);
  demoEma=demoEma===null?v:demoEma*0.6+v*0.4;
  if(demoLevel===0)demoLevel=demoEma>=500?2:1;
  else if(demoLevel===1&&demoEma>=500)demoLevel=2;
  else if(demoLevel===2&&demoEma<480)demoLevel=1;
  return demoEma;
}
function demoData(){
  const dp=demoStep();
  return {pressure:100812.4-dp,baseline:100812.4,delta_p:dp,status:demoLevel===2?'CRITICAL':'WARNING',
    sensor_ok:true,tared:true,baseline_age_s:7600,stale_after_s:21600,wifi_rssi:-58,
    uptime_s:Math.floor(performance.now()/1000)};
}
async function getData(){
  if(demo)return demoData();
  return await (await fetch('/api/data')).json();
}

async function fetchData(){
  try{
    const d=await getData();
    const warn=document.getElementById('warn');
    let msg='';
    if(!d.sensor_ok)msg='Sensor not found. Readings are not valid.';
    else if(!d.tared)msg='No baseline yet. Turn the fan OFF and press Re-tare.';
    else if(d.baseline_age_s>d.stale_after_s)msg='Baseline is old. Weather drift may affect readings. Re-tare with the fan OFF.';
    warn.style.display=msg?'block':'none';
    warn.textContent=msg;

    const ok=d.sensor_ok&&d.tared;
    document.getElementById('dp').textContent=ok?d.delta_p.toFixed(1):'--';
    document.getElementById('p').textContent=d.sensor_ok?d.pressure.toFixed(1):'--';
    document.getElementById('bl').textContent=d.tared?d.baseline.toFixed(1):'--';
    document.getElementById('age').textContent=d.tared?fmtAge(d.baseline_age_s):'--';
    document.getElementById('ts').textContent='Last update: '+new Date().toLocaleTimeString()+'  |  WiFi '+d.wifi_rssi+' dBm  |  Up '+fmtAge(d.uptime_s);
    const badge=document.getElementById('status');
    badge.textContent=d.status;
    badge.className='badge '+d.status;
    if(ok){
      data.push(d.delta_p);
      if(data.length>MAX_PTS)data.shift();
      drawChart();
    }
  }catch(e){
    document.getElementById('ts').textContent='Connection lost...';
  }
}

async function doTare(){
  if(!confirm('Is the fan OFF? Re-tare stores the current pressure as zero.'))return;
  try{
    await fetch('/api/tare',{method:'POST'});
    data.length=0;
    document.getElementById('tareMsg').textContent='Tare requested';
    setTimeout(()=>document.getElementById('tareMsg').textContent='',3000);
  }catch(e){document.getElementById('tareMsg').textContent='Failed';}
}

document.getElementById('demoToggle').addEventListener('change',function(){
  demo=this.checked;
  data.length=0;demoI=0;demoEma=null;demoLevel=0;
  document.getElementById('demoBanner').style.display=demo?'block':'none';
  if(demo){for(let k=0;k<MAX_PTS-1;k++)data.push(demoStep());}
  fetchData();
});

fetchData();
setInterval(fetchData,5000);
window.addEventListener('resize',drawChart);
</script>
</body>
</html>
)rawliteral";
  server.send_P(200, "text/html", PAGE);
}

// ---------------- Setup ----------------
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("======================================");
  Serial.println(" HVAC FILTER HEALTH MONITOR");
  Serial.println(" ESP32 + SINGLE BMP280");
  Serial.println("======================================");

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setTimeOut(100);
  Serial.printf("[I2C] SDA = GPIO%d, SCL = GPIO%d\n", SDA_PIN, SCL_PIN);

  sensorOk = beginSensor();
  Serial.println(sensorOk ? "[S] Found at 0x76" : "[S] NOT found at 0x76 (SDO to GND?)");

  if (sensorOk && TARE_AT_BOOT) tareSensor();

  Serial.print("[WiFi] Connecting to ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[WiFi] Connected!");
    Serial.print("[WiFi] Dashboard: http://");
    Serial.println(WiFi.localIP());
    server.on("/", handleRoot);
    server.on("/api/data", handleData);
    server.on("/api/tare", HTTP_POST, handleTare);
    server.begin();
    Serial.println("[Web] Server started.");
  } else {
    Serial.println("[WiFi] Failed. Check SSID/password. Serial readings continue.");
  }
  Serial.println("[SYSTEM] Fan control: MANUAL");
}

// ---------------- Loop ----------------
void loop() {
  if (WiFi.status() == WL_CONNECTED) server.handleClient();

  if (!sensorOk && millis() - lastRetry >= RETRY_MS) {
    lastRetry = millis();
    sensorOk = beginSensor();
    if (sensorOk && TARE_AT_BOOT && !tared) tareSensor();
  }

  if (tareRequested) {
    tareRequested = false;
    if (sensorOk) tareSensor();
  }

  if (millis() - lastSample >= SAMPLE_MS) {
    lastSample = millis();

    if (sensorOk) {
      pressure = bmp.readPressure();
      if (tared) {
        float dp = SENSOR_BELOW_AMBIENT ? (baseline - pressure) : (pressure - baseline);
        smoothedDP = movingAvg(dp);
        updateStatus();
      }
    }

    Serial.printf("S=%s | P=%.1f Pa | Base=%.1f Pa | DeltaP=%.1f Pa | %s\n",
                  sensorOk ? "OK" : "--", pressure, baseline, smoothedDP, getStatus().c_str());
  }
}
