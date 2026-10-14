// ════════════════════════════════════════════════════════════════════════════
// ApprenticeStand_v3.ino
// ESP32 + SparkFun HX711 + Phidgets 10kg load cell + mRo pitot (MS4525DO)
//
// What this does:
//  - Creates a WiFi hotspot called "ApprenticeStand"
//  - Hosts a live dashboard at 192.168.4.1
//  - One graph, two lines: sensor weight (lb, left axis) and airspeed (ft/s, right axis)
//  - Current + peak weight, current + peak airspeed
//  - Stopwatch auto-starts when weight exceeds 0.25 lb
//  - Logs time, weight, airspeed, downloadable as CSV or Excel (.xlsx)
//  - Prints a status line to Serial Monitor every second for debugging
//
// Hardware:
//  - ESP32 Dev Module (ESP-WROOM-32)
//  - SparkFun HX711 Load Cell Amplifier (SEN-13879)
//  - Phidgets S-Type Load Cell 10kg (1305)
//  - mRo PX4 Airspeed v1.8 (MS4525DO, 5V version, 1 psi, I2C 0x28)
//
// Wiring:
//   Load cell RED   → HX711 RED (E+)
//   Load cell BLACK → HX711 BLK (E-)
//   Load cell GREEN → HX711 GRN (A+)
//   Load cell WHITE → HX711 WHT (A-)
//   HX711 VCC       → ESP32 VIN (5V)
//   HX711 VDD       → ESP32 3V3
//   HX711 GND       → ESP32 GND
//   HX711 DAT       → ESP32 GPIO 4
//   HX711 CLK       → ESP32 GPIO 5
//   Pitot pin 1 5V  → ESP32 VIN (5V)
//   Pitot pin 4 GND → ESP32 GND
//   Pitot pin 3 SDA → ESP32 GPIO 21   (see guide: direct only if SDA reads 0V or 3.3V)
//   Pitot pin 2 SCL → ESP32 GPIO 22   (otherwise through a level shifter)
//
// Libraries (Arduino Library Manager):
//   - HX711 (Rob Tillaart's or Bogdan Necula's, both work with this code)
//   - WiFi, WebServer, Wire are built into the ESP32 core
// ════════════════════════════════════════════════════════════════════════════

#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include "HX711.h"

// ── WiFi credentials ─────────────────────────────────────────────────────────
const char* AP_SSID = "ApprenticeStand";   // WiFi name
const char* AP_PASS = "apprentice26";      // WiFi password, 8+ characters

// ── Pins ─────────────────────────────────────────────────────────────────────
const int HX_DT  = 4;    // HX711 DAT → GPIO 4
const int HX_SCK = 5;    // HX711 CLK → GPIO 5
const int I2C_SDA = 21;  // pitot SDA
const int I2C_SCL = 22;  // pitot SCL

// ── Calibration constant ─────────────────────────────────────────────────────
// YOU MUST CHANGE THIS to your own value from Calibration.ino.
// counts_per_lb = (loaded_counts - zero_counts) / known_weight_lb
float counts_per_lb = 200955.5;   // calibrated 10.08 lb, Oct 2026

// ── Air density for airspeed ─────────────────────────────────────────────────
// 1.225  = sea level standard (gives indicated airspeed)
// 1.056  = ~5,000 ft standard day (Prescott-ish, gives ~true airspeed)
// For best accuracy: rho = station_pressure_Pa / (287.05 * temp_K)
const float RHO = 1.056;     // kg/m^3

// ── Pitot settings ───────────────────────────────────────────────────────────
const uint8_t PITOT_ADDR = 0x28;
const float DP_DEADBAND_PA = 3.0;   // dp below this reads 0 airspeed (kills noise at idle)
const float DP_SMOOTH = 0.2;        // 0..1, lower = smoother airspeed
const float AS_ON_FPS = 5.0;        // airspeed must pass this (ft/s)...
const uint32_t AS_HOLD_MS = 300;    // ...for this long before it activates (and below it this long to shut off)

// ── Logging ──────────────────────────────────────────────────────────────────
const uint32_t LOG_INTERVAL_MS = 100;   // 10 rows per second in the CSV
const int LOG_MAX = 6000;               // 6000 rows = 10 min at 10 Hz, oldest dropped after

// ── Library objects ──────────────────────────────────────────────────────────
HX711 scale;
WebServer server(80);

// ── State ────────────────────────────────────────────────────────────────────
unsigned long t0_ms = 0;
bool loggingEnabled = true;

float hxBuf[3];                // 3-sample moving average, same smoothing as the old read_average(3)
int hxCount = 0, hxIdx = 0;
float lastCounts = 0, lastThrust = 0, lastThrustRaw = 0;

bool pitotOk = false;
float pitotZeroPa = 0, dpFilt = 0, lastFps = 0;
bool asActive = false;                    // airspeed gate: reads 0 until real airflow is held
unsigned long aboveSince = 0, belowSince = 0;
unsigned long lastPitotMs = 0;

struct Rec { uint32_t t_ms; float thrust; float fps; };
Rec logBuf[LOG_MAX];
int logHead = 0, logCount = 0;
unsigned long lastLogMs = 0, lastSerialMs = 0;

// ── Pitot: raw read of the MS4525DO (type A, -1..+1 psi) ────────────────────
bool readPitotRaw(float &dPa) {
  if (Wire.requestFrom(PITOT_ADDR, (uint8_t)4) != 4) return false;
  uint8_t b0 = Wire.read(), b1 = Wire.read(); Wire.read(); Wire.read();
  if ((b0 >> 6) != 0) return false;                 // 0 = fresh, 2 = stale, 3 = fault
  uint16_t p = ((uint16_t)(b0 & 0x3F) << 8) | b1;
  float psi = ((float)p - 0.1f * 16383.0f) * 2.0f / (0.8f * 16383.0f) - 1.0f;
  dPa = psi * 6894.757f;
  return true;
}

// ── Pitot: detect + zero. Cover the pitot tip / no wind while this runs. ────
void zeroPitot() {
  Wire.beginTransmission(PITOT_ADDR);
  pitotOk = (Wire.endTransmission() == 0);
  if (!pitotOk) { Serial.println("[PITOT] not found at 0x28"); return; }
  float sum = 0; int n = 0;
  unsigned long ts = millis();
  while (n < 100 && millis() - ts < 2000) {
    float d;
    if (readPitotRaw(d)) { sum += d; n++; }
    delay(5);
  }
  if (n == 0) { pitotOk = false; Serial.println("[PITOT] no valid readings"); return; }
  pitotZeroPa = sum / n;
  dpFilt = 0; lastFps = 0;
  asActive = false; aboveSince = 0; belowSince = 0;
  Serial.printf("[PITOT] zeroed at %.1f Pa (%d samples)\n", pitotZeroPa, n);
}

// ── Pitot: update airspeed (called ~50 Hz from loop) ─────────────────────────
void updatePitot() {
  if (!pitotOk) return;
  float d;
  if (!readPitotRaw(d)) return;
  float dp = d - pitotZeroPa;
  dpFilt += DP_SMOOTH * (dp - dpFilt);
  float use = (dpFilt > DP_DEADBAND_PA) ? dpFilt : 0.0f;
  float rawFps = sqrtf(2.0f * use / RHO) * 3.28084f;   // m/s → ft/s

  // gate: only activate after airspeed stays above AS_ON_FPS for AS_HOLD_MS,
  // and only shut off after it stays below for AS_HOLD_MS. Kills idle noise and gusts.
  unsigned long now = millis();
  if (rawFps >= AS_ON_FPS) {
    belowSince = 0;
    if (aboveSince == 0) aboveSince = now;
    if (!asActive && now - aboveSince >= AS_HOLD_MS) asActive = true;
  } else {
    aboveSince = 0;
    if (belowSince == 0) belowSince = now;
    if (asActive && now - belowSince >= AS_HOLD_MS) asActive = false;
  }
  lastFps = asActive ? rawFps : 0.0f;
}

// ── Load cell: non-blocking, only reads when the HX711 has new data ─────────
void updateLoadCell() {
  if (!scale.is_ready()) return;
  float c = (float)scale.read();
  hxBuf[hxIdx] = c;
  hxIdx = (hxIdx + 1) % 3;
  if (hxCount < 3) hxCount++;
  float sum = 0;
  for (int i = 0; i < hxCount; i++) sum += hxBuf[i];
  lastCounts = sum / hxCount;
  lastThrustRaw = (lastCounts - (float)scale.get_offset()) / counts_per_lb;
  // clamp tiny negatives from noise to zero, keep real negatives visible
  lastThrust = (lastThrustRaw < 0.0f && lastThrustRaw > -0.20f) ? 0.0f : lastThrustRaw;
}

// ── Log buffer ───────────────────────────────────────────────────────────────
void logRecord() {
  logBuf[logHead] = { (uint32_t)(millis() - t0_ms), lastThrust, lastFps };
  logHead = (logHead + 1) % LOG_MAX;
  if (logCount < LOG_MAX) logCount++;
}

// ── HTML dashboard ───────────────────────────────────────────────────────────
const char INDEX_HTML[] PROGMEM = R"HTMLEND(<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Apprentice Stand v3</title>
<style>
:root{--bg:#0b1633;--panel:#12224a;--border:#233a72;--accent:#ff3b30;--accent2:#ff7a1a;--spd:#ffb000;--text:#eef2ff;--muted:#7f8db8;--green:#ff7a1a;}
*{box-sizing:border-box;margin:0;padding:0;}
body{background:var(--bg);color:var(--text);font-family:sans-serif;padding:20px;padding-bottom:80px;}
header{display:flex;align-items:center;gap:14px;margin-bottom:20px;border-bottom:1px solid var(--border);padding-bottom:16px;}
.dot{width:12px;height:12px;border-radius:50%;background:var(--accent);box-shadow:0 0 12px var(--accent);animation:pulse 2s infinite;}
@keyframes pulse{0%,100%{opacity:1;}50%{opacity:0.3;}}
h1{font-size:22px;font-weight:700;letter-spacing:.08em;text-transform:uppercase;}
h1 span{color:var(--accent);}
h1 small{font-size:12px;color:var(--muted);margin-left:8px;letter-spacing:.05em;}
.controls{display:flex;gap:10px;flex-wrap:wrap;margin-bottom:20px;}
button{padding:9px 18px;font-size:13px;font-weight:600;text-transform:uppercase;border:1px solid var(--border);border-radius:4px;background:var(--panel);color:var(--text);cursor:pointer;}
button:hover{border-color:var(--accent);color:var(--accent);}
.tare{border-color:var(--green);color:var(--green);}
.zero{border-color:var(--spd);color:var(--spd);}
.stats{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:12px;margin-bottom:20px;}
.card{background:var(--panel);border:1px solid var(--border);border-radius:6px;padding:14px 16px;}
.lbl{font-size:11px;letter-spacing:.1em;text-transform:uppercase;color:var(--muted);margin-bottom:6px;}
.val{font-family:monospace;font-size:28px;color:var(--accent);line-height:1;}
.val.pk{color:var(--accent2);}
.val.sp{color:var(--spd);}
.val.sm{font-size:20px;color:var(--muted);}
.unit{font-size:12px;color:var(--muted);margin-top:4px;}
.wrap{background:var(--panel);border:1px solid var(--border);border-radius:6px;padding:16px;}
.legend{display:flex;gap:18px;font-size:12px;color:var(--muted);margin-bottom:8px;}
.legend i{display:inline-block;width:14px;height:3px;margin-right:6px;vertical-align:middle;}
canvas{display:block;width:100%;height:360px;}
#sb{font-size:12px;color:var(--muted);font-family:monospace;margin-top:10px;}
#sb span{color:var(--accent);}
#swbar{position:fixed;bottom:0;left:0;right:0;background:#0b1633;border-top:1px solid #233a72;display:flex;align-items:center;justify-content:center;gap:20px;padding:12px 20px;z-index:100;}
#swtime{font-family:monospace;font-size:32px;color:var(--accent);letter-spacing:0.05em;min-width:120px;text-align:center;}
#swtime.paused{color:var(--muted);}
.sw-btn{padding:7px 16px;font-size:12px;font-weight:600;text-transform:uppercase;border:1px solid var(--border);border-radius:4px;background:var(--panel);color:var(--text);cursor:pointer;}
.sw-btn:hover{border-color:var(--accent);color:var(--accent);}
.sw-btn.danger:hover{border-color:var(--accent2);color:var(--accent2);}
</style>
</head>
<body>
<header><div class="dot"></div><h1>Apprentice <span>Stand</span><small>v3</small></h1></header>

<div class="controls">
<button onclick="toggleLog()" id="lb">Pause</button>
<button onclick="resetPeak()">Reset Peaks</button>
<button class="tare" onclick="doTare()">Tare Weight</button>
<button class="zero" onclick="doZero()">Zero Airspeed</button>
<button onclick="dlCSV()">Download CSV</button>
<button onclick="dlXLSX()">Download Excel</button>
<button onclick="clrLog()">Clear</button>
</div>

<div class="stats">
<div class="card"><div class="lbl">Weight</div><div class="val" id="yc">--</div><div class="unit">lb</div></div>
<div class="card"><div class="lbl">Peak weight</div><div class="val pk" id="pc">--</div><div class="unit">lb</div></div>
<div class="card"><div class="lbl">Airspeed</div><div class="val sp" id="ac">--</div><div class="unit">ft/s</div></div>
<div class="card"><div class="lbl">Peak airspeed</div><div class="val pk" id="pa">--</div><div class="unit">ft/s</div></div>
<div class="card"><div class="lbl">Time</div><div class="val sm" id="tc">--</div><div class="unit">s</div></div>
<div class="card"><div class="lbl">Counts</div><div class="val sm" id="cc">--</div><div class="unit">raw</div></div>
</div>

<div class="wrap">
<div class="legend"><span><i style="background:#ff3b30"></i>Weight (lb, left)</span><span><i style="background:#ffb000"></i>Airspeed (ft/s, right)</span></div>
<canvas id="cv"></canvas><div id="sb">Connecting...</div>
</div>

<div id="swbar">
<button class="sw-btn" onclick="swPause()" id="swPauseBtn">Pause</button>
<div id="swtime">00:00.0</div>
<button class="sw-btn" onclick="swReset()">Reset</button>
<button class="sw-btn danger" onclick="clrLog()">Clear</button>
</div>

<script>
// graph data: t = time (s), y = weight (lb), s = airspeed (ft/s)
var t=[],y=[],s=[],logging=true,pk=0,pks=0,pitotOk=false;
var cv=document.getElementById("cv"),cx=cv.getContext("2d");
var PL=52,PR=60,PT=16,PB=36;
var YMX=16;    // weight axis max (lb), change if you exceed it
var SMX=160;   // airspeed axis max (ft/s), change if you go faster
var MAXPTS=600;

// ── stopwatch (unchanged from your original) ──
var swStarted=false,swRunning=false,swElapsed=0,swSegStart=0;
function swStart(){if(!swStarted){swStarted=true;swRunning=true;swSegStart=Date.now();}}
function swPause(){
  if(!swStarted)return;
  if(swRunning){swElapsed+=Date.now()-swSegStart;swRunning=false;
    document.getElementById("swPauseBtn").textContent="Resume";document.getElementById("swtime").classList.add("paused");}
  else{swSegStart=Date.now();swRunning=true;
    document.getElementById("swPauseBtn").textContent="Pause";document.getElementById("swtime").classList.remove("paused");}
}
function swReset(){swStarted=false;swRunning=false;swElapsed=0;swSegStart=0;
  document.getElementById("swtime").textContent="00:00.0";document.getElementById("swtime").classList.remove("paused");
  document.getElementById("swPauseBtn").textContent="Pause";}
function fmtTime(ms){var tt=Math.floor(ms/100),te=tt%10,se=Math.floor(tt/10)%60,mi=Math.floor(tt/600);
  return (mi<10?"0":"")+mi+":"+(se<10?"0":"")+se+"."+te;}
setInterval(function(){if(!swStarted||!swRunning)return;
  document.getElementById("swtime").textContent=fmtTime(swElapsed+(Date.now()-swSegStart));},100);

// ── graph ──
function resize(){var r=cv.parentElement.getBoundingClientRect();cv.width=r.width-32;cv.height=360;}
resize();window.addEventListener("resize",function(){resize();draw();});
function xm(v,mn,mx){var w=cv.width-PL-PR;return mx===mn?PL:PL+(v-mn)*w/(mx-mn);}
function ymT(v){var h=cv.height-PT-PB;return PT+(YMX-v)*h/YMX;}
function ymS(v){var h=cv.height-PT-PB;return PT+(SMX-v)*h/SMX;}

function line(arr,ymap,col,fill){
  var mn=t[0],mx=t[t.length-1];
  if(fill){
    cx.beginPath();cx.moveTo(xm(t[0],mn,mx),ymap(0));
    for(var i=0;i<t.length;i++)cx.lineTo(xm(t[i],mn,mx),ymap(arr[i]));
    cx.lineTo(xm(t[t.length-1],mn,mx),ymap(0));cx.closePath();
    var g=cx.createLinearGradient(0,PT,0,cv.height-PB);
    g.addColorStop(0,"rgba(255,59,48,0.18)");g.addColorStop(1,"rgba(255,59,48,0.01)");
    cx.fillStyle=g;cx.fill();
  }
  cx.strokeStyle=col;cx.lineWidth=2;cx.lineJoin="round";cx.shadowColor=col;cx.shadowBlur=6;
  cx.beginPath();cx.moveTo(xm(t[0],mn,mx),ymap(arr[0]));
  for(var i=1;i<t.length;i++)cx.lineTo(xm(t[i],mn,mx),ymap(arr[i]));
  cx.stroke();cx.shadowBlur=0;
  var lx=xm(t[t.length-1],mn,mx),ly=ymap(arr[arr.length-1]);
  cx.fillStyle=col;cx.beginPath();cx.arc(lx,ly,4,0,Math.PI*2);cx.fill();
}

function draw(){
  var W=cv.width,H=cv.height,pw=W-PL-PR,ph=H-PT-PB;
  cx.fillStyle="#0b1633";cx.fillRect(0,0,W,H);
  cx.font="11px monospace";
  for(var i=0;i<=8;i++){
    var vT=YMX*i/8,vS=SMX*i/8,p=ymT(vT);
    cx.strokeStyle=i===0?"#2b4280":"#1c2d5c";cx.lineWidth=1;cx.setLineDash([4,4]);
    cx.beginPath();cx.moveTo(PL,p);cx.lineTo(W-PR,p);cx.stroke();cx.setLineDash([]);
    cx.textAlign="right";cx.fillStyle="#c46a6a";cx.fillText(vT+" lb",PL-6,p+4);
    cx.textAlign="left";cx.fillStyle="#c9962e";cx.fillText(vS+"",W-PR+6,p+4);
  }
  if(t.length>=2){
    var mn=t[0],mx=t[t.length-1];cx.textAlign="center";
    for(var i=0;i<=6;i++){
      var tv=mn+(mx-mn)*i/6,px=xm(tv,mn,mx);
      cx.fillStyle="#6a7aa8";cx.fillText(tv.toFixed(1)+"s",px,H-PB+18);
      cx.strokeStyle="#1c2d5c";cx.lineWidth=1;cx.setLineDash([2,4]);
      cx.beginPath();cx.moveTo(px,PT);cx.lineTo(px,H-PB);cx.stroke();cx.setLineDash([]);
    }
  }
  cx.strokeStyle="#233a72";cx.lineWidth=1;cx.strokeRect(PL,PT,pw,ph);
  cx.save();cx.translate(14,PT+ph/2);cx.rotate(-Math.PI/2);
  cx.textAlign="center";cx.fillStyle="#7f8db8";cx.font="11px sans-serif";cx.fillText("Weight (lb)",0,0);cx.restore();
  cx.save();cx.translate(W-10,PT+ph/2);cx.rotate(Math.PI/2);
  cx.textAlign="center";cx.fillStyle="#7f8db8";cx.font="11px sans-serif";cx.fillText("Airspeed (ft/s)",0,0);cx.restore();
  cx.textAlign="center";cx.fillStyle="#7f8db8";cx.font="11px sans-serif";cx.fillText("Time (s)",PL+pw/2,H-4);
  if(t.length<2){cx.fillStyle="#2b4280";cx.font="14px sans-serif";cx.fillText("Waiting...",PL+pw/2,PT+ph/2);return;}
  line(y,ymT,"#ff3b30",true);
  if(pitotOk)line(s,ymS,"#ffb000",false);
}

// ── poll the ESP32 4x per second ──
function poll(){
  fetch("/sample").then(function(r){return r.json();}).then(function(j){
    pitotOk=j.pitot_ok;
    document.getElementById("tc").textContent=j.time_s.toFixed(2);
    document.getElementById("yc").textContent=j.thrust_lb.toFixed(3);
    document.getElementById("cc").textContent=j.counts;
    document.getElementById("ac").textContent=pitotOk?j.airspeed_fps.toFixed(1):"N/A";
    if(j.thrust_lb>pk){pk=j.thrust_lb;document.getElementById("pc").textContent=pk.toFixed(3);}
    if(pitotOk&&j.airspeed_fps>pks){pks=j.airspeed_fps;document.getElementById("pa").textContent=pks.toFixed(1);}
    if(j.thrust_lb>0.25&&!swStarted)swStart();
    if(logging){
      t.push(j.time_s);y.push(j.thrust_lb);s.push(j.airspeed_fps);
      if(t.length>MAXPTS){t.shift();y.shift();s.shift();}
    }
    document.getElementById("sb").innerHTML=
      "Logging: <span>"+(logging?"ON":"OFF")+"</span> | Points: <span>"+t.length+"</span> | "+
      "Pitot: <span style='color:"+(pitotOk?"#ffb000":"#ff7a1a")+"'>"+(pitotOk?"OK":"NOT FOUND")+"</span> | "+
      "CSV rows: <span>"+j.log_rows+"</span>";
    draw();
  }).catch(function(){document.getElementById("sb").innerHTML="<span style='color:#ff7a1a'>Disconnected</span>";});
}

function toggleLog(){logging=!logging;document.getElementById("lb").textContent=logging?"Pause":"Resume";
  fetch("/log?enabled="+(logging?"1":"0"));}
function resetPeak(){pk=0;pks=0;document.getElementById("pc").textContent="0.000";document.getElementById("pa").textContent="0.0";}
function doTare(){fetch("/tare");resetPeak();t=[];y=[];s=[];swReset();draw();}
function doZero(){fetch("/zero");pks=0;document.getElementById("pa").textContent="0.0";}
function clrLog(){fetch("/clear");t=[];y=[];s=[];resetPeak();swReset();draw();}
function dlCSV(){window.location="/csv";}
// ── Excel export: builds a real .xlsx in the browser (no internet needed) ──
var CRCT=(function(){var t=new Uint32Array(256);for(var n=0;n<256;n++){var c=n;for(var k=0;k<8;k++)c=(c&1)?(0xEDB88320^(c>>>1)):(c>>>1);t[n]=c>>>0;}return t;})();
function crc32(b){var c=0xFFFFFFFF;for(var i=0;i<b.length;i++)c=CRCT[(c^b[i])&0xFF]^(c>>>8);return (c^0xFFFFFFFF)>>>0;}
function makeZip(files){
  var enc=new TextEncoder(),parts=[],cd=[],off=0;
  files.forEach(function(f){
    var nm=enc.encode(f.name),d=enc.encode(f.data),crc=crc32(d);
    var h=new DataView(new ArrayBuffer(30));
    h.setUint32(0,0x04034b50,true);h.setUint16(4,20,true);h.setUint16(6,0,true);h.setUint16(8,0,true);
    h.setUint16(10,0,true);h.setUint16(12,33,true);h.setUint32(14,crc,true);h.setUint32(18,d.length,true);
    h.setUint32(22,d.length,true);h.setUint16(26,nm.length,true);h.setUint16(28,0,true);
    parts.push(new Uint8Array(h.buffer),nm,d);
    var c=new DataView(new ArrayBuffer(46));
    c.setUint32(0,0x02014b50,true);c.setUint16(4,20,true);c.setUint16(6,20,true);c.setUint16(8,0,true);
    c.setUint16(10,0,true);c.setUint16(12,0,true);c.setUint16(14,33,true);c.setUint32(16,crc,true);
    c.setUint32(20,d.length,true);c.setUint32(24,d.length,true);c.setUint16(28,nm.length,true);
    c.setUint16(30,0,true);c.setUint16(32,0,true);c.setUint16(34,0,true);c.setUint16(36,0,true);
    c.setUint32(38,0,true);c.setUint32(42,off,true);
    cd.push(new Uint8Array(c.buffer),nm);
    off+=30+nm.length+d.length;
  });
  var cdSize=0;cd.forEach(function(a){cdSize+=a.length;});
  var e=new DataView(new ArrayBuffer(22));
  e.setUint32(0,0x06054b50,true);e.setUint16(4,0,true);e.setUint16(6,0,true);
  e.setUint16(8,files.length,true);e.setUint16(10,files.length,true);
  e.setUint32(12,cdSize,true);e.setUint32(16,off,true);e.setUint16(20,0,true);
  return new Blob(parts.concat(cd,[new Uint8Array(e.buffer)]),{type:"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"});
}
function buildXlsx(csvText){
  var X='<?xml version="1.0" encoding="UTF-8" standalone="yes"?>';
  var NS='http://schemas.openxmlformats.org/';
  var lines=csvText.trim().split("\n"),L="ABC";
  var hdr=["Time (s)","Weight (lb)","Airspeed (ft/s)"];
  var rows='<row r="1">';
  for(var k=0;k<3;k++)rows+='<c r="'+L[k]+'1" t="inlineStr"><is><t>'+hdr[k]+'</t></is></c>';
  rows+='</row>';
  var rn=1;
  for(var i=1;i<lines.length;i++){
    var p=lines[i].split(",");if(p.length<3)continue;
    rn++;rows+='<row r="'+rn+'">';
    for(var k=0;k<3;k++){var v=parseFloat(p[k]);if(isFinite(v))rows+='<c r="'+L[k]+rn+'"><v>'+v+'</v></c>';}
    rows+='</row>';
  }
  var sheet=X+'<worksheet xmlns="'+NS+'spreadsheetml/2006/main"><sheetViews><sheetView workbookViewId="0">'+
    '<pane ySplit="1" topLeftCell="A2" activePane="bottomLeft" state="frozen"/></sheetView></sheetViews>'+
    '<cols><col min="1" max="3" width="16" customWidth="1"/></cols><sheetData>'+rows+'</sheetData></worksheet>';
  return makeZip([
    {name:"[Content_Types].xml",data:X+'<Types xmlns="'+NS+'package/2006/content-types">'+
      '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'+
      '<Default Extension="xml" ContentType="application/xml"/>'+
      '<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>'+
      '<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/></Types>'},
    {name:"_rels/.rels",data:X+'<Relationships xmlns="'+NS+'package/2006/relationships">'+
      '<Relationship Id="rId1" Type="'+NS+'officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/></Relationships>'},
    {name:"xl/workbook.xml",data:X+'<workbook xmlns="'+NS+'spreadsheetml/2006/main" xmlns:r="'+NS+'officeDocument/2006/relationships">'+
      '<sheets><sheet name="Apprentice Stand" sheetId="1" r:id="rId1"/></sheets></workbook>'},
    {name:"xl/_rels/workbook.xml.rels",data:X+'<Relationships xmlns="'+NS+'package/2006/relationships">'+
      '<Relationship Id="rId1" Type="'+NS+'officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/></Relationships>'},
    {name:"xl/worksheets/sheet1.xml",data:sheet}
  ]);
}
function pad2(n){return (n<10?"0":"")+n;}
function dlXLSX(){
  fetch("/csv").then(function(r){return r.text();}).then(function(txt){
    var blob=buildXlsx(txt),d=new Date();
    var name="apprentice_stand_v3_"+d.getFullYear()+pad2(d.getMonth()+1)+pad2(d.getDate())+"_"+pad2(d.getHours())+pad2(d.getMinutes())+".xlsx";
    var a=document.createElement("a");a.href=URL.createObjectURL(blob);a.download=name;
    document.body.appendChild(a);a.click();
    setTimeout(function(){URL.revokeObjectURL(a.href);a.remove();},2000);
  }).catch(function(){alert("Excel export failed, try Download CSV");});
}

setInterval(poll,250);
</script>
</body>
</html>)HTMLEND";

// ── Web handlers ─────────────────────────────────────────────────────────────
void handleRoot() { server.send(200, "text/html", INDEX_HTML); }

void handleSample() {
  float time_s = (millis() - t0_ms) / 1000.0f;
  char json[220];
  snprintf(json, sizeof(json),
    "{\"time_s\":%.2f,\"thrust_lb\":%.3f,\"thrust_lb_raw\":%.3f,\"counts\":%ld,"
    "\"airspeed_fps\":%.2f,\"pitot_ok\":%s,\"log_rows\":%d}",
    time_s, lastThrust, lastThrustRaw, (long)lastCounts,
    lastFps, pitotOk ? "true" : "false", logCount);
  server.send(200, "application/json", json);
}

// streams the CSV in chunks so it never builds one giant string in RAM
void handleCSV() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Content-Disposition", "attachment; filename=apprentice_stand_log.csv");
  server.send(200, "text/csv", "");
  server.sendContent("time_s,weight_lb,airspeed_fps\n");
  String chunk; chunk.reserve(1200);
  int start = (logHead - logCount + LOG_MAX) % LOG_MAX;
  char ln[48];
  for (int i = 0; i < logCount; i++) {
    const Rec &r = logBuf[(start + i) % LOG_MAX];
    snprintf(ln, sizeof(ln), "%.2f,%.3f,%.2f\n", r.t_ms / 1000.0f, r.thrust, r.fps);
    chunk += ln;
    if (chunk.length() > 1000) { server.sendContent(chunk); chunk = ""; }
  }
  if (chunk.length()) server.sendContent(chunk);
  server.sendContent("");
}

void handleClear() { logHead = 0; logCount = 0; server.send(200, "text/plain", "cleared"); }

void handleLogToggle() {
  if (server.hasArg("enabled")) loggingEnabled = (server.arg("enabled") == "1");
  server.send(200, "text/plain", loggingEnabled ? "on" : "off");
}

void handleTare() {
  scale.tare(20);
  hxCount = 0; hxIdx = 0;
  t0_ms = millis();
  server.send(200, "text/plain", "tared");
}

void handleZero() {
  zeroPitot();
  server.send(200, "text/plain", pitotOk ? "zeroed" : "pitot not found");
}

// ── setup ────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== Apprentice Stand v3 ===");

  // load cell
  scale.begin(HX_DT, HX_SCK);
  scale.set_gain(128);
  delay(1000);
  Serial.println("[HX711] taring, keep the motor off...");
  scale.tare(30);
  Serial.println("[HX711] done");

  // pitot (cover the tip / no airflow during boot)
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);
  zeroPitot();

  t0_ms = millis();

  // WiFi + web server
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  delay(200);
  server.on("/", handleRoot);
  server.on("/sample", handleSample);
  server.on("/csv", handleCSV);
  server.on("/clear", handleClear);
  server.on("/log", handleLogToggle);
  server.on("/tare", handleTare);
  server.on("/zero", handleZero);
  server.begin();

  Serial.print("[WIFI] connect to \""); Serial.print(AP_SSID);
  Serial.print("\", then open http://"); Serial.println(WiFi.softAPIP());
}

// ── loop ─────────────────────────────────────────────────────────────────────
void loop() {
  server.handleClient();
  updateLoadCell();

  unsigned long now = millis();
  if (now - lastPitotMs >= 20) { lastPitotMs = now; updatePitot(); }   // ~50 Hz

  if (loggingEnabled && now - lastLogMs >= LOG_INTERVAL_MS) { lastLogMs = now; logRecord(); }

  if (now - lastSerialMs >= 1000) {
    lastSerialMs = now;
    char spd[32];
    if (pitotOk) snprintf(spd, sizeof(spd), "%.1f ft/s", lastFps);
    else snprintf(spd, sizeof(spd), "N/A (pitot not found)");
    Serial.printf("weight %.3f lb | counts %.0f | airspeed %s | CSV rows %d\n",
                  lastThrust, lastCounts, spd, logCount);
  }
}
