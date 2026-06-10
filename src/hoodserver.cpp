// Novy hood WiFi controller — renedis ESP32_Novy_Controller RF logic, ported to
// our CC1101 (async-OOK TX on GDO0) with a small web UI. MQTT stripped for simplicity.
//
//   pio run -e hood -t upload -t monitor
//
// Faithful to renedis: protocol 12, pulse 350, code sent as a bit-STRING,
// repeat counts (light=2, brightness=4, brightness-low=10, others=3), 50ms gaps.

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <RCSwitch.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <Preferences.h>
#include <DNSServer.h>
#include <PubSubClient.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>      // TLS client for HTTPS to GitHub (arduino-esp32 3.x)
#include "esp_wifi.h"
#include "pins.h"
#include "config.h"

enum ButtonCommand { CMD_LIGHT, CMD_POWER, CMD_PLUS, CMD_MINUS, CMD_NOVY };

RCSwitch transmitter = RCSwitch();
WebServer server(80);

Preferences prefs;            // NVS store for WiFi + MQTT settings
DNSServer  dnsServer;         // captive-portal DNS (wildcard) while in setup mode
bool   portalActive = false;  // true while the setup hotspot + portal are running
String staSSID, staPass;      // active credentials (loaded from NVS, else config.h defaults)
const byte DNS_PORT = 53;
const int  SETUP_BTN_PIN = 9;      // ESP32-C3 BOOT button — hold at reset to force the portal

// ---- MQTT (Home Assistant auto-discovery; mirrors renedis ESP32_Novy_Controller) ----
WiFiClient   mqttNet;
PubSubClient mqtt(mqttNet);
bool   mqttEnabled = false;                  // runtime-configurable from /mqtt
String mqttHost, mqttUser, mqttPass, mqttPrefix;
uint16_t mqttPort = 1883;
bool   haDiscoverySent = false;

// The buttons exposed over MQTT / HA discovery (the 5 real remote buttons).
struct MqttBtn { const char* id; const char* name; ButtonCommand cmd; const char* icon; };
const MqttBtn MQTT_BTNS[] = {
  { "power", "Power",       CMD_POWER, "mdi:power" },
  { "plus",  "Faster",      CMD_PLUS,  "mdi:fan-plus" },
  { "minus", "Slower",      CMD_MINUS, "mdi:fan-minus" },
  { "novy",  "Novy (auto)", CMD_NOVY,  "mdi:fan-auto" },
  { "light", "Light",       CMD_LIGHT, "mdi:lightbulb" },
};
const int MQTT_BTN_N = sizeof(MQTT_BTNS) / sizeof(MQTT_BTNS[0]);

// ---- Pull-OTA self-update from a GitHub release ----
const char* FW_VER = FW_VERSION;     // running version (compile-time, from VERSION)
volatile bool g_doUpdate = false;    // set by POST /update, consumed in loop()
String g_latestVersion;              // last value fetched from version.txt
String g_updateError;                // human-readable last failure

const int LOG_N = 12;
String logBuf[LOG_N];
int logIdx = 0;
void logMsg(const String& m) { logBuf[logIdx] = m; logIdx = (logIdx + 1) % LOG_N; Serial.println(m); }
String logHtml() {
  String s;
  for (int i = 0; i < LOG_N; i++) { String l = logBuf[(logIdx + i) % LOG_N]; if (l.length()) s += l + "<br>"; }
  return s;
}

// ---- WiFi credential storage (NVS namespace "wifi") ----
void loadCreds() {
  prefs.begin("wifi", true);                       // read-only
  staSSID = prefs.getString("ssid", DEFAULT_SSID); // empty NVS -> compile-time defaults
  staPass = prefs.getString("pass", DEFAULT_PASSWORD);
  prefs.end();
}
void saveCreds(const String& s, const String& p) {
  prefs.begin("wifi", false);
  prefs.putString("ssid", s);
  prefs.putString("pass", p);
  prefs.end();
}
void forgetCreds() {
  prefs.begin("wifi", false);
  prefs.clear();                                   // clears only the "wifi" namespace
  prefs.end();
}

// ---- MQTT settings storage (NVS namespace "mqtt"), config.h values as defaults ----
void loadMqtt() {
  prefs.begin("mqtt", true);
  mqttEnabled = prefs.getBool("en", MQTT_ENABLED);
  mqttHost    = prefs.getString("host", MQTT_SERVER);
  mqttPort    = prefs.getUShort("port", MQTT_PORT);
  mqttUser    = prefs.getString("user", MQTT_USER);
  mqttPass    = prefs.getString("pass", MQTT_PASSWORD);
  mqttPrefix  = prefs.getString("prefix", MQTT_PREFIX);
  prefs.end();
}
void saveMqtt(bool en, const String& host, uint16_t port, const String& user, const String& pass, const String& prefix) {
  prefs.begin("mqtt", false);
  prefs.putBool("en", en);
  prefs.putString("host", host);
  prefs.putUShort("port", port);
  prefs.putString("user", user);
  prefs.putString("pass", pass);
  prefs.putString("prefix", prefix);
  prefs.end();
}

// ---- STA connect (preserves the proven 802.11b-only minimal config) ----
bool connectSTA(uint32_t timeoutMs) {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setHostname(HOSTNAME.c_str());
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B);  // must stay: 11b-only PHY
  WiFi.begin(staSSID.c_str(), staPass.c_str());
  Serial.printf("Connecting to WiFi '%s' ...\n", staSSID.c_str());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) { delay(400); Serial.print("."); }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

// ---- Setup hotspot + captive portal ----
void startPortal() {
  portalActive = true;
  WiFi.mode(WIFI_AP_STA);                          // AP_STA so /scan still works in the portal
  WiFi.softAP(AP_SSID);                            // open network (no password)
  IPAddress apIP = WiFi.softAPIP();                // 192.168.4.1
  dnsServer.start(DNS_PORT, "*", apIP);            // wildcard DNS -> captive redirect
  Serial.printf("PORTAL up. Join '%s', open http://%s/\n", AP_SSID, apIP.toString().c_str());
  logMsg(String("PORTAL: join ") + AP_SSID);
}

// Shared theme + base styles for both pages (inline; no external assets — works offline).
#define NOVY_CSS \
":root{--bg:#f5f6f8;--card:#fff;--fg:#1a1a1c;--muted:#6b7280;--line:#e5e7eb;--accent:#007aff;--ap:#0051d5;--ok:#1db954;--sh:0 1px 3px rgba(0,0,0,.08),0 6px 22px rgba(0,0,0,.06)}" \
"@media(prefers-color-scheme:dark){:root{--bg:#0b0c10;--card:#16181d;--fg:#f0f1f3;--muted:#9aa0aa;--line:#262a31;--accent:#0a84ff;--ap:#409cff;--ok:#30d158;--sh:0 1px 2px rgba(0,0,0,.4)}}" \
"*{box-sizing:border-box}" \
"body{margin:0;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Helvetica,Arial,sans-serif;background:var(--bg);color:var(--fg);padding:max(16px,env(safe-area-inset-top)) max(16px,env(safe-area-inset-right)) max(28px,env(safe-area-inset-bottom)) max(16px,env(safe-area-inset-left))}" \
".wrap{max-width:560px;margin:0 auto}" \
"header{position:sticky;top:0;display:flex;align-items:center;justify-content:space-between;gap:12px;padding:8px 2px 16px;background:linear-gradient(var(--bg),var(--bg) 70%,transparent);z-index:5}" \
"h1{font-size:22px;font-weight:680;margin:0;letter-spacing:-.02em}" \
".pill{display:inline-flex;align-items:center;gap:6px;font-size:12px;font-weight:600;color:var(--muted);background:var(--card);border:1px solid var(--line);padding:6px 10px;border-radius:999px;white-space:nowrap}" \
".dot{width:8px;height:8px;border-radius:50%;background:var(--muted);transition:background .3s}" \
".pill.on .dot{background:var(--ok);box-shadow:0 0 0 3px color-mix(in srgb,var(--ok) 25%,transparent)}" \
".card{background:var(--card);border:1px solid var(--line);border-radius:18px;padding:16px;margin:0 0 14px;box-shadow:var(--sh)}" \
".card h2{font-size:12px;text-transform:uppercase;letter-spacing:.07em;color:var(--muted);margin:2px 2px 12px;font-weight:700}" \
".grid{display:grid;gap:10px;grid-template-columns:repeat(auto-fit,minmax(150px,1fr))}" \
"button{font:inherit;font-size:16px;font-weight:600;color:var(--fg);background:var(--bg);border:1px solid var(--line);border-radius:14px;padding:15px;min-height:54px;cursor:pointer;display:flex;align-items:center;justify-content:center;gap:8px;transition:transform .06s,background .15s,border-color .15s;-webkit-tap-highlight-color:transparent}" \
"button:hover{border-color:var(--accent)}button:active{transform:scale(.97)}" \
"button.sent{background:color-mix(in srgb,var(--accent) 16%,var(--card));border-color:var(--accent)}" \
"button:focus-visible{outline:2px solid var(--accent);outline-offset:2px}" \
".primary{background:var(--accent);color:#fff;border-color:transparent}" \
".primary:hover{background:var(--ap)}.primary.sent{background:var(--ap)}" \
".span{grid-column:1/-1}" \
"svg{width:20px;height:20px;flex:0 0 auto}" \
"@media(prefers-reduced-motion:reduce){*{transition:none!important}}"

const char PAGE[] PROGMEM = R"HTML(<!doctype html><html lang=en><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name=color-scheme content="light dark">
<meta name=theme-color content="#0b0c10" media="(prefers-color-scheme:dark)">
<meta name=theme-color content="#f5f6f8" media="(prefers-color-scheme:light)">
<title>Novy Hood</title><style>)HTML" NOVY_CSS R"HTML(
.seg{display:grid;grid-template-columns:1fr 1fr;gap:10px}
details.log{margin-top:2px}
details.log summary{cursor:pointer;color:var(--muted);font-size:13px;font-weight:600;padding:8px 2px;list-style:none}
details.log summary::-webkit-details-marker{display:none}
details.log summary::before{content:"\25B8  "}details.log[open] summary::before{content:"\25BE  "}
#log{margin-top:8px;background:var(--card);border:1px solid var(--line);border-radius:14px;padding:12px;font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:12px;line-height:1.6;color:var(--muted);white-space:pre-wrap;word-break:break-word;max-height:220px;overflow:auto}
footer{display:flex;gap:18px;justify-content:center;margin-top:18px}
footer a{color:var(--muted);font-size:13px;text-decoration:none;font-weight:600}footer a:hover{color:var(--accent)}
#toast{position:fixed;left:50%;bottom:calc(26px + env(safe-area-inset-bottom));transform:translateX(-50%) translateY(20px);background:#000;color:#fff;padding:10px 16px;border-radius:12px;font-size:14px;font-weight:600;opacity:0;pointer-events:none;transition:opacity .2s,transform .2s;z-index:20}
#toast.show{opacity:.92;transform:translateX(-50%) translateY(0)}
</style></head><body><div class=wrap>
<header><h1>Novy Hood</h1><span class=pill id=pill><span class=dot></span><span id=pt>connecting</span></span></header>
<div class=card><h2>Fan</h2><div class=grid>
<button class="primary span" onclick="c('togglePower',this)" aria-label=Power><svg viewBox="0 0 24 24" fill=none stroke=currentColor stroke-width=2 stroke-linecap=round><path d="M12 3v9"/><path d="M6.6 6.6a8 8 0 1 0 10.8 0"/></svg>Power</button>
<div class="seg span">
<button onclick="c('toggleMinus',this)" aria-label="Speed down"><svg viewBox="0 0 24 24" fill=none stroke=currentColor stroke-width=2 stroke-linecap=round><path d="M5 12h14"/></svg>Slower</button>
<button onclick="c('togglePlus',this)" aria-label="Speed up"><svg viewBox="0 0 24 24" fill=none stroke=currentColor stroke-width=2 stroke-linecap=round><path d="M12 5v14M5 12h14"/></svg>Faster</button></div>
<button class=span onclick="c('toggleNovy',this)" aria-label="Novy auto mode">Novy (auto)</button>
</div></div>
<div class=card><h2>Light</h2><div class=grid>
<button class=span onclick="c('toggleLight',this)" aria-label=Light><svg viewBox="0 0 24 24" fill=none stroke=currentColor stroke-width=2 stroke-linecap=round><path d="M9 18h6M10 21h4"/><path d="M12 3a6 6 0 0 0-4 10.5c.6.6 1 1.4 1 2.2V16h6v-.3c0-.8.4-1.6 1-2.2A6 6 0 0 0 12 3Z"/></svg>Light</button>
</div></div>
<details class=log><summary>Activity log</summary><div id=log>%LOG%</div></details>
<footer><a href=/wifi>WiFi</a><a href=/mqtt>MQTT</a><a href=# id=upd style=display:none onclick="doUpd();return false">Update</a><a href=# onclick="if(confirm('Reboot the device?'))fetch('/reset');return false">Reboot</a></footer>
<div id=ver style="text-align:center;color:var(--muted);font-size:12px;margin-top:10px">&nbsp;</div>
</div><div id=toast></div><script>
var pill=document.getElementById('pill'),pt=document.getElementById('pt'),logEl=document.getElementById('log'),toast=document.getElementById('toast'),tt,lastLog='';
function showToast(m){toast.textContent=m;toast.classList.add('show');clearTimeout(tt);tt=setTimeout(function(){toast.classList.remove('show')},1400)}
function c(x,b){var lbl=b?b.textContent.trim():'';if(b){b.classList.add('sent');setTimeout(function(){b.classList.remove('sent')},450)}
fetch('/'+x).then(function(r){return r.text()}).then(function(t){lastLog=t;logEl.innerHTML=t;setOnline(true);showToast((lbl||'Command')+' sent')}).catch(function(){showToast('failed')})}
function setOnline(ok){pill.classList.toggle('on',ok);pt.textContent=ok?('v%FW_VERSION%'):'offline'}
function load(){fetch('/log').then(function(r){return r.text()}).then(function(t){if(t!==lastLog){lastLog=t;logEl.innerHTML=t}setOnline(true)}).catch(function(){setOnline(false)})}
load();setInterval(function(){if(!document.hidden)load()},1500);document.addEventListener('visibilitychange',function(){if(!document.hidden)load()});
var verEl=document.getElementById('ver'),updEl=document.getElementById('upd'),latest='';
function checkUpd(){fetch('/update/check').then(function(r){return r.json()}).then(function(d){
if(!d.enabled){verEl.textContent='v'+d.cur;updEl.style.display='none';return}
latest=d.latest;
if(d.avail){verEl.innerHTML='v'+d.cur+' → update to '+d.latest;updEl.textContent='Update now';updEl.style.display='';updEl.style.color='var(--accent)'}
else if(d.err){verEl.textContent='v'+d.cur+' (check failed: '+d.err+')';updEl.style.display='none'}
else{verEl.textContent='v'+d.cur+' · up to date';updEl.style.display='none'}
}).catch(function(){verEl.textContent='version check failed'})}
function doUpd(){if(!latest){checkUpd();return}
if(!confirm('Update to '+latest+'? The device will reboot.'))return;
showToast('Updating… do not power off');
fetch('/update',{method:'POST'}).then(pollBack).catch(pollBack)}
function pollBack(){verEl.textContent='Flashing… reconnecting (~1 min)';updEl.style.display='none';setOnline(false);
var t=setInterval(function(){fetch('/update/check').then(function(r){return r.json()}).then(function(d){
if(d.cur===latest){clearInterval(t);verEl.textContent='Updated to v'+d.cur;setOnline(true);showToast('Updated')}}).catch(function(){})},3000)}
checkUpd();
</script></body></html>
)HTML";

const char WIFI_PAGE[] PROGMEM = R"HTML(<!doctype html><html lang=en><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name=color-scheme content="light dark">
<meta name=theme-color content="#0b0c10" media="(prefers-color-scheme:dark)">
<meta name=theme-color content="#f5f6f8" media="(prefers-color-scheme:light)">
<title>Novy WiFi Setup</title><style>)HTML" NOVY_CSS R"HTML(
label{display:block;font-size:12px;text-transform:uppercase;letter-spacing:.06em;color:var(--muted);font-weight:700;margin:14px 2px 6px}
label:first-of-type{margin-top:0}
select,input{width:100%;font:inherit;font-size:16px;color:var(--fg);background:var(--bg);border:1px solid var(--line);border-radius:14px;padding:14px;min-height:52px}
select:focus,input:focus{outline:2px solid var(--accent);outline-offset:1px;border-color:var(--accent)}
.row{display:flex;gap:8px}.row>:first-child{flex:1}
.row>button{width:auto;min-width:56px;padding:14px}
.submit{margin-top:18px;font-size:17px;min-height:58px;width:100%}
.hint{color:var(--muted);font-size:13px;line-height:1.5;margin:16px 2px 0}
code{background:var(--bg);border:1px solid var(--line);border-radius:6px;padding:2px 6px}
</style></head><body><div class=wrap>
<header><h1>WiFi Setup</h1><a href=/ style="color:var(--muted);text-decoration:none;font-weight:600;font-size:13px">&larr; Back</a></header>
<div class=card><form method=POST action=/wifi>
<label for=ssidsel>Network</label>
<div class=row><select id=ssidsel onchange=pick()><option value="">Scanning…</option></select>
<button type=button onclick=scan() aria-label=Rescan title=Rescan><svg viewBox="0 0 24 24" fill=none stroke=currentColor stroke-width=2 stroke-linecap=round><path d="M21 12a9 9 0 1 1-2.6-6.3"/><path d="M21 4v5h-5"/></svg></button></div>
<input id=ssid name=ssid value="%SSID%" placeholder="Network name" autocomplete=off autocapitalize=off autocorrect=off spellcheck=false>
<label for=pass>Password</label>
<div class=row><input id=pass name=pass type=password placeholder="(unchanged)" autocomplete=off>
<button type=button id=eye onclick=tog()>Show</button></div>
<button class="primary submit" type=submit>Save &amp; Reboot</button>
</form>
<p class=hint>Pick your network, enter the password, and the hood reconnects to it. If this page didn't open automatically, browse to <code>http://192.168.4.1/</code>.</p>
</div></div><script>
var sel=document.getElementById('ssidsel'),ssid=document.getElementById('ssid'),pass=document.getElementById('pass');
function addOpt(v,t){var o=document.createElement('option');o.value=v;o.textContent=t;sel.appendChild(o)}
function pick(){if(sel.value==='__m'){ssid.value='';ssid.focus()}else if(sel.value){ssid.value=sel.value}}
function tog(){var e=document.getElementById('eye');if(pass.type==='password'){pass.type='text';e.textContent='Hide'}else{pass.type='password';e.textContent='Show'}}
function scan(){sel.innerHTML='';addOpt('','Scanning…');
fetch('/scan').then(function(r){return r.json()}).then(function(a){a.sort(function(x,y){return y.rssi-x.rssi});
var seen={};sel.innerHTML='';addOpt('','Choose a network…');
a.forEach(function(n){if(seen[n.ssid])return;seen[n.ssid]=1;addOpt(n.ssid,n.ssid+(n.lock?' 🔒':''))});
addOpt('__m','Other (type manually)…')}).catch(function(){sel.innerHTML='';addOpt('','Scan failed — type below')})}
scan();
</script></body></html>
)HTML";

const char MQTT_PAGE[] PROGMEM = R"HTML(<!doctype html><html lang=en><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name=color-scheme content="light dark">
<meta name=theme-color content="#0b0c10" media="(prefers-color-scheme:dark)">
<meta name=theme-color content="#f5f6f8" media="(prefers-color-scheme:light)">
<title>Novy MQTT Setup</title><style>)HTML" NOVY_CSS R"HTML(
label{display:block;font-size:12px;text-transform:uppercase;letter-spacing:.06em;color:var(--muted);font-weight:700;margin:14px 2px 6px}
input{width:100%;font:inherit;font-size:16px;color:var(--fg);background:var(--bg);border:1px solid var(--line);border-radius:14px;padding:14px;min-height:52px}
input:focus{outline:2px solid var(--accent);outline-offset:1px;border-color:var(--accent)}
.row{display:flex;gap:8px}.row>:first-child{flex:1}.row>button{width:auto;min-width:56px;padding:14px}
.tog{display:flex;align-items:center;justify-content:space-between;gap:12px;margin:0 2px}
.tog span{font-size:16px;font-weight:600}
.sw{position:relative;width:52px;height:30px;flex:0 0 auto}
.sw input{position:absolute;opacity:0;width:100%;height:100%;margin:0;cursor:pointer;min-height:0}
.sw i{position:absolute;inset:0;background:var(--line);border-radius:999px;transition:background .2s}
.sw i:before{content:"";position:absolute;width:24px;height:24px;left:3px;top:3px;background:#fff;border-radius:50%;transition:transform .2s;box-shadow:0 1px 3px rgba(0,0,0,.3)}
.sw input:checked+i{background:var(--ok)}.sw input:checked+i:before{transform:translateX(22px)}
.submit{margin-top:18px;font-size:17px;min-height:58px;width:100%}
.hint{color:var(--muted);font-size:13px;line-height:1.5;margin:16px 2px 0}
</style></head><body><div class=wrap>
<header><h1>MQTT Setup</h1><a href=/ style="color:var(--muted);text-decoration:none;font-weight:600;font-size:13px">&larr; Back</a></header>
<div class=card><form method=POST action=/mqtt>
<div class=tog><span>Enable MQTT</span><label class=sw><input type=checkbox name=en %EN%><i></i></label></div>
<label>Broker host / IP</label><input name=host value="%HOST%" placeholder="homeassistant.local" autocomplete=off autocapitalize=off spellcheck=false>
<label>Port</label><input name=port type=number value="%PORT%" placeholder="1883">
<label>Username</label><input name=user value="%USER%" placeholder="(optional)" autocomplete=off autocapitalize=off spellcheck=false>
<label>Password</label>
<div class=row><input id=pass name=pass type=password placeholder="(unchanged)" autocomplete=off>
<button type=button id=eye onclick=tog()>Show</button></div>
<label>Topic prefix</label><input name=prefix value="%PREFIX%" placeholder="novy-hood" autocomplete=off autocapitalize=off spellcheck=false>
<button class="primary submit" type=submit>Save &amp; Reboot</button>
</form>
<p class=hint>Buttons auto-appear in Home Assistant via MQTT discovery. Leave password blank to keep the current one. Topics: <code>&lt;prefix&gt;/novy-hood/button/&lt;id&gt;/set</code>.</p>
</div></div><script>
function tog(){var p=document.getElementById('pass'),e=document.getElementById('eye');if(p.type==='password'){p.type='text';e.textContent='Hide'}else{p.type='password';e.textContent='Show'}}
</script></body></html>
)HTML";

void initRadio() {
  ELECHOUSE_cc1101.setSpiPin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CSN);
  ELECHOUSE_cc1101.setGDO0(PIN_GDO0);
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setCCMode(0);       // async serial OOK on GDO0
  ELECHOUSE_cc1101.setModulation(2);   // ASK/OOK
  ELECHOUSE_cc1101.setMHZ(433.92);
  ELECHOUSE_cc1101.setPA(12);
  ELECHOUSE_cc1101.SetTx();
  transmitter.enableTransmit(TRANSMIT_433MHZ_PIN);
  transmitter.setProtocol(12);         // renedis values
  transmitter.setPulseLength(350);
}

void sendRFCommand(ButtonCommand cmd, const char* label, int channelIndex = 0) {
  ELECHOUSE_cc1101.SetTx();
  transmitter.enableTransmit(TRANSMIT_433MHZ_PIN);
  transmitter.setProtocol(12);
  transmitter.setPulseLength(350);

  String command;
  int repeatCount = 3;
  switch (cmd) {
    case CMD_LIGHT:  command = NOVY_COMMAND_LIGHT; repeatCount = 2; break;
    case CMD_POWER:  command = NOVY_COMMAND_POWER; break;
    case CMD_PLUS:   command = NOVY_COMMAND_PLUS;  break;
    case CMD_MINUS:  command = NOVY_COMMAND_MINUS; break;
    case CMD_NOVY:   command = NOVY_COMMAND_NOVY;  break;
  }

  String rfCode = NOVY_DEVICE_CODE[channelIndex] + NOVY_PREFIX + command;
  for (int i = 0; i < repeatCount; i++) { transmitter.send(rfCode.c_str()); delay(50); }
  logMsg(String(label) + " → RF " + rfCode + " ×" + repeatCount);
}

void route(const char* path, ButtonCommand cmd, const char* label) {
  server.on(path, HTTP_GET, [cmd, label]() {
    sendRFCommand(cmd, label);                 // logs the click, then transmits
    server.send(200, "text/html", logHtml());  // return fresh log -> instant UI update
  });
}

// WiFi setup page with the currently-configured SSID prefilled (blank password = unchanged).
String wifiPageHtml() {
  String h = FPSTR(WIFI_PAGE);
  h.replace("%SSID%", staSSID);
  return h;
}

// ===================== MQTT (PubSubClient + Home Assistant discovery) =====================
String mqttBase() { return mqttPrefix + "/" + HOSTNAME; }              // e.g. ESP/novy-hood
String mqttStatusTopic() { return mqttBase() + "/status"; }
String mqttCmdTopic(const char* id) { return mqttBase() + "/button/" + id + "/set"; }

// Incoming command: topic = <prefix>/<hostname>/button/<id>/set -> fire the matching RF button.
void mqttCallback(char* topic, byte* payload, unsigned int len) {
  String t(topic);
  for (int i = 0; i < MQTT_BTN_N; i++) {
    if (t == mqttCmdTopic(MQTT_BTNS[i].id)) {
      sendRFCommand(MQTT_BTNS[i].cmd, MQTT_BTNS[i].name);
      return;
    }
  }
}

// Publish one retained HA discovery config per button so they auto-appear in Home Assistant.
void mqttPublishDiscovery() {
  String avail = mqttStatusTopic();
  for (int i = 0; i < MQTT_BTN_N; i++) {
    const MqttBtn& b = MQTT_BTNS[i];
    String topic = String("homeassistant/button/") + HOSTNAME + "/" + b.id + "/config";
    String payload = String("{")
      + "\"name\":\"" + b.name + "\","
      + "\"uniq_id\":\"" + HOSTNAME + "_" + b.id + "\","
      + "\"icon\":\"" + b.icon + "\","
      + "\"cmd_t\":\"" + mqttCmdTopic(b.id) + "\","
      + "\"avty_t\":\"" + avail + "\","
      + "\"pl_avail\":\"online\",\"pl_not_avail\":\"offline\","
      + "\"dev\":{\"ids\":[\"" + HOSTNAME + "\"],\"name\":\"" + HOSTNAME
        + " Novy Controller\",\"mdl\":\"Novy RF Controller\",\"mf\":\"renedis\"}"
      + "}";
    mqtt.publish(topic.c_str(), (const uint8_t*)payload.c_str(), payload.length(), true);
  }
  logMsg("MQTT: HA discovery published");
}

bool mqttConnect() {
  mqtt.setServer(mqttHost.c_str(), mqttPort);
  mqtt.setBufferSize(1024);                       // discovery payloads exceed the 256B default
  mqtt.setCallback(mqttCallback);
  String st = mqttStatusTopic();
  // Connect with Last-Will = retained "offline" so HA marks it unavailable on disconnect.
  bool ok = mqtt.connect(MQTT_CLIENT_ID, mqttUser.c_str(), mqttPass.c_str(),
                         st.c_str(), 0, true, "offline");
  if (!ok) { Serial.printf("MQTT connect failed, state=%d\n", mqtt.state()); return false; }
  mqtt.publish(st.c_str(), "online", true);       // retained availability
  for (int i = 0; i < MQTT_BTN_N; i++) mqtt.subscribe(mqttCmdTopic(MQTT_BTNS[i].id).c_str());
  mqttPublishDiscovery();
  logMsg("MQTT connected: " + mqttHost + ":" + mqttPort);
  return true;
}

// Non-blocking: service MQTT, retry connection every 5s. Only when WiFi-STA up & not in portal.
void mqttLoop() {
  if (!mqttEnabled || portalActive || WiFi.status() != WL_CONNECTED || mqttHost.length() == 0) return;
  if (mqtt.connected()) { mqtt.loop(); return; }
  static unsigned long lastTry = 0;
  if (millis() - lastTry < 5000) return;
  lastTry = millis();
  mqttConnect();
}

// ===================== Pull-OTA self-update from GitHub release =====================
// All assets live on the rolling "latest" release:
//   https://github.com/<owner>/<repo>/releases/latest/download/<asset>
String releaseBase() { return String("https://github.com/") + FW_UPDATE_REPO + "/releases/latest/download/"; }

// Fetch the published version marker (version.txt). Returns "" on failure (sets g_updateError).
String fetchLatestVersion() {
  g_updateError = "";
  if (strlen(FW_UPDATE_REPO) == 0)     { g_updateError = "update disabled"; return ""; }
  if (WiFi.status() != WL_CONNECTED)   { g_updateError = "wifi down";       return ""; }

  WiFiClientSecure client;
  client.setInsecure();                                  // encrypted, server identity NOT verified
  HTTPClient http;
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS); // github.com -> asset CDN (cross-host)
  http.setConnectTimeout(8000);
  http.setTimeout(8000);
  if (!http.begin(client, releaseBase() + "version.txt")) { g_updateError = "begin failed"; return ""; }
  int code = http.GET();
  String body;
  if (code == HTTP_CODE_OK) { body = http.getString(); body.trim(); }
  else                      { g_updateError = "HTTP " + String(code); }
  http.end();
  return (code == HTTP_CODE_OK) ? body : String("");
}

// Blocking: download novy-hood.ota.bin over HTTPS and flash it; reboots into it on success.
void doHttpUpdate() {
  if (strlen(FW_UPDATE_REPO) == 0)   { logMsg("OTA: disabled (no repo)"); return; }
  if (WiFi.status() != WL_CONNECTED) { logMsg("OTA: wifi down"); return; }

  logMsg("OTA: downloading update...");
  WiFiClientSecure client;
  client.setInsecure();
  httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  httpUpdate.rebootOnUpdate(true);

  t_httpUpdate_return ret = httpUpdate.update(client, releaseBase() + "novy-hood.ota.bin", "");
  switch (ret) {
    case HTTP_UPDATE_OK:         logMsg("OTA: OK, rebooting"); break;   // typically never returns
    case HTTP_UPDATE_NO_UPDATES: g_updateError = "no update";  logMsg("OTA: no update"); break;
    case HTTP_UPDATE_FAILED:
      g_updateError = String("err ") + httpUpdate.getLastError() + " " + httpUpdate.getLastErrorString();
      g_updateError.replace("\"", "'");
      logMsg("OTA FAILED: " + g_updateError);
      break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println("\n=== Novy hood WiFi controller ===");

  // Hold the BOOT button (GPIO9) during reset to force the setup portal even when
  // valid credentials are saved — the recovery path for a stranded headless device.
  pinMode(SETUP_BTN_PIN, INPUT_PULLUP);
  delay(30);
  bool forcePortal = (digitalRead(SETUP_BTN_PIN) == LOW);

  WiFi.onEvent([](WiFiEvent_t e, WiFiEventInfo_t info) {
    if (e == ARDUINO_EVENT_WIFI_STA_DISCONNECTED)
      Serial.printf("  [wifi] disconnected, reason=%d\n", info.wifi_sta_disconnected.reason);
  });

  loadCreds();
  loadMqtt();

  bool connected = false;
  if (forcePortal)               Serial.println("BOOT held -> forcing setup portal");
  else if (staSSID.length() == 0) Serial.println("No saved WiFi -> setup portal");
  else                            connected = connectSTA(20000);

  if (connected) {
    Serial.print("WiFi connected. IP ADDRESS: ");
    Serial.println(WiFi.localIP());
    logMsg("IP: " + WiFi.localIP().toString());
    if (MDNS.begin(HOSTNAME.c_str()))
      Serial.printf("mDNS: http://%s.local\n", HOSTNAME.c_str());
  } else {
    if (!forcePortal && staSSID.length())
      Serial.printf("!! WiFi '%s' failed -> starting setup portal\n", staSSID.c_str());
    startPortal();
  }

  // Radio is brought up AFTER WiFi so the CC1101 is idle during association.
  initRadio();
  Serial.println(ELECHOUSE_cc1101.getCC1101() ? "CC1101 OK" : "!! CC1101 NOT detected");

  ArduinoOTA.setHostname(HOSTNAME.c_str());
  ArduinoOTA.setPassword(OTAPASSWORD.c_str());
  ArduinoOTA.begin();

  server.on("/", HTTP_GET, []() {
    String html = FPSTR(PAGE);
    html.replace("%LOG%", logHtml());
    html.replace("%FW_VERSION%", FW_VER);
    server.send(200, "text/html", html);
  });
  server.on("/log", HTTP_GET, []() { server.send(200, "text/html", logHtml()); });
  server.on("/reset", HTTP_GET, []() { server.send(200, "text/plain", "rebooting"); delay(300); ESP.restart(); });
  route("/togglePower", CMD_POWER, "Power");
  route("/togglePlus",  CMD_PLUS,  "Faster");
  route("/toggleMinus", CMD_MINUS, "Slower");
  route("/toggleNovy",  CMD_NOVY,  "Novy (auto)");
  route("/toggleLight", CMD_LIGHT, "Light");

  // ---- WiFi provisioning endpoints ----
  server.on("/wifi", HTTP_GET, []() { server.send(200, "text/html", wifiPageHtml()); });
  server.on("/wifi", HTTP_POST, []() {
    String ns = server.arg("ssid");
    // Blank password keeps the current one (lets you re-save the same network unchanged).
    String np = server.hasArg("pass") && server.arg("pass").length() ? server.arg("pass") : staPass;
    if (ns.length() == 0) { server.send(400, "text/plain", "SSID required"); return; }
    saveCreds(ns, np);
    logMsg("WiFi saved: " + ns + " -> rebooting");
    server.send(200, "text/html",
      "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
      "<meta http-equiv=refresh content='8;url=/'>"
      "<body style='font-family:sans-serif;background:#0b0c10;color:#eee;text-align:center;padding:40px'>"
      "<h2>Saved \xE2\x9C\x93</h2><p>Joining <b>" + ns + "</b>\xE2\x80\xA6 the device is rebooting.</p>"
      "<p style='opacity:.6;font-size:14px'>Reconnect your phone/computer to that network, then reopen this page.</p></body>");
    delay(400); ESP.restart();
  });
  server.on("/forget", HTTP_GET, []() {
    forgetCreds();
    server.send(200, "text/plain", "Credentials cleared. Rebooting into setup portal.");
    delay(400); ESP.restart();
  });
  // MQTT settings page (prefilled from current values; password shown blank).
  server.on("/mqtt", HTTP_GET, []() {
    String h = FPSTR(MQTT_PAGE);
    h.replace("%EN%", mqttEnabled ? "checked" : "");
    h.replace("%HOST%", mqttHost);
    h.replace("%PORT%", String(mqttPort));
    h.replace("%USER%", mqttUser);
    h.replace("%PREFIX%", mqttPrefix);
    server.send(200, "text/html", h);
  });
  server.on("/mqtt", HTTP_POST, []() {
    bool en = server.hasArg("en");
    String host = server.arg("host");
    uint16_t port = server.arg("port").toInt(); if (port == 0) port = 1883;
    String user = server.arg("user");
    String pass = server.hasArg("pass") && server.arg("pass").length() ? server.arg("pass") : mqttPass; // blank keeps current
    String prefix = server.arg("prefix"); if (prefix.length() == 0) prefix = "novy-hood";
    saveMqtt(en, host, port, user, pass, prefix);
    logMsg("MQTT settings saved -> rebooting");
    server.send(200, "text/html",
      "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
      "<meta http-equiv=refresh content='6;url=/'>"
      "<body style='font-family:sans-serif;background:#0b0c10;color:#eee;text-align:center;padding:40px'>"
      "<h2>Saved \xE2\x9C\x93</h2><p>MQTT settings stored. Rebooting\xE2\x80\xA6</p></body>");
    delay(400); ESP.restart();
  });
  // Network scan for the setup dropdown -> JSON: [{"ssid":"..","rssi":-55,"lock":true}, ...]
  server.on("/scan", HTTP_GET, []() {
    int n = WiFi.scanNetworks();
    String j = "[";
    for (int i = 0; i < n; i++) {
      String s = WiFi.SSID(i);
      if (s.length() == 0) continue;                 // skip hidden
      s.replace("\\", "\\\\"); s.replace("\"", "\\\""); // JSON-escape
      if (j.length() > 1) j += ",";
      j += "{\"ssid\":\"" + s + "\",\"rssi\":" + WiFi.RSSI(i) +
           ",\"lock\":" + (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true") + "}";
    }
    j += "]";
    WiFi.scanDelete();
    server.send(200, "application/json", j);
  });

  // Firmware self-update: report current vs latest version as JSON for the UI.
  server.on("/update/check", HTTP_GET, []() {
    g_latestVersion = fetchLatestVersion();              // blocking ≤8s (like /scan)
    bool enabled = strlen(FW_UPDATE_REPO) > 0;
    bool avail   = enabled && g_latestVersion.length() && g_latestVersion != String(FW_VER);
    String j = String("{\"cur\":\"") + FW_VER + "\",\"latest\":\"" + g_latestVersion +
               "\",\"enabled\":" + (enabled ? "true" : "false") +
               ",\"avail\":" + (avail ? "true" : "false") +
               ",\"err\":\"" + g_updateError + "\"}";
    server.send(200, "application/json", j);
  });
  // Trigger the update; respond first, flash later (deferred in loop()). POST so no stray GET flashes.
  server.on("/update", HTTP_POST, []() {
    if (strlen(FW_UPDATE_REPO) == 0) { server.send(403, "text/plain", "disabled"); return; }
    g_doUpdate = true;
    server.send(200, "text/plain", "updating");
  });

  // Captive portal: in setup mode, any unknown URL returns the setup page so iOS/Android
  // auto-open it. When connected normally, unknown URLs are a plain 404.
  server.onNotFound([]() {
    if (portalActive) server.send(200, "text/html", wifiPageHtml());
    else              server.send(404, "text/plain", "not found");
  });
  server.begin();

  logMsg("ready");
}

void loop() {
  if (g_doUpdate) { g_doUpdate = false; doHttpUpdate(); }  // deferred pull-OTA (response already sent)
  ArduinoOTA.handle();
  if (portalActive) dnsServer.processNextRequest();
  server.handleClient();
  mqttLoop();

  // Race-free status heartbeat: prints WiFi state + IP every 3s.
  static unsigned long last = 0;
  if (millis() - last >= 3000) {
    last = millis();
    if (portalActive)
      Serial.printf("[hb] SETUP PORTAL '%s' active -> http://%s/\n",
                    AP_SSID, WiFi.softAPIP().toString().c_str());
    else if (WiFi.status() == WL_CONNECTED)
      Serial.printf("[hb] WiFi OK  IP ADDRESS = %s  (http://%s/)\n",
                    WiFi.localIP().toString().c_str(), WiFi.localIP().toString().c_str());
    else
      Serial.printf("[hb] WiFi status=%d (not connected)\n", WiFi.status());
  }
}
