/*
 * ESP32-C6 "MEDIA STREAMER CLOUD" (UPDATED)
 * Features: Video/Image Preview + Cloud UI + TTP223 + DHT11 + Admin + OLED
 */

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <WiFi.h> 
#include <WebServer.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include "esp32-hal-rgb-led.h"
#include <DHT.h>
#include <time.h>
#include <sys/time.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ================= HARDWARE CONFIG =================
#define SD_CS_PIN 4
#define RGB_PIN 8 
#define EXT_RGB_PIN 12
#define TOUCH_PIN 2
#define DHTPIN 3
#define DHTTYPE DHT11
#define BUTTON_PIN 9

#define OLED_SDA 0
#define OLED_SCL 1
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// ================= GLOBALS =================
WebServer server(80);
File uploadFile;
bool isCloudMode = false;
bool isUploading = false;
bool credsFound = false;

DHT dht(DHTPIN, DHTTYPE);
float currentTemp = 0.0;
float currentHum = 0.0;
unsigned long lastDhtRead = 0;

int uiState = 0; // 0: DHT, 1: Time/Date, 2: IP, 3: Users, 4: Storage, 5: Setup Mode
unsigned long lastDisplayUpdate = 0;
long userGmtOffset = 19800; // Default IST +5:30 in seconds

Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// LED Variables
unsigned long lastLedUpdate = 0;
int hue = 0;
int ledMode = 2; // 0: Constant, 1: Rainbow, 2: Fire
bool ledPower = true;
uint8_t customR = 255, customG = 0, customB = 0;
int colorIndex = 0;

// Inputs
bool lastBtnState = false;
unsigned long btnPressTime = 0;
bool btnLongPressed = false;
bool BUTTON_PRESSED_STATE = LOW; // Set to HIGH if your button is wired to 3.3V

bool lastTouchState = LOW;
unsigned long touchPressTime = 0;
unsigned long lastTapTime = 0;
int tapCount = 0;
bool touchLongPressed = false;

// Time
const char* ntpServer = "pool.ntp.org";

// ================= DISPLAY DRIVER =================
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789      _panel_instance;
  lgfx::Bus_SPI           _bus_instance;
  lgfx::Light_PWM         _light_instance;
public:
  LGFX(void) {
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host = SPI2_HOST;
      cfg.freq_write = 80000000;
      cfg.freq_read  = 16000000;
      cfg.pin_sclk = 7;
      cfg.pin_mosi = 6;
      cfg.pin_miso = 5; 
      cfg.pin_dc   = 15;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }
    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs           = 14;
      cfg.pin_rst          = 21;
      cfg.panel_width      = 172;
      cfg.panel_height     = 320;
      cfg.offset_x         = 34;
      cfg.invert           = true;
      cfg.bus_shared       = true; 
      _panel_instance.config(cfg);
    }
    {
      auto cfg = _light_instance.config();
      cfg.pin_bl = 22;
      cfg.pwm_channel = 7;
      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);
    }
    setPanel(&_panel_instance);
  }
};
LGFX tft;
LGFX_Sprite spr(&tft);

// ================= LED ENGINE =================
void writeToPixels(uint8_t r, uint8_t g, uint8_t b) {
  if(!ledPower) { r=0; g=0; b=0; }
  neopixelWrite(RGB_PIN, r, g, b);
  neopixelWrite(EXT_RGB_PIN, r, g, b);
}

void updateNeoPixels() {
  if(!ledPower) { writeToPixels(0,0,0); return; }
  if (ledMode == 0) {
    writeToPixels(customR, customG, customB);
  } else if (ledMode == 1) {
    if (millis() - lastLedUpdate < 20) return;
    lastLedUpdate = millis();
    hue = (hue + 2) % 360;
    float c = 1.0;
    float x = c * (1 - abs(fmod(((float)hue / 60.0), 2) - 1));
    float r=0, g=0, b=0;
    if(hue < 60) { r=c; g=x; b=0; }
    else if(hue < 120) { r=x; g=c; b=0; }
    else if(hue < 180) { r=0; g=c; b=x; }
    else if(hue < 240) { r=0; g=x; b=c; }
    else if(hue < 300) { r=x; g=0; b=c; }
    else { r=c; g=0; b=x; }
    writeToPixels((int)(r*255), (int)(g*255), (int)(b*255));
  } else if (ledMode == 2) {
    if (millis() - lastLedUpdate < 40) return;
    lastLedUpdate = millis();
    hue = (hue + 1) % 360;
    float brightness = random(20, 150) / 255.0; 
    float c = 1.0 * brightness;
    float x = c * (1 - abs(fmod(((float)hue / 60.0), 2) - 1));
    float r=0, g=0, b=0;
    if(hue < 60) { r=c; g=x; b=0; }
    else if(hue < 120) { r=x; g=c; b=0; }
    else if(hue < 180) { r=0; g=c; b=x; }
    else if(hue < 240) { r=0; g=x; b=c; }
    else if(hue < 300) { r=x; g=0; b=c; }
    else { r=c; g=0; b=x; }
    writeToPixels((int)(r*255), (int)(g*255), (int)(b*255));
  }
}

// ================= HELPERS =================
String getContentType(String filename) {
  if (server.hasArg("download")) return "application/octet-stream";
  else if (filename.endsWith(".htm") || filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".png")) return "image/png";
  else if (filename.endsWith(".gif")) return "image/gif";
  else if (filename.endsWith(".jpg")) return "image/jpeg";
  else if (filename.endsWith(".mp4")) return "video/mp4";
  return "text/plain";
}

void saveWiFiCreds(String ssid, String pass) {
  if(SD.exists("/wifi.txt")) SD.remove("/wifi.txt");
  File f = SD.open("/wifi.txt", FILE_WRITE);
  if(f) { f.println(ssid); f.println(pass); f.close(); }
}

bool loadWiFiCreds(String &ssid, String &pass) {
  if(!SD.exists("/wifi.txt")) return false;
  File f = SD.open("/wifi.txt", "r");
  if(f) { ssid = f.readStringUntil('\n'); ssid.trim(); pass = f.readStringUntil('\n'); pass.trim(); f.close(); return true; }
  return false;
}

void deleteWiFiCreds() { if(SD.exists("/wifi.txt")) SD.remove("/wifi.txt"); }

bool checkLogin(String u, String p) {
  if(u == "admin" && p == "admin") return true; 
  if (!SD.exists("/users.txt")) return false;
  File db = SD.open("/users.txt", "r");
  while (db.available()) {
    String line = db.readStringUntil('\n'); line.trim();
    int split = line.indexOf(':');
    if (split > 0 && line.substring(0, split) == u && line.substring(split + 1) == p) { db.close(); return true; }
  }
  db.close(); return false;
}

bool registerUser(String u, String p) {
  if(u == "admin") return false;
  File db = SD.open("/users.txt", "r");
  while (db.available()) { String line = db.readStringUntil('\n'); if (line.startsWith(u + ":")) { db.close(); return false; } }
  db.close(); db = SD.open("/users.txt", FILE_APPEND); db.println(u + ":" + p); db.close(); SD.mkdir("/" + u); return true;
}

void deleteUserCompletely(String u) {
  if(u == "admin") return;
  File root = SD.open("/" + u);
  if(root){ File file = root.openNextFile(); while(file){ String path = String(root.path()) + "/" + file.name(); if(!file.isDirectory()) SD.remove(path); file = root.openNextFile(); } root.close(); SD.rmdir("/" + u); }
  File db = SD.open("/users.txt", "r"); File temp = SD.open("/temp.txt", FILE_WRITE);
  while(db.available()){ String line = db.readStringUntil('\n'); line.trim(); if(!line.startsWith(u + ":") && line.length() > 0) temp.println(line); }
  db.close(); temp.close(); SD.remove("/users.txt"); SD.rename("/temp.txt", "/users.txt");
}

int countUsers() {
  int count = 0;
  if (!SD.exists("/users.txt")) return 0;
  File db = SD.open("/users.txt", "r");
  while (db.available()) {
    String line = db.readStringUntil('\n'); line.trim();
    if (line.indexOf(':') > 0) count++;
  }
  db.close(); return count;
}

// ================= HTML =================
String getSetupPage() {
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'><title>Setup</title>";
  html += "<style>body{font-family:sans-serif;padding:20px;text-align:center;background:#1a1a1a;color:#fff} select,input,button{width:100%;padding:10px;margin:10px 0;} button{background:#00d2ff;border:none;font-weight:bold}</style></head><body>";
  html += "<h2>DEVICE SETUP</h2><form action='/save' method='POST'>";
  int n = WiFi.scanNetworks();
  if(n == 0) html += "<p>No networks.</p>";
  else { html += "<select name='ssid'>"; for (int i = 0; i < n; ++i) html += "<option value='" + WiFi.SSID(i) + "'>" + WiFi.SSID(i) + "</option>"; html += "</select>"; }
  html += "<input type='password' name='pass' placeholder='Password'><button type='submit'>CONNECT</button></form></body></html>";
  return html;
}

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>MediaCloud</title>
<link href="https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;600;700&display=swap" rel="stylesheet">
<style>
  :root {
    --bg1: #0f172a;
    --bg2: #1e1b4b;
    --glass: rgba(255, 255, 255, 0.05);
    --border: rgba(255, 255, 255, 0.1);
    --text: #e2e8f0;
    --accent: #38bdf8;
    --red: #f43f5e;
    --green: #10b981;
  }
  * { box-sizing: border-box; font-family: 'Outfit', sans-serif; }
  body {
    margin: 0; min-height: 100vh;
    background: linear-gradient(135deg, var(--bg1), var(--bg2));
    background-size: 400% 400%;
    animation: gradientBG 15s ease infinite;
    color: var(--text);
    padding: 20px;
    display: flex; flex-direction: column; align-items: center;
  }
  @keyframes gradientBG { 0%{background-position:0% 50%} 50%{background-position:100% 50%} 100%{background-position:0% 50%} }
  
  .card {
    background: var(--glass);
    backdrop-filter: blur(16px);
    -webkit-backdrop-filter: blur(16px);
    border: 1px solid var(--border);
    padding: 25px;
    border-radius: 20px;
    width: 100%; max-width: 500px;
    margin-bottom: 20px;
    box-shadow: 0 8px 32px 0 rgba(0,0,0,0.3);
    animation: slideUp 0.5s ease-out forwards;
    opacity: 0; transform: translateY(20px);
  }
  @keyframes slideUp { to { opacity: 1; transform: translateY(0); } }
  
  h2, h3 { margin: 0 0 15px 0; font-weight: 600; color: #fff; }
  .text-accent { color: var(--accent); }
  
  .sensor-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 15px; margin-top:10px; }
  .sensor-box {
    background: rgba(0,0,0,0.2); border-radius: 15px; padding: 15px; text-align: center;
    border: 1px solid var(--border); transition: transform 0.3s;
  }
  .sensor-box:hover { transform: translateY(-5px); }
  .sensor-val { font-size: 28px; font-weight: 700; display: block; margin-top: 5px; }
  .temp-val { color: #f87171; text-shadow: 0 0 10px rgba(248,113,113,0.4); }
  .hum-val { color: var(--accent); text-shadow: 0 0 10px rgba(56,189,248,0.4); }
  
  input, button {
    width: 100%; padding: 12px; margin: 8px 0;
    border-radius: 10px; font-size: 15px; outline: none; transition: 0.3s;
  }
  input { background: rgba(0,0,0,0.3); border: 1px solid var(--border); color: #fff; }
  input:focus { border-color: var(--accent); box-shadow: 0 0 10px rgba(56,189,248,0.2); }
  
  button {
    background: var(--accent); color: #000; border: none; font-weight: 700; cursor: pointer;
  }
  button:hover { filter: brightness(1.1); transform: scale(1.02); box-shadow: 0 0 15px rgba(56,189,248,0.4); }
  button.red { background: var(--red); color: white; }
  button.red:hover { box-shadow: 0 0 15px rgba(244,63,94,0.4); }
  button.dark { background: rgba(255,255,255,0.1); color: #fff; border: 1px solid var(--border); }
  button.dark:hover { background: rgba(255,255,255,0.2); box-shadow: none; }
  
  .hide { display: none !important; }
  table { width: 100%; border-collapse: collapse; margin-top: 10px; }
  th, td { padding: 12px; text-align: left; border-bottom: 1px solid var(--border); }
  th { font-weight: 600; color: var(--accent); }
  a { color: var(--accent); text-decoration: none; } a:hover { text-decoration: underline; }
  
  #progress-container { width: 100%; background: rgba(0,0,0,0.3); height: 8px; margin-top: 15px; border-radius: 4px; overflow: hidden; display: none; }
  #progress-bar { width: 0%; height: 100%; background: var(--accent); transition: 0.3s; box-shadow: 0 0 10px var(--accent); }
  
  #modal { display:none; position:fixed; top:0; left:0; width:100%; height:100%; background:rgba(0,0,0,0.85); backdrop-filter:blur(5px); z-index:1000; justify-content:center; align-items:center; }
  #modal.active { display:flex; animation: fadeIn 0.3s; }
  @keyframes fadeIn { from {opacity:0} to {opacity:1} }
  #modal-content { max-width:90%; max-height:80%; border-radius: 10px; box-shadow: 0 20px 50px rgba(0,0,0,0.5); }
  #modal-close { position:absolute; top:20px; right:20px; width:auto; padding:8px 20px; border-radius:30px; }
  
  .admin-row { display: flex; align-items: center; gap: 10px; margin-bottom: 15px; }
  .color-picker { width: 50px; height: 40px; padding: 0; border-radius: 8px; cursor: pointer; border: 1px solid var(--border); background: transparent; }
  hr { border: none; border-top: 1px solid var(--border); margin: 20px 0; }
</style>
</head>
<body>

<div class="card" style="animation-delay: 0s;">
  <h3 class="text-accent">Live Environment</h3>
  <div class="sensor-grid">
    <div class="sensor-box">
      <span style="font-size:14px; opacity:0.8;">Temperature</span>
      <span class="sensor-val temp-val" id="env-t">--.-°C</span>
    </div>
    <div class="sensor-box">
      <span style="font-size:14px; opacity:0.8;">Humidity</span>
      <span class="sensor-val hum-val" id="env-h">--.-%</span>
    </div>
  </div>
</div>

<div id="pg-login" class="card" style="animation-delay: 0.1s;">
  <h2>MediaCloud Portal</h2>
  <input id="u" placeholder="Username"><input id="p" type="password" placeholder="Password">
  <button onclick="doLogin()">ACCESS SYSTEM</button>
  <button class="dark" onclick="show('pg-signup')">CREATE ACCOUNT</button>
</div>

<div id="pg-signup" class="card hide">
  <h2>New Registration</h2>
  <input id="nu" placeholder="Username"><input id="np" type="password" placeholder="Password">
  <button onclick="doSignup()">REGISTER</button>
  <button class="dark" onclick="show('pg-login')">BACK TO LOGIN</button>
</div>

<div id="pg-dash" class="card hide">
  <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:20px;">
     <h3 class="text-accent" id="wel"></h3>
     <button class="dark" style="width:auto;padding:6px 15px;margin:0;" onclick="logout()">Log out</button>
  </div>
  
  <div style="background:rgba(0,0,0,0.2); padding:20px; border-radius:15px; border:1px dashed var(--border); margin-bottom:20px;">
    <input type="file" id="fup" multiple onchange="prep()" style="background:transparent; border:none; padding:0; cursor:pointer;">
    <button id="btn-start" onclick="upload()" class="dark hide">START UPLOAD</button>
    <div id="stat" style="margin-top:10px;font-size:13px;opacity:0.6;">Select files to upload</div>
    <div id="progress-container"><div id="progress-bar"></div></div>
  </div>

  <h3 style="font-size:16px;">Cloud Storage</h3>
  <div style="max-height:300px;overflow-y:auto; border-radius:10px;">
    <table id="list"></table>
  </div>
</div>

<div id="pg-admin" class="card hide">
  <h2 style="color:var(--red)">Admin Console</h2>
  <p style="opacity:0.8">Storage Usage: <strong id="adm-usage" style="color:var(--accent)"></strong></p>
  <hr>
  
  <h3>Device Controls</h3>
  <div class="admin-row">
    <input type="color" id="rgb-color" class="color-picker" value="#00d2ff">
    <button onclick="setColor()" style="margin:0;">SET NEOPIXEL COLOR</button>
  </div>
  <button class="dark" onclick="syncTime(event)">SYNC LOCAL TIME</button>
  <button class="red" onclick="resetWifi()">FACTORY RESET WIFI</button>
  <hr>

  <h3>Manage Users</h3>
  <div id="adm-list" style="height:180px;overflow-y:auto; padding-right:5px;"></div>
  <button class="dark" style="margin-top:20px;" onclick="logout()">EXIT CONSOLE</button>
</div>

<div id="modal">
  <button id="modal-close" class="red" onclick="closeModal()">CLOSE</button>
  <div id="modal-body"></div>
</div>

<script>
let curUser='', isPaused=false, isCancelled=false;

  const updateEnv = () => {
  fetch('/api/sensor').then(r=>r.json()).then(d=>{
    document.getElementById('env-t').innerText = d.t.toFixed(1) + "\u00B0C";
    document.getElementById('env-h').innerText = d.h.toFixed(1) + "%";
  }).catch(()=>{});
}
setInterval(updateEnv, 5000); updateEnv();

  const setColor = () => {
  let hex = document.getElementById('rgb-color').value;
  let r = parseInt(hex.substr(1,2),16); let g = parseInt(hex.substr(3,2),16); let b = parseInt(hex.substr(5,2),16);
  fetch(`/api/setcolor?r=${r}&g=${g}&b=${b}`).then(()=>{
    let btn = event.target; let old = btn.innerText;
    btn.innerText = "COLOR APPLIED!"; setTimeout(()=>btn.innerText=old, 1500);
  });
}
  const syncTime = (e) => {
  let epoch = Math.floor(Date.now() / 1000);
  let tz = -(new Date().getTimezoneOffset()) * 60;
  fetch(`/api/settime?t=${epoch}&tz=${tz}`).then(()=>{
    let btn = e.target || e.srcElement; let old = btn.innerText;
    btn.innerText = "SYNCED!"; setTimeout(()=>btn.innerText=old, 1500);
  });
}

  const show = (id) => {document.querySelectorAll('.card:not(:first-child)').forEach(d=> {if(d.id && d.id.startsWith('pg-')) d.classList.add('hide');}); document.getElementById(id).classList.remove('hide');}
  const doLogin = () => {
  let u=document.getElementById('u').value, p=document.getElementById('p').value;
  let btn=event.target; btn.innerText="VERIFYING...";
  fetch('/api/login?u='+u+'&p='+p).then(r=>r.text()).then(t=>{
    if(t=='OK'){curUser=u;dash();} else if(t=='ADMIN'){curUser='admin';admin();} else { alert('Access Denied'); btn.innerText="ACCESS SYSTEM"; }
  }).catch(()=>{btn.innerText="ACCESS SYSTEM";});
}
  const doSignup = () => { fetch('/api/signup?u='+document.getElementById('nu').value+'&p='+document.getElementById('np').value).then(r=>r.text()).then(t=>{if(t=='OK')alert('Account Created');show('pg-login');}); }

  const dash = () => {
  show('pg-dash'); document.getElementById('wel').innerText="Welcome, "+curUser;
  fetch('/api/list?u='+curUser).then(r=>r.json()).then(d=>{
    let h='<tr><th>Filename</th><th>Size</th><th>Actions</th></tr>'; 
    d.files.forEach(f=>{
       let ext = f.n.split('.').pop().toLowerCase();
       let isImg = ['jpg','jpeg','png','gif'].includes(ext); let isVid = ['mp4','webm','mov'].includes(ext);
       let viewBtn = '';
       if(isImg) viewBtn = `<button class="dark" style="padding:4px 10px;width:auto;margin:0 5px;" onclick="viewMedia('${f.n}','img')">VIEW</button>`;
       if(isVid) viewBtn = `<button class="dark" style="padding:4px 10px;width:auto;margin:0 5px;" onclick="viewMedia('${f.n}','vid')">PLAY</button>`;
       h+=`<tr><td><a href="/api/dl?u=${curUser}&f=${f.n}&download=1">${f.n}</a></td><td>${(f.s/1024).toFixed(1)} KB</td><td><div style="display:flex;">${viewBtn}<button class="red" style="padding:4px 10px;width:auto;margin:0;" onclick="del('${f.n}')">DEL</button></div></td></tr>`;
    });
    document.getElementById('list').innerHTML=h;
  });
}

  const viewMedia = (fname, type) => {
  let url = `/api/dl?u=${curUser}&f=${fname}`; 
  let body = document.getElementById('modal-body');
  if(type=='img') body.innerHTML = `<img src="${url}" id="modal-content">`;
  else if(type=='vid') body.innerHTML = `<video controls autoplay id="modal-content"><source src="${url}" type="video/mp4"></video>`;
  document.getElementById('modal').classList.add('active');
}
  const closeModal = () => { document.getElementById('modal-body').innerHTML = ""; document.getElementById('modal').classList.remove('active'); }

  const admin = () => {
  show('pg-admin');
  fetch('/api/admin').then(r=>r.json()).then(d=>{
    document.getElementById('adm-usage').innerText = d.used + " / " + d.total;
    let h=''; d.users.forEach(u=>{h+=`<div style="background:rgba(0,0,0,0.3);padding:15px;border-radius:10px;margin-bottom:10px;border-left:4px solid var(--red); display:flex; justify-content:space-between; align-items:center;"><strong>${u.name}</strong><button class="red" style="width:auto;padding:5px 12px;margin:0;" onclick="nuke('${u.name}')">REVOKE</button></div>`;});
    document.getElementById('adm-list').innerHTML=h;
  });
}
  const prep = () => {document.getElementById('btn-start').classList.remove('hide');}

  const upload = async () => {
  let files=document.getElementById('fup').files; if(files.length==0)return;
  document.getElementById('btn-start').classList.add('hide'); document.getElementById('progress-container').style.display='block';
  for(let i=0;i<files.length;i++){
    document.getElementById('stat').innerText=`Uploading ${files[i].name}...`;
    await new Promise((res,rej)=>{
      let xhr=new XMLHttpRequest(); let fd=new FormData();fd.append("file",files[i]);
      xhr.upload.onprogress=e=>{if(e.lengthComputable)document.getElementById('progress-bar').style.width=Math.round((e.loaded/e.total)*100)+"%"};
      xhr.onload=()=>res(); xhr.onerror=()=>rej(); xhr.open("POST","/api/up?u="+curUser); xhr.send(fd);
    }).catch(()=>{});
  }
  document.getElementById('stat').innerText="Transfer Complete!"; setTimeout(()=>{dash();document.getElementById('progress-container').style.display='none';document.getElementById('progress-bar').style.width='0%';},1500);
}
  const del = (f) => {if(confirm('Delete file permanently?')) fetch(`/api/del?u=${curUser}&f=${f}`).then(dash);}
  const nuke = (u) => {if(confirm('WARNING: PERMANENTLY DELETE USER AND ALL FILES?'))fetch(`/api/nuke?u=${u}`).then(admin);}
  const resetWifi = () => {if(confirm('WARNING: Device will disconnect and reboot into setup mode. Continue?'))fetch('/api/resetwifi').then(()=>alert('Device Rebooting...'));}
  const logout = () => {location.reload();}
</script></body></html>
)rawliteral";

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  pinMode(TOUCH_PIN, INPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  dht.begin();
  
  // OLED Setup
  Wire.begin(OLED_SDA, OLED_SCL);
  if(!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED init failed");
  }
  
  tft.init(); tft.setRotation(1); 
  spr.createSprite(320, 172);
  spr.fillSprite(TFT_BLACK); spr.pushSprite(0, 0);
  
  writeToPixels(20, 20, 0); // Boot Yellow
  
  SPI.begin(7, 5, 6, SD_CS_PIN);
  if(!SD.begin(SD_CS_PIN, SPI, 20000000)) { 
     spr.setTextColor(TFT_RED); spr.println("SD FAIL"); spr.pushSprite(0,0); writeToPixels(255, 0, 0); return; 
  }

  String ssid, pass;
  credsFound = loadWiFiCreds(ssid, pass);
  
  if(credsFound) {
    spr.setCursor(0,0); spr.setTextColor(TFT_ORANGE); spr.println("INIT SYSTEM..."); spr.pushSprite(0,0);
    WiFi.begin(ssid.c_str(), pass.c_str());
    int r = 0;
    while(WiFi.status() != WL_CONNECTED && r < 20) {
      delay(250); writeToPixels(0, 0, 50); delay(250); writeToPixels(0, 0, 0);
      spr.print("."); spr.pushSprite(0,0); r++;
    }
    if(WiFi.status() != WL_CONNECTED) credsFound = false;
  }

  if(!credsFound) {
    isCloudMode = false;
    uiState = 5; // Setup Mode UI
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("ESP-MEDIA", "");
    writeToPixels(0, 0, 255); 
    server.on("/", HTTP_GET, [](){ server.send(200, "text/html", getSetupPage()); });
    server.on("/save", HTTP_POST, [](){
       String s = server.arg("ssid"); String p = server.arg("pass");
       if(s.length()>0) { saveWiFiCreds(s,p); server.send(200,"text/html","<h1>SAVED. REBOOTING...</h1>"); delay(1000); ESP.restart(); }
    });
    server.begin();
  } else {
    isCloudMode = true;
    configTime(userGmtOffset, 0, ntpServer); // Setup NTP time with IST offset
    uiState = 0; // Default UI
    
    // Cloud API routes
    server.on("/", HTTP_GET, [](){ server.send(200, "text/html", index_html); });
    server.on("/api/login", HTTP_GET, [](){ if(server.arg("u")=="admin" && server.arg("p")=="admin") server.send(200,"text/plain","ADMIN"); else if(checkLogin(server.arg("u"),server.arg("p"))) server.send(200,"text/plain","OK"); else server.send(401,"text/plain","FAIL"); });
    server.on("/api/signup", HTTP_GET, [](){ if(registerUser(server.arg("u"),server.arg("p"))) server.send(200,"text/plain","OK"); else server.send(400); });
    server.on("/api/list", HTTP_GET, [](){
      String u = server.arg("u"); String json = "{\"files\":[";
      File root = SD.open("/" + u); File f = root.openNextFile(); bool first=true;
      while(f){ if(!f.isDirectory()){ if(!first)json+=","; json+="{\"n\":\""+String(f.name())+"\",\"s\":"+String(f.size())+"}"; first=false; } f = root.openNextFile(); }
      json += "]}"; server.send(200, "application/json", json);
    });
    server.on("/api/admin", HTTP_GET, [](){
      String json = "{ \"used\":\"" + String(SD.usedBytes()/(1024*1024)) + "MB\", \"total\":\"" + String(SD.totalBytes()/(1024*1024)) + "MB\", \"users\":[";
      File root = SD.open("/"); File uD = root.openNextFile(); bool fU = true;
      while(uD){ if(uD.isDirectory() && String(uD.name())!="System Volume Information"){
        if(!fU)json+=","; json+="{\"name\":\""+String(uD.name())+"\",\"files\":[]}"; fU = false; 
      } uD = root.openNextFile(); } json += "]}"; server.send(200, "application/json", json);
    });
    
    server.on("/api/dl", HTTP_GET, [](){
       String path = "/"+server.arg("u")+"/"+server.arg("f");
       if(SD.exists(path)){ 
         File f=SD.open(path,"r"); 
         String mime = getContentType(server.arg("f")); 
         if (server.hasArg("download")) server.sendHeader("Content-Disposition", "attachment; filename=\"" + server.arg("f") + "\"");
         else { server.sendHeader("Content-Disposition", "inline"); server.sendHeader("Access-Control-Allow-Origin", "*"); }
         server.streamFile(f, mime); f.close(); 
       } else server.send(404);
    });
  
    server.on("/api/del", HTTP_GET, [](){ SD.remove("/"+server.arg("u")+"/"+server.arg("f")); server.send(200,"text/plain","Del"); });
    server.on("/api/nuke", HTTP_GET, [](){ deleteUserCompletely(server.arg("u")); server.send(200); });
    server.on("/api/resetwifi", HTTP_GET, [](){ deleteWiFiCreds(); server.send(200); delay(1000); ESP.restart(); });
    
    server.on("/api/sensor", HTTP_GET, [](){
      String json = "{\"t\":" + String(currentTemp) + ",\"h\":" + String(currentHum) + "}";
      server.send(200, "application/json", json);
    });
    server.on("/api/setcolor", HTTP_GET, [](){
      if(server.hasArg("r")) customR = server.arg("r").toInt();
      if(server.hasArg("g")) customG = server.arg("g").toInt();
      if(server.hasArg("b")) customB = server.arg("b").toInt();
      ledMode = 0; ledPower = true;
      server.send(200, "text/plain", "OK");
    });
    server.on("/api/settime", HTTP_GET, [](){
      if(server.hasArg("t")) {
        time_t epoch = (time_t)strtoll(server.arg("t").c_str(), NULL, 10);
        if(server.hasArg("tz")) {
          userGmtOffset = strtol(server.arg("tz").c_str(), NULL, 10);
        }
        struct timeval tv; tv.tv_sec = epoch; tv.tv_usec = 0;
        settimeofday(&tv, NULL);
        configTime(userGmtOffset, 0, ntpServer);
        server.send(200, "text/plain", "OK");
      } else server.send(400);
    });
  
    server.on("/api/up", HTTP_POST, [](){ server.send(200); isUploading = false; }, [](){
      HTTPUpload& u = server.upload();
      if(u.status == UPLOAD_FILE_START){
        isUploading = true; writeToPixels(200, 0, 255);
        String path = "/"+server.arg("u")+"/"+u.filename;
        if(!SD.exists("/"+server.arg("u"))) SD.mkdir("/"+server.arg("u"));
        if(SD.exists(path)) SD.remove(path);
        uploadFile = SD.open(path, FILE_WRITE);
      } else if(u.status == UPLOAD_FILE_WRITE){
        if(uploadFile) uploadFile.write(u.buf, u.currentSize);
      } else if(u.status == UPLOAD_FILE_END){
        if(uploadFile) uploadFile.close();
      }
    });
  
    server.begin();
  }
}

void handleInputs() {
  bool btn = (digitalRead(BUTTON_PIN) == BUTTON_PRESSED_STATE);
  
  if (btn && !lastBtnState) {
    if (millis() - btnPressTime > 50) { // Debounce
      btnPressTime = millis();
      btnLongPressed = false;
      
      // Change UI state instantly on PRESS
      if(uiState == 5) uiState = 0;
      else uiState = (uiState + 1) % 5;
      lastDisplayUpdate = 0; // force redraw
    }
  } else if (btn && lastBtnState) {
    if (millis() - btnPressTime > 3000 && !btnLongPressed) {
      btnLongPressed = true;
      deleteWiFiCreds();
      spr.fillSprite(TFT_RED); spr.setCursor(10,10); spr.setTextSize(2); spr.setTextColor(TFT_WHITE); spr.println("WIFI RESET!"); spr.pushSprite(0,0);
      oled.clearDisplay(); oled.setCursor(0,0); oled.println("WIFI RESET!"); oled.display();
      delay(1000); ESP.restart();
    }
  } else if (!btn && lastBtnState) {
     // Button released
  }
  
  lastBtnState = btn;

  bool tch = digitalRead(TOUCH_PIN) == HIGH;
  if (tch && lastTouchState == LOW) {
    touchPressTime = millis();
    touchLongPressed = false;
  } else if (tch && lastTouchState == HIGH) {
    if (millis() - touchPressTime > 5000 && !touchLongPressed) {
      touchLongPressed = true;
      ledPower = !ledPower;
      tapCount = 0;
    }
  } else if (!tch && lastTouchState == HIGH) {
    unsigned long dur = millis() - touchPressTime;
    if (!touchLongPressed && dur > 20 && dur < 1000) { 
      tapCount++;
      lastTapTime = millis();
    }
  }
  lastTouchState = tch;

  if (tapCount > 0 && (millis() - lastTapTime > 300)) {
    if (tapCount == 1) {
      colorIndex = (colorIndex + 1) % 6;
      if(colorIndex==0) {customR=255; customG=0; customB=0;}
      if(colorIndex==1) {customR=0; customG=255; customB=0;}
      if(colorIndex==2) {customR=0; customG=0; customB=255;}
      if(colorIndex==3) {customR=255; customG=255; customB=0;}
      if(colorIndex==4) {customR=0; customG=255; customB=255;}
      if(colorIndex==5) {customR=255; customG=0; customB=255;}
      ledMode = 0; ledPower = true;
    } else if (tapCount == 2) {
      ledMode = (ledMode + 1) % 3;
      ledPower = true;
    }
    tapCount = 0;
  }
}

void updateDisplay() {
  if(millis() - lastDisplayUpdate < 1000) return;
  lastDisplayUpdate = millis();
  
  // --- TFT DISPLAY UPDATE (Only Shows IP / Server Status) ---
  spr.fillSprite(TFT_BLACK);
  if(uiState == 5) {
    spr.fillRect(0, 0, 320, 30, spr.color565(20, 20, 20));
    spr.setCursor(5, 5); spr.setTextSize(2); spr.setTextColor(TFT_RED);
    spr.println("WIFI SETUP");
    spr.setCursor(10, 60); spr.setTextSize(2); spr.setTextColor(TFT_WHITE);
    spr.println("Connect to WiFi:");
    spr.setCursor(10, 90); spr.setTextColor(TFT_ORANGE);
    spr.println("ESP-MEDIA");
    spr.setCursor(10, 130); spr.setTextColor(TFT_WHITE);
    spr.println("IP: 192.168.4.1");
  } else {
    spr.fillRect(0, 0, 320, 30, spr.color565(20, 20, 20));
    spr.setCursor(5, 5); spr.setTextSize(2); spr.setTextColor(spr.color565(88, 166, 255));
    spr.println("MEDIA CLOUD HOST");
    
    spr.setCursor(10, 60); spr.setTextSize(2); spr.setTextColor(TFT_WHITE);
    spr.println("IP Address:");
    spr.setCursor(10, 90); spr.setTextSize(3); spr.setTextColor(TFT_GREEN);
    spr.println(WiFi.localIP().toString());
  }
  spr.pushSprite(0, 0);

  // --- OLED DISPLAY UPDATE (Cycles UI via Button) ---
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  
  if(uiState == 0) {
    // --- SENSOR DATA ---
    oled.drawLine(0, 11, 128, 11, SSD1306_WHITE);
    oled.setCursor(30, 2); oled.println("ENVIRONMENT");
    
    oled.setCursor(5, 18); oled.setTextSize(2);
    oled.printf("%.1f", currentTemp);
    oled.setTextSize(1); oled.print("C");
    
    oled.setCursor(5, 42); oled.setTextSize(2);
    oled.printf("%.1f", currentHum);
    oled.setTextSize(1); oled.print("%");
    
    oled.drawLine(0, 58, 128, 58, SSD1306_WHITE);
    oled.setCursor(80, 20); oled.setTextSize(1); oled.println("TEMP");
    oled.setCursor(80, 44); oled.println("HUMID");
    
  } else if(uiState == 1) {
    // --- DATE & TIME with AM/PM ---
    struct tm timeinfo;
    if(getLocalTime(&timeinfo, 0)) {
      oled.drawLine(0, 11, 128, 11, SSD1306_WHITE);
      oled.setCursor(30, 2); oled.println("DATE & TIME");
      
      // Date: DD/MM/YYYY
      oled.setCursor(15, 16); oled.setTextSize(1);
      oled.printf("%02d/%02d/%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
      
      // Day of week
      const char* days[] = {"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
      oled.setCursor(35, 28); oled.println(days[timeinfo.tm_wday]);
      
      // Time in 12hr AM/PM
      int hr = timeinfo.tm_hour;
      const char* ampm = (hr >= 12) ? "PM" : "AM";
      if(hr == 0) hr = 12;
      else if(hr > 12) hr -= 12;
      
      oled.setCursor(10, 42); oled.setTextSize(2);
      oled.printf("%02d:%02d:%02d", hr, timeinfo.tm_min, timeinfo.tm_sec);
      oled.setTextSize(1); oled.setCursor(108, 48); oled.print(ampm);
    } else {
      oled.setCursor(0, 2); oled.println("DATE & TIME");
      oled.drawLine(0, 11, 128, 11, SSD1306_WHITE);
      oled.setCursor(15, 28); oled.setTextSize(1);
      oled.println("Sync from Admin");
      oled.setCursor(15, 40);
      oled.println("Panel or wait NTP");
    }
    
  } else if(uiState == 2) {
    oled.drawLine(0, 11, 128, 11, SSD1306_WHITE);
    oled.setCursor(40, 2); oled.println("NETWORK");
    oled.setCursor(0, 25); oled.setTextSize(2);
    oled.println(WiFi.localIP().toString());
    
  } else if(uiState == 3) {
    oled.drawLine(0, 11, 128, 11, SSD1306_WHITE);
    oled.setCursor(45, 2); oled.println("USERS");
    oled.setCursor(40, 22); oled.setTextSize(3);
    oled.println(countUsers());
    oled.setTextSize(1); oled.setCursor(30, 52); oled.println("registered");
    
  } else if(uiState == 4) {
    oled.drawLine(0, 11, 128, 11, SSD1306_WHITE);
    oled.setCursor(38, 2); oled.println("STORAGE");
    oled.setCursor(0, 18); oled.setTextSize(1);
    oled.printf("Used:  %llu MB", SD.usedBytes() / (1024*1024));
    oled.setCursor(0, 30);
    oled.printf("Total: %llu MB", SD.totalBytes() / (1024*1024));
    
    oled.drawRect(0, 46, 128, 14, SSD1306_WHITE);
    int p = (int)((float)SD.usedBytes() / SD.totalBytes() * 126);
    oled.fillRect(1, 47, p, 12, SSD1306_WHITE);
    int pct = (int)((float)SD.usedBytes() / SD.totalBytes() * 100);
    oled.setCursor(50, 49); 
    if(p > 50) oled.setTextColor(SSD1306_BLACK); 
    oled.printf("%d%%", pct);
    oled.setTextColor(SSD1306_WHITE);
    
  } else if(uiState == 5) {
    oled.drawLine(0, 11, 128, 11, SSD1306_WHITE);
    oled.setCursor(20, 2); oled.println("WIFI SETUP");
    oled.setCursor(10, 22); oled.setTextSize(1);
    oled.println("Check Main Screen");
    oled.setCursor(10, 34);
    oled.println("to connect to WiFi.");
  }
  
  oled.display();
}

void loop() {
  server.handleClient();
  
  if (millis() - lastDhtRead > 2000) {
    lastDhtRead = millis();
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t)) currentTemp = t;
    if (!isnan(h)) currentHum = h;
  }
  
  handleInputs();
  
  if (!isUploading) {
    updateNeoPixels();
    updateDisplay();
  }
}