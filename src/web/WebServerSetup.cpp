#include "WebServerSetup.h"
#include "DebugLogger.h"
#include "ConfigStore.h"
#include <ArduinoJson.h>

namespace {
constexpr unsigned long kSetupConnectTimeoutMs = 45000UL;
constexpr uint32_t kPostConnectRestartDelayMs = 4000U;

struct SetupConnectState {
  bool connectRequested = false;
  unsigned long connectStartedMs = 0;
  bool restartArmed = false;
};

SetupConnectState gSetupConnectState;

bool isClientInSetupSubnet(AsyncWebServerRequest* request) {
  if (request == nullptr || request->client() == nullptr) {
    return false;
  }

  const IPAddress remote = request->client()->remoteIP();
  const IPAddress apIp = WiFi.softAPIP();
  const IPAddress apMask = WiFi.softAPSubnetMask();

  const uint32_t remoteRaw = static_cast<uint32_t>(remote);
  const uint32_t apIpRaw = static_cast<uint32_t>(apIp);
  const uint32_t apMaskRaw = static_cast<uint32_t>(apMask);

  return (remoteRaw & apMaskRaw) == (apIpRaw & apMaskRaw);
}

bool isClientInStationSubnet(AsyncWebServerRequest* request) {
  if (request == nullptr || request->client() == nullptr) {
    return false;
  }

  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  const IPAddress remote = request->client()->remoteIP();
  const IPAddress staIp = WiFi.localIP();
  const IPAddress staMask = WiFi.subnetMask();

  const uint32_t remoteRaw = static_cast<uint32_t>(remote);
  const uint32_t staIpRaw = static_cast<uint32_t>(staIp);
  const uint32_t staMaskRaw = static_cast<uint32_t>(staMask);

  return (remoteRaw & staMaskRaw) == (staIpRaw & staMaskRaw);
}

bool requireSetupSubnet(AsyncWebServerRequest* request) {
  if (isClientInSetupSubnet(request) || isClientInStationSubnet(request)) {
    return true;
  }

  const char* remoteIp = "unknown";
  if (request != nullptr && request->client() != nullptr) {
    remoteIp = request->client()->remoteIP().toString().c_str();
  }

  DebugLogger::getInstance().log(
    LogLevel::WARN,
    std::string("Blocked setup request from ") + remoteIp
  );

  const String apIp = WiFi.softAPIP().toString();
  const String staIp = WiFi.localIP().toString();
  String message = "Forbidden\n";
  message += "Verbinde dich mit dem Setup-AP 'Level-Control-Setup' oder nutze die STA-IP im selben Subnetz.\n";
  message += "AP-IP: " + apIp + "\n";
  message += "STA-IP: " + staIp + "\n";
  request->send(403, "text/plain", message);
  return false;
}
}

WebServerSetup& WebServerSetup::getInstance() {
    static WebServerSetup instance;
    return instance;
}

WebServerSetup::WebServerSetup() : server_(80) {}

void WebServerSetup::init() {
    setupRoutes();
}

void WebServerSetup::start() {
    server_.begin();
    DebugLogger::getInstance().log(LogLevel::INFO, "WebServerSetup started");
}

void WebServerSetup::setupRoutes() {
    server_.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!requireSetupSubnet(request)) {
      return;
    }

        static const char HTML[] PROGMEM = R"HTMLEOF(
<!DOCTYPE html><html lang="de">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Level-Control – WiFi Setup</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:'Trebuchet MS','Segoe UI',sans-serif;background:radial-gradient(circle at 20% 0%,#0f2140 0%,#0b1630 46%,#070d1f 100%);min-height:100vh;color:#e8f0ff;padding:16px}
.card{background:rgba(9,20,42,.72);border:1px solid rgba(112,146,198,.25);border-radius:16px;padding:20px;max-width:480px;margin:0 auto;box-shadow:0 12px 30px rgba(0,6,20,.45)}
.brand{display:flex;align-items:center;gap:10px;margin-bottom:20px}
.brand-icon{width:40px;height:40px;border-radius:12px;display:inline-flex;align-items:center;justify-content:center;background:linear-gradient(150deg,rgba(98,184,221,.2) 0%,rgba(98,184,221,.05) 100%);border:1px solid rgba(112,146,198,.25);color:#62b8dd;flex-shrink:0}
.brand-icon svg{width:20px;height:20px;fill:currentColor}
h1{font-size:1.35rem;color:#e8f0ff;letter-spacing:.6px;text-transform:uppercase;font-weight:900}
.brand-sub{margin-top:3px;color:#a8bddf;font-size:.8rem;letter-spacing:.16em;text-transform:uppercase}
h2{font-size:1rem;font-weight:700;letter-spacing:.04em;text-transform:uppercase;color:#a8bddf;margin-bottom:16px}
.field-row{display:flex;align-items:center;gap:12px;margin-bottom:12px}
.field-row span{min-width:90px;font-size:.9rem;color:#a8bddf;font-weight:700}
.input-wrap{flex:1;position:relative;display:flex;gap:8px}
input[type=text],input[type=password],select{flex:1;padding:7px 10px;border-radius:8px;border:1px solid rgba(112,146,198,.25);background:rgba(8,18,36,.8);color:#e8f0ff;font-size:.9rem;min-width:0}
input:focus,select:focus{outline:none;border-color:#62b8dd}
.icon-btn{padding:6px 8px;border-radius:8px;border:1px solid rgba(112,146,198,.25);background:rgba(28,52,93,.8);color:#a8bddf;cursor:pointer;display:inline-flex;align-items:center;justify-content:center;flex-shrink:0}
.icon-btn svg{width:16px;height:16px;fill:currentColor}
.icon-btn:hover{background:#2c74bb;color:#fff}
.scan-list{margin-bottom:12px}
.scan-list select{width:100%}
.save-btn{margin-top:8px;width:100%;padding:10px;border-radius:8px;border:none;background:#2c74bb;color:#fff;font-size:.95rem;font-weight:700;letter-spacing:.04em;cursor:pointer}
.save-btn:hover{background:#1a5ea8}
.helper{margin-top:8px;font-size:.8rem;color:#a8bddf;opacity:.7}
#msg{margin-top:12px;font-size:.85rem;padding:8px 10px;border-radius:8px;display:none}
#msg.ok{background:rgba(55,180,100,.18);border:1px solid rgba(55,180,100,.4);color:#76e0a0;display:block}
#msg.err{background:rgba(220,60,60,.18);border:1px solid rgba(220,60,60,.4);color:#f08080;display:block}
#handover{margin-top:12px;padding:10px;border-radius:8px;border:1px solid rgba(112,146,198,.25);background:rgba(8,18,36,.8);display:none}
#handover h3{font-size:.95rem;margin-bottom:6px;color:#e8f0ff}
#handover p{font-size:.85rem;line-height:1.4;color:#c9d9f2}
#handover .count{display:inline-block;margin-top:6px;font-weight:700;color:#76e0a0}
</style>
</head>
<body>
<div class="card">
  <div class="brand">
    <span class="brand-icon"><svg viewBox="0 0 24 24"><path d="M6.25 12a1.75 1.75 0 1 1 3.5 0 1.75 1.75 0 0 1-3.5 0Zm5.2 0a.95.95 0 0 1 .95-.95A5.6 5.6 0 0 0 18 5.45a.95.95 0 1 1 1.9 0 7.5 7.5 0 0 1-7.5 7.5.95.95 0 0 1-.95-.95Zm.95 4.55a.95.95 0 0 1 0-1.9A9.2 9.2 0 0 0 21.6 5.45a.95.95 0 1 1 1.9 0c0 6.04-4.91 10.95-10.95 10.95a.95.95 0 0 1-.15 0Zm0-8.95a.95.95 0 1 1 0-1.9 1.7 1.7 0 0 0 1.7-1.7.95.95 0 1 1 1.9 0 3.6 3.6 0 0 1-3.6 3.6Z"/></svg></span>
    <div>
      <h1>Level-Control</h1>
      <p class="brand-sub">WiFi Setup</p>
    </div>
  </div>
  <h2>WiFi</h2>
  <div class="field-row">
    <span>SSID:</span>
    <div class="input-wrap">
      <input type="text" id="ssid" name="ssid" autocomplete="off">
      <button class="icon-btn" type="button" onclick="scanWifi(this)" title="WiFi-Netze suchen">
        <svg viewBox="0 0 24 24"><path d="M10 4a6 6 0 1 0 3.87 10.58l4.27 4.28 1.42-1.42-4.28-4.27A6 6 0 0 0 10 4zm0 2a4 4 0 1 1 0 8 4 4 0 0 1 0-8z"/></svg>
      </button>
    </div>
  </div>
  <div id="scanResult" class="scan-list" style="display:none">
    <div class="field-row">
      <span>Gefundene Netze:</span>
      <select id="netList" onchange="selectNet(this.value)"><option value="">Bitte auswählen</option></select>
    </div>
  </div>
  <div class="field-row">
    <span>Passwort:</span>
    <div class="input-wrap">
      <input type="password" id="pw" name="password" autocomplete="new-password">
      <button class="icon-btn" type="button" onclick="togglePw()" title="Passwort anzeigen">
        <svg id="eyeIcon" viewBox="0 0 24 24"><path d="M12 4.5C7 4.5 2.73 7.61 1 12c1.73 4.39 6 7.5 11 7.5s9.27-3.11 11-7.5c-1.73-4.39-6-7.5-11-7.5zm0 12.5a5 5 0 1 1 0-10 5 5 0 0 1 0 10zm0-8a3 3 0 1 0 0 6 3 3 0 0 0 0-6z"/></svg>
      </button>
    </div>
  </div>
  <div class="field-row">
    <span>Gerätename:</span>
    <div class="input-wrap">
      <input type="text" id="deviceName" name="deviceName" autocomplete="off" placeholder="level-control">
    </div>
  </div>
  <button class="save-btn" onclick="saveConfig()">Speichern &amp; Neustart</button>
  <p class="helper">Nach dem Speichern startet der ESP neu und verbindet sich mit dem angegebenen Netzwerk.</p>
  <div id="msg"></div>
  <div id="handover"></div>
</div>
<script>
var connectPollTimer=null;
var redirectCountdownTimer=null;
var redirectTargetIp='';
var redirectTargetHostname='';
var redirectSecondsLeft=15;
var redirectRetryCount=0;

function togglePw(){
  var i=document.getElementById('pw');
  i.type=i.type==='password'?'text':'password';
}
function selectNet(v){if(v)document.getElementById('ssid').value=v;}
function scanWifi(btn){
  if(!btn){return;}
  btn.disabled=true;
  fetch('/scan',{method:'POST'}).then(r=>r.json()).then(nets=>{
    var sel=document.getElementById('netList');
    sel.innerHTML='<option value="">Bitte auswählen</option>';
    nets.forEach(function(n){var o=document.createElement('option');o.value=n.ssid;o.textContent=n.ssid+' ('+n.rssi+' dBm)';sel.appendChild(o);});
    document.getElementById('scanResult').style.display=nets.length?'block':'none';
    if(!nets.length){showMsg('err','Keine WiFi-Netze gefunden.');}
  }).catch(function(){showMsg('err','WiFi-Scan fehlgeschlagen.');}).finally(function(){btn.disabled=false;});
}
function saveConfig(){
  var ssid=document.getElementById('ssid').value.trim();
  var pw=document.getElementById('pw').value;
  var deviceName=document.getElementById('deviceName').value.trim();
  if(!ssid){showMsg('err','Bitte eine SSID eingeben.');return;}
  var fd=new FormData();fd.append('ssid',ssid);fd.append('password',pw);fd.append('deviceName',deviceName);
  showMsg('ok','Speichere …');
  fetch('/save',{method:'POST',body:fd}).then(r=>r.json()).then(function(resp){
    if(!resp.ok){throw new Error('save_failed');}
    redirectTargetHostname=String(resp.dnsHostname||'').trim();
    if(resp.staticIp){
      redirectTargetIp=String(resp.staticIp).trim();
    }
    setSetupFormEnabled(false);
    showMsg('ok','WLAN-Einstellungen gespeichert.');
    startHandoverFlow();
    pollConnectStatus();
  }).catch(function(){showMsg('err','Fehler beim Speichern.');});
}

function setSetupFormEnabled(enabled){
  var controls=document.querySelectorAll('input,select,button');
  controls.forEach(function(ctrl){
    ctrl.disabled=!enabled;
  });
}

function startHandoverFlow(){
  if(!redirectTargetIp){redirectTargetIp='';}
  redirectSecondsLeft=15;
  redirectRetryCount=0;
  renderHandoverBox(false,false);
  if(redirectCountdownTimer){clearInterval(redirectCountdownTimer);}
  redirectCountdownTimer=setInterval(function(){
    redirectSecondsLeft-=1;
    if(redirectSecondsLeft<=0){
      clearInterval(redirectCountdownTimer);
      redirectCountdownTimer=null;
      tryRedirectToDevice();
      return;
    }
    renderHandoverBox(false,false);
  },1000);
}

function renderHandoverBox(isTimeout,isRedirecting){
  var box=document.getElementById('handover');
  if(!box){return;}
  box.style.display='block';

  if(isTimeout){
    box.innerHTML=''
      +'<h3>Verbindung konnte nicht aufgebaut werden</h3>'
      +'<p>Bitte verbinde dich erneut mit deinem WLAN und öffne die Geräteseite manuell.</p>';
    return;
  }

  if(isRedirecting){
    var manualTarget=redirectTargetIp ? redirectTargetIp : (redirectTargetHostname || 'wird noch ermittelt');
    box.innerHTML=''
      +'<h3>Weiterleitung läuft …</h3>'
      +'<p>Bitte stelle sicher, dass dein Laptop wieder mit dem WLAN verbunden ist. '
      +'Falls nichts passiert, öffne das Gerät manuell unter: '+manualTarget+'</p>';
    return;
  }

  var ipHint=redirectTargetIp?('Ermittelte Geräte-IP: '+redirectTargetIp+'. '):'';
  var hostHint=redirectTargetHostname?('Gerätename im Netzwerk: '+redirectTargetHostname+'. '):'';
  box.innerHTML=''
    +'<h3>Setup abgeschlossen</h3>'
    +'<p>Die WLAN-Einstellungen wurden gespeichert. Der Setup-Access-Point wird deaktiviert. '
    +'Bitte verbinde dich wieder mit deinem WLAN (normalerweise automatisch, sofern nicht manuell deaktiviert). '
    +ipHint+hostHint+'Die Seite leitet in <span class="count">'+redirectSecondsLeft+' Sekunden</span> auf das Gerät weiter.</p>';
}

function pollConnectStatus(){
  fetch('/connect-status',{cache:'no-store'}).then(r=>r.json()).then(function(status){
    if(status.connected && status.ip){
      redirectTargetIp=status.ip;
      renderHandoverBox(false,false);
    }
    if(status.timeout){
      showMsg('err','WLAN-Verbindung fehlgeschlagen. Bitte Zugangsdaten prüfen.');
      renderHandoverBox(true,false);
      if(connectPollTimer){clearTimeout(connectPollTimer);connectPollTimer=null;}
      return;
    }
    connectPollTimer=setTimeout(pollConnectStatus,1200);
  }).catch(function(){
    connectPollTimer=setTimeout(pollConnectStatus,1500);
  });
}

function tryRedirectToDevice(){
  renderHandoverBox(false,true);
  if(redirectTargetIp){
    window.location.href='http://'+redirectTargetIp+'/';
    return;
  }
  if(redirectTargetHostname){
    window.location.href='http://'+redirectTargetHostname+'/';
    return;
  }

  redirectRetryCount+=1;
  if(redirectRetryCount>=20){
    showMsg('err','Automatische Weiterleitung nicht möglich. Bitte Gerät manuell aufrufen.');
    renderHandoverBox(true,false);
    return;
  }

  // Die Zieladresse ist noch nicht bekannt (oder AP-Link bereits weg) -> retry.
  setTimeout(tryRedirectToDevice,2000);
}

function showMsg(cls,txt){
  var el=document.getElementById('msg');
  el.className=cls;el.textContent=txt;
}
</script>
</body></html>
)HTMLEOF";
  request->send(200, "text/html", HTML);
    });

    server_.on("/scan", HTTP_POST, [](AsyncWebServerRequest *request) {
      if (!requireSetupSubnet(request)) {
        return;
      }

        auto networks = WifiManager::getInstance().scanNetworks();
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        for (auto& n : networks) {
            JsonObject obj = arr.add<JsonObject>();
            obj["ssid"] = n.ssid;
            obj["rssi"] = n.rssi;
        }
        String json;
        serializeJson(doc, json);
        request->send(200, "application/json", json);
    });

    server_.on("/save", HTTP_POST, [](AsyncWebServerRequest *request) {
      if (!requireSetupSubnet(request)) {
        return;
      }

        std::string ssid = request->arg("ssid").c_str();
        std::string password = request->arg("password").c_str();
        std::string deviceName = request->arg("deviceName").c_str();
        if (ssid.empty()) {
          request->send(400, "application/json", "{\"ok\":false,\"error\":\"ssid_required\"}");
          return;
        }

        Config fullConfig;
        ConfigStore::getInstance().load(fullConfig);

        WifiConfig wifiConfig = fullConfig.wifi;
        wifiConfig.ssid = ssid;
        wifiConfig.password = password;
        if (!deviceName.empty()) {
          wifiConfig.deviceName = deviceName;
        }
        if (!WifiManager::getInstance().setConfig(wifiConfig, fullConfig.staticIp)) {
          request->send(500, "application/json", "{\"ok\":false,\"error\":\"config_save_failed\"}");
          return;
        }

        gSetupConnectState.connectRequested = true;
        gSetupConnectState.connectStartedMs = millis();
        gSetupConnectState.restartArmed = false;

        const bool connectStarted = WifiManager::getInstance().beginConnectAsync();

        JsonDocument doc;
        doc["ok"] = connectStarted;
        doc["connecting"] = connectStarted;
        doc["dnsHostname"] = WifiManager::getInstance().getDnsHostname();
        doc["staticIp"] = fullConfig.staticIp.ip;
        if (!connectStarted) {
          doc["error"] = "wifi_connect_start_failed";
          std::string json;
          serializeJson(doc, json);
          request->send(400, "application/json", json.c_str());
          return;
        }

        std::string json;
        serializeJson(doc, json);
        request->send(200, "application/json", json.c_str());
    });

    server_.on("/connect-status", HTTP_GET, [](AsyncWebServerRequest *request) {
      if (!requireSetupSubnet(request)) {
        return;
      }

      const bool wifiConnected = WiFi.status() == WL_CONNECTED;
      const String staIp = WiFi.localIP().toString();
      const bool timeout = gSetupConnectState.connectRequested
        && !wifiConnected
        && (millis() - gSetupConnectState.connectStartedMs) > kSetupConnectTimeoutMs;

      if (wifiConnected && !gSetupConnectState.restartArmed) {
        WifiManager::getInstance().scheduleRestart(kPostConnectRestartDelayMs);
        gSetupConnectState.restartArmed = true;
      }

      JsonDocument doc;
      doc["connectRequested"] = gSetupConnectState.connectRequested;
      doc["connecting"] = gSetupConnectState.connectRequested && !wifiConnected && !timeout;
      doc["connected"] = wifiConnected;
      doc["timeout"] = timeout;
      doc["ip"] = wifiConnected ? staIp.c_str() : "";
      doc["dnsHostname"] = WifiManager::getInstance().getDnsHostname();
      doc["restartScheduled"] = WifiManager::getInstance().isRestartScheduled();
      doc["restartInMs"] = WifiManager::getInstance().getRestartRemainingMs();

      std::string json;
      serializeJson(doc, json);
      request->send(200, "application/json", json.c_str());
    });

      // Setup-Modus robust machen: Browser rufen haeufig /index.html oder alte Pfade auf.
      // Statt "Not found" leiten wir GET-Pfade auf die Setup-Startseite um.
      server_.onNotFound([](AsyncWebServerRequest *request) {
        if (request->method() == HTTP_GET) {
          request->redirect("/");
          return;
        }
        request->send(404, "text/plain", "Not found");
      });
}

