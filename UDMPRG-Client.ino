#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <ESPmDNS.h>
#include <time.h>
#include <HTTPClient.h>
#include <Update.h>

#define EEPROM_SIZE 201
#define SSID_OFFSET 0
#define PASS_OFFSET 64
#define SERVERIP_OFFSET 128
#define CALLSIGN_OFFSET 160
#define PORT_OFFSET 192
#define BAUD_OFFSET 194
#define LOGLEVEL_OFFSET 198

#define LED_PIN 2

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

HardwareSerial RS232(1);
WiFiClient client;

unsigned long lastBlink = 0;
bool ledState = false;
unsigned long lastKeepalive = 0;
const unsigned long KEEPALIVE_INTERVAL = 10000;
unsigned long lastRS232 = 0;

unsigned long lastServerConnectTry = 0;
const unsigned long SERVER_CONNECT_INTERVAL = 5000;
bool serverConnected = false;

unsigned long lastRX = 0;
unsigned long lastTX = 0;
const unsigned long RS232_ACTIVE_TIME = 200;

WebServer server(80);

char wifiSsid[64] = "";
char wifiPass[64] = "";
char serverIp[32] = "";
char callsign[32] = "";
uint16_t serverPort = 2323;
uint32_t baudrate = 2400;
uint8_t logLevel = 1;
bool apActive = false;

#define MONITOR_BUF_SIZE 4096
String monitorBuf = "";
String rs232HexBuf = "";
String rs232AscBuf = "";

const char* ntpServer = "at.pool.ntp.org";
const long  gmtOffset_sec = 3600;
const int   daylightOffset_sec = 3600;

// OTA
String localVersion = "1.0.0"; // <-- Stelle sicher, dass du hier deine aktuelle Version pflegst!
bool otaCheckedThisSession = false;

void appendMonitor(const String& msg, const char* level = "INFO");
String getTimestamp();
void blinkLED();

void bootPrint(const String &msg) {
  static int line = 0;
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 8 * line++);
  display.print(msg);
  display.display();
  if (line >= 8) {
    display.clearDisplay();
    line = 0;
  }
  delay(300);
}

// ===== OTA Fortschrittsanzeige =====
void showOTAUpdateScreen(const char* text, float progress = -1) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(8, 13);
  display.print("Firmware-Update");
  display.setCursor(8, 25);
  display.print(text);
  if(progress >= 0.0) {
    // Fortschrittsbalken
    int barWidth = 112, barHeight = 10, barX = 8, barY = 40;
    display.drawRect(barX, barY, barWidth, barHeight, SSD1306_WHITE);
    int filled = (int)(barWidth * progress);
    if(filled > 0)
      display.fillRect(barX+1, barY+1, filled-2, barHeight-2, SSD1306_WHITE);
    // Prozent
    display.setCursor(48, 54);
    display.print(int(progress*100));
    display.print("%");
  }
  display.display();
}

// ===== OLED Funktionen =====
void drawWifiStrength(int strength) {
  int x = SCREEN_WIDTH - 22;
  int y = 0;
  display.drawRect(x, y+4, 2, 8, SSD1306_WHITE);
  for(int i=0;i<4;i++) {
    if(strength>i)
      display.fillRect(x+4+i*3, y+12-2*i, 2, 2+2*i, SSD1306_WHITE);
    else
      display.drawRect(x+4+i*3, y+12-2*i, 2, 2+2*i, SSD1306_WHITE);
  }
}

void drawRXTXRects() {
  int rect_width = 26;
  int rect_height = 16;
  int rect_y = 48;
  int rx_x = 16;
  int tx_x = 80;

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(rx_x+6, rect_y-10);
  display.print("GNN");
  display.setCursor(tx_x+6, rect_y-10);
  display.print("TX");

  if (millis() - lastRX < RS232_ACTIVE_TIME) {
    display.fillRect(rx_x, rect_y, rect_width, rect_height, SSD1306_WHITE);
    display.drawRect(rx_x, rect_y, rect_width, rect_height, SSD1306_BLACK);
  } else {
    display.drawRect(rx_x, rect_y, rect_width, rect_height, SSD1306_WHITE);
  }
  if (millis() - lastTX < RS232_ACTIVE_TIME) {
    display.fillRect(tx_x, rect_y, rect_width, rect_height, SSD1306_WHITE);
    display.drawRect(tx_x, rect_y, rect_width, rect_height, SSD1306_BLACK);
  } else {
    display.drawRect(tx_x, rect_y, rect_width, rect_height, SSD1306_WHITE);
  }
}

void updateOLED() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(0,0);
  display.print(callsign);
  int rssi = WiFi.RSSI();
  int strength = 0;
  if (WiFi.status() == WL_CONNECTED) {
    if (rssi > -55) strength = 4;
    else if (rssi > -65) strength = 3;
    else if (rssi > -75) strength = 2;
    else if (rssi > -85) strength = 1;
    else strength = 0;
  }
  drawWifiStrength(strength);
  display.drawLine(0, 20, SCREEN_WIDTH, 20, SSD1306_WHITE);
  display.setTextSize(1);
  const char* serverStatus = client.connected() ? "Client ONLINE" : "Client OFFLINE";
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(serverStatus, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, 22);
  display.print(serverStatus);
  display.drawLine(0, 32, SCREEN_WIDTH, 32, SSD1306_WHITE);
  drawRXTXRects();
  display.display();
}

// ========== OTA-Update mit Fortschrittsbalken ==========
void checkForUpdates() {
  appendMonitor("DEBUG: checkForUpdates() aufgerufen", "DEBUG");
  if (otaCheckedThisSession) return;
  otaCheckedThisSession = true;
  if(strlen(serverIp) == 0) return;
  String baseUrl = String("http://") + serverIp;
  if(serverPort != 80 && serverPort != 0) baseUrl += ":" + String(serverPort);
  String urlVersion = baseUrl + "/version.txt";
  String urlFirmware = baseUrl + "/firmware.bin";

  appendMonitor("OTA: Prüfe auf neue Firmware unter " + urlVersion, "INFO");
  HTTPClient http;
  http.begin(urlVersion);
  int httpCode = http.GET();
  if(httpCode == 200) {
    String remoteVersion = http.getString();
    remoteVersion.trim();
    appendMonitor("OTA: Server Version: " + remoteVersion + ", lokal: " + localVersion, "DEBUG");
    if(remoteVersion != localVersion) {
      appendMonitor("OTA: Neue Version gefunden (" + remoteVersion + "), Update wird geladen!", "INFO");
      showOTAUpdateScreen("Bitte NICHT abstecken", 0.0);

      // Download Firmware mit Fortschritt
      http.end();
      http.begin(urlFirmware);
      int resp = http.GET();
      if(resp == 200) {
        int contentLength = http.getSize();
        if(contentLength > 0) {
          WiFiClient * stream = http.getStreamPtr();
          bool canBegin = Update.begin(contentLength);
          if(canBegin) {
            uint8_t buff[512];
            int written = 0;
            int totalRead = 0;
            unsigned long lastDisplay = millis();
            while(http.connected() && totalRead < contentLength) {
              size_t avail = stream->available();
              if(avail) {
                int read = stream->readBytes(buff, ((avail > sizeof(buff)) ? sizeof(buff) : avail));
                Update.write(buff, read);
                totalRead += read;
                float progress = float(totalRead) / float(contentLength);
                if(millis() - lastDisplay > 100) {
                  showOTAUpdateScreen("Bitte NICHT abstecken", progress);
                  lastDisplay = millis();
                }
              }
              yield();
            }
            if(Update.end(true)) {
              showOTAUpdateScreen("Update abgeschlossen!", 1.0);
              appendMonitor("OTA-Update erfolgreich! Neustart...", "INFO");
              delay(2000);
              ESP.restart();
            } else {
              showOTAUpdateScreen("Fehler beim Update!", 0.0);
              appendMonitor(String("OTA fehlgeschlagen: ") + Update.getError(), "ERROR");
              delay(4000);
            }
          } else {
            showOTAUpdateScreen("Update Init Fehler!", 0.0);
            appendMonitor("OTA konnte nicht gestartet werden!", "ERROR");
            delay(4000);
          }
        } else {
          showOTAUpdateScreen("Leere Firmware!", 0.0);
          appendMonitor("OTA: Leere Firmwaredatei.", "ERROR");
          delay(4000);
        }
      } else {
        showOTAUpdateScreen("Download Fehler!", 0.0);
        appendMonitor("OTA: Fehler beim Firmware-Download: " + String(resp), "ERROR");
        delay(4000);
      }
      http.end();
      return;
    } else {
      appendMonitor("OTA: Firmware aktuell.", "INFO");
    }
  } else {
    appendMonitor("OTA: Fehler beim Versions-Check: " + String(httpCode), "WARNING");
  }
  http.end();
}

// ===========================
// ... (REST wie gehabt!)

void saveConfig() {
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < 64; ++i) EEPROM.write(SSID_OFFSET+i, wifiSsid[i]);
  for (int i = 0; i < 64; ++i) EEPROM.write(PASS_OFFSET+i, wifiPass[i]);
  for (int i = 0; i < 32; ++i) EEPROM.write(SERVERIP_OFFSET+i, serverIp[i]);
  for (int i = 0; i < 32; ++i) EEPROM.write(CALLSIGN_OFFSET+i, callsign[i]);
  EEPROM.write(PORT_OFFSET, (serverPort >> 8) & 0xFF);
  EEPROM.write(PORT_OFFSET+1, serverPort & 0xFF);
  EEPROM.write(BAUD_OFFSET, (baudrate >> 24) & 0xFF);
  EEPROM.write(BAUD_OFFSET+1, (baudrate >> 16) & 0xFF);
  EEPROM.write(BAUD_OFFSET+2, (baudrate >> 8) & 0xFF);
  EEPROM.write(BAUD_OFFSET+3, baudrate & 0xFF);
  EEPROM.write(LOGLEVEL_OFFSET, logLevel);
  EEPROM.commit();
}

void loadConfig() {
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < 64; ++i) wifiSsid[i] = EEPROM.read(SSID_OFFSET+i);
  wifiSsid[63] = 0;
  for (int i = 0; i < 64; ++i) wifiPass[i] = EEPROM.read(PASS_OFFSET+i);
  wifiPass[63] = 0;
  for (int i = 0; i < 32; ++i) serverIp[i] = EEPROM.read(SERVERIP_OFFSET+i);
  serverIp[31] = 0;
  for (int i = 0; i < 32; ++i) callsign[i] = EEPROM.read(CALLSIGN_OFFSET+i);
  callsign[31] = 0;
  serverPort = (EEPROM.read(PORT_OFFSET) << 8) | EEPROM.read(PORT_OFFSET+1);
  baudrate = ((uint32_t)EEPROM.read(BAUD_OFFSET) << 24)
           | ((uint32_t)EEPROM.read(BAUD_OFFSET+1) << 16)
           | ((uint32_t)EEPROM.read(BAUD_OFFSET+2) << 8)
           | ((uint32_t)EEPROM.read(BAUD_OFFSET+3));
  logLevel = EEPROM.read(LOGLEVEL_OFFSET);
  if(serverPort == 0xFFFF || serverPort == 0x0000) {
    serverPort = 2323;
    appendMonitor("Server Port war ungültig, auf 2323 gesetzt", "WARNING");
  }
  if(baudrate == 0xFFFFFFFF || baudrate == 0x00000000) {
    baudrate = 2400;
    appendMonitor("Baudrate war ungültig, auf 2400 gesetzt", "WARNING");
  }
  if(logLevel > 3) logLevel = 1;
  if(strlen(wifiSsid) == 0) appendMonitor("WLAN SSID ist leer!", "WARNING");
  if(strlen(serverIp) == 0) appendMonitor("Server IP ist leer!", "WARNING");
}

void handleRoot() {
  String html = R"=====(
<!DOCTYPE html>
<html>
  <head>
    <meta charset="UTF-8">
    <title>UDMPRIG-Client</title>
    <meta name="viewport" content="width=device-width, initial-scale=1.0"/>
    <link href="https://cdnjs.cloudflare.com/ajax/libs/materialize/1.0.0/css/materialize.min.css" rel="stylesheet">
    <style>
      body {padding:18px;}
      pre {font-size: 12px; background:#222; color:#eee; padding:10px;}
      .input-field label {color: #009688;}
      .tabs .tab a.active { color: #009688;}
      .tabs .tab a { color: #444;}
      .input-field {margin-bottom: 0;}
      .custom-row {margin-bottom: 20px;}
      .modal-content { text-align: center; }
      .modal .preloader-wrapper { margin: 30px auto; }
      .modal .errormsg { color: #b71c1c; font-weight: bold; font-size: 1.2em; }
      .log-debug { color: #90caf9 !important; }
      .log-info { color: #a5d6a7 !important; }
      .log-error { color: #ef9a9a !important; }
      .log-warn { color: #ffe082 !important; }
      .log-default { color: #eee !important; }
      .log-rs232 { color: #ffecb3 !important; font-family:monospace; }
    </style>
  </head>
  <body>
    <h4><i class="material-icons left">settings_ethernet</i>UDMPRIG-Client</h4>
    <ul id="tabs-swipe-demo" class="tabs">
      <li class="tab col s3"><a class="active" href="#config">Config</a></li>
      <li class="tab col s3"><a href="#monitor">Monitor</a></li>
    </ul>
    <div id="config" class="col s12">
      <form id="configform" action='/save' method='post'>
        <div class="input-field custom-row">
          <input id="ssid" name="ssid" type="text" maxlength="63" value=")=====";
  html += String(wifiSsid);
  html += R"=====(">
          <label for="ssid" class="active">WLAN SSID</label>
        </div>
        <div class="input-field custom-row">
          <input id="pass" name="pass" type="password" maxlength="63" value=")=====";
  html += String(wifiPass);
  html += R"=====(">
          <label for="pass" class="active">WLAN Passwort</label>
        </div>
        <div class="input-field custom-row">
          <input id="serverip" name="serverip" type="text" maxlength="31" value=")=====";
  html += String(serverIp);
  html += R"=====(">
          <label for="serverip" class="active">Server IP</label>
        </div>
        <div class="input-field custom-row">
          <input id="serverport" name="serverport" type="number" min="1" max="65535" value=")=====";
  html += String(serverPort);
  html += R"=====(">
          <label for="serverport" class="active">Server Port</label>
        </div>
        <div class="input-field custom-row">
          <input id="callsign" name="callsign" type="text" maxlength="31" value=")=====";
  html += String(callsign);
  html += R"=====(">
          <label for="callsign" class="active">Callsign</label>
        </div>
        <div class="input-field custom-row">
          <select id="baudrate" name="baudrate">
  )=====";
  uint32_t rates[] = {1200, 2400, 4800, 9600, 14400, 19200, 38400, 57600, 115200};
  for(int i=0;i<9;++i){
    html += "<option value='";
    html += String(rates[i]);
    html += "'";
    if(baudrate==rates[i]) html += " selected";
    html += ">";
    html += String(rates[i]);
    html += " Baud</option>";
  }
  html += R"=====(</select>
          <label for="baudrate">RS232 Baudrate</label>
        </div>
)=====";
  html += "<div class=\"input-field custom-row\">";
  html += "<select id=\"loglevel\" name=\"loglevel\">";
  html += "<option value=\"0\"";
  if(logLevel==0) html += " selected";
  html += ">Error</option>";
  html += "<option value=\"1\"";
  if(logLevel==1) html += " selected";
  html += ">Info</option>";
  html += "<option value=\"2\"";
  if(logLevel==2) html += " selected";
  html += ">Warning</option>";
  html += "<option value=\"3\"";
  if(logLevel==3) html += " selected";
  html += ">Debug</option>";
  html += "</select>";
  html += "<label for=\"loglevel\">Log Level</label>";
  html += "</div>";
  html += R"=====(
        <button class="btn waves-effect waves-light teal" type="submit" id="savebtn">Speichern
          <i class="material-icons right">save</i>
        </button>
      </form>
      <div class="section">
        (c) www.pukepals.com, 73 de AT1NAD
        <br><small>Auch erreichbar unter: <b>http://udmprig-client.local/</b></small>
      </div>
      <!-- Modal Structure -->
      <div id="restartModal" class="modal">
        <div class="modal-content">
          <h5>Client wird neu gestartet</h5>
          <div class="preloader-wrapper big active" id="spinner">
            <div class="spinner-layer spinner-blue-only">
              <div class="circle-clipper left">
                <div class="circle"></div>
              </div><div class="gap-patch">
                <div class="circle"></div>
              </div><div class="circle-clipper right">
                <div class="circle"></div>
              </div>
            </div>
          </div>
          <div id="restartMsg">
            <p>Bitte warten...</p>
          </div>
        </div>
      </div>
    </div>
    <div id="monitor" class="col s12">
      <h6>Serieller Monitor</h6>
      <pre id="monitorArea" style="height:350px;overflow:auto;"></pre>
      <button class="btn red" onclick="clearMonitor()">Leeren</button>
      <script>
        document.addEventListener('DOMContentLoaded', function() {
          var el = document.querySelectorAll('.tabs');
          M.Tabs.init(el, {});
          var selects = document.querySelectorAll('select');
          M.FormSelect.init(selects, {});
          var modals = document.querySelectorAll('.modal');
          M.Modal.init(modals, {});
        });
        function updateMonitor() {
          fetch('/monitor').then(r=>r.text()).then(t=>{
            let area = document.getElementById('monitorArea');
            // Neue Darstellung: Zeilen mit RS232-Daten im Debug-Loglevel farbig, alles andere wie gehabt
            let html = t.replace(/^\[([0-9:T\.\- ]+)\]\s+\[(\w+)\]\s*(.*)$/gm, function(_,ts,level,msg){
              if(level=="DEBUG_RS232") return `<span class="log-rs232">[${ts}] [DEBUG] ${msg}</span>`;
              let cls = "log-default";
              if(level=="DEBUG") cls="log-debug";
              else if(level=="INFO") cls="log-info";
              else if(level=="ERROR") cls="log-error";
              else if(level=="WARNING") cls="log-warn";
              return `<span class="${cls}">[${ts}] [${level}] ${msg}</span>`;
            });
            let areaDiv = document.getElementById('monitorArea');
            let atBottom = (areaDiv.scrollTop + areaDiv.clientHeight >= areaDiv.scrollHeight-2);
            areaDiv.innerHTML = html;
            if(atBottom) areaDiv.scrollTop = areaDiv.scrollHeight;
          });
        }
        setInterval(updateMonitor, 1000);
        function clearMonitor() {
          fetch('/monitor_clear').then(()=>updateMonitor());
        }
        window.onload = updateMonitor;
      </script>
      <script>
        // Zeige Modal & Poll bis Seite wieder online, max 1 Minute!
        document.addEventListener('DOMContentLoaded', function() {
          var form = document.getElementById('configform');
          if(form){
            form.onsubmit = function(e){
              var instance = M.Modal.getInstance(document.getElementById('restartModal'));
              instance.open();
              // Modal-Text und Spinner zurücksetzen
              document.getElementById('restartMsg').innerHTML = "<p>Bitte warten...</p>";
              document.getElementById('spinner').style.display = "";
              // Nach 2s Polling starten (damit POST und Reboot Zeit haben)
              setTimeout(function(){
                var start = Date.now();
                function pollOnline(){
                  fetch('/', {cache:'no-store'})
                    .then(r => {
                      if(r.ok) location.reload();
                      else setTimeout(pollOnline, 1000);
                    })
                    .catch(() => {
                      if(Date.now()-start < 60000) setTimeout(pollOnline, 1000);
                      else {
                        // Fehler anzeigen
                        document.getElementById('spinner').style.display = "none";
                        document.getElementById('restartMsg').innerHTML =
                          "<div class='errormsg'>Fehler: Client hat sich nicht mehr gemeldet oder eine neue IP Adresse bekommen.<br>Bitte aktualisieren.</div>";
                      }
                    });
                }
                pollOnline();
              }, 2000);
              return true; // Formular trotzdem absenden
            }
          }
        });
      </script>
    </div>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/materialize/1.0.0/js/materialize.min.js"></script>
    <link href="https://fonts.googleapis.com/icon?family=Material+Icons" rel="stylesheet">
  </body>
</html>
)=====";
  server.send(200, "text/html", html);
}

void handleSave() {
  if (server.hasArg("ssid")) strncpy(wifiSsid, server.arg("ssid").c_str(), 63);
  if (server.hasArg("pass")) strncpy(wifiPass, server.arg("pass").c_str(), 63);
  if (server.hasArg("serverip")) strncpy(serverIp, server.arg("serverip").c_str(), 31);
  if (server.hasArg("callsign")) strncpy(callsign, server.arg("callsign").c_str(), 31);
  if (server.hasArg("serverport")) serverPort = server.arg("serverport").toInt();
  if (server.hasArg("baudrate")) baudrate = server.arg("baudrate").toInt();
  if (server.hasArg("loglevel")) logLevel = server.arg("loglevel").toInt();
  wifiSsid[63]=0; wifiPass[63]=0; serverIp[31]=0; callsign[31]=0;
  saveConfig();
  appendMonitor("Konfiguration gespeichert. Neustart folgt.", "INFO");
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
  delay(500);
  ESP.restart();
}

void handleMonitor() {
  if(logLevel == 3 && (rs232HexBuf.length() > 0 || rs232AscBuf.length() > 0)) {
    // Im Debug-Modus RS232 Daten anzeigen (HEX und ASCII in einer Zeile)
    String out = "[";
    out += getTimestamp();
    out += "] [DEBUG_RS232] RS232 RX: HEX ";
    out += rs232HexBuf;
    out += " | ASCII ";
    out += rs232AscBuf;
    out += "\n";
    monitorBuf += out;
    if (monitorBuf.length() > MONITOR_BUF_SIZE) {
      monitorBuf = monitorBuf.substring(monitorBuf.length() - MONITOR_BUF_SIZE);
    }
    rs232HexBuf = "";
    rs232AscBuf = "";
  }
  server.send(200, "text/plain", monitorBuf);
}
void handleMonitorClear() {
  monitorBuf = "";
  rs232HexBuf = "";
  rs232AscBuf = "";
  appendMonitor("Monitor gelöscht", "INFO");
  server.send(200, "text/plain", "OK");
}

void startWebserver() {
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/monitor", handleMonitor);
  server.on("/monitor_clear", handleMonitorClear);
  server.begin();
}

void startConfigPortal() {
  WiFi.softAP("UDMPRIG-Client");
  IPAddress apip(192,168,4,1);
  WiFi.softAPConfig(apip, apip, IPAddress(255,255,255,0));
  apActive = true;
  startWebserver();
  appendMonitor("AccessPoint gestartet für Konfiguration", "INFO");
  while (WiFi.status() != WL_CONNECTED) {
    server.handleClient();
    blinkLED();
    delay(1);
  }
  WiFi.softAPdisconnect(true);
  appendMonitor("Konfigurationsmodus verlassen, Hotspot deaktiviert", "INFO");
  apActive = false;
}

bool tryConnectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSsid, wifiPass);
  appendMonitor("WLAN Verbindung wird aufgebaut...", "INFO");
  for (int i = 0; i < 40; ++i) {
    if (WiFi.status() == WL_CONNECTED) {
      appendMonitor("WLAN verbunden: " + WiFi.localIP().toString(), "INFO");
      return true;
    }
    blinkLED();
    delay(250);
  }
  appendMonitor("WLAN Verbindung fehlgeschlagen!", "ERROR");
  return false;
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED Fehler!");
    while (1) { delay(10); }
  }
  display.clearDisplay();
  display.display();
  bootPrint("Init Display ... OK");

  loadConfig();
  bootPrint("Load Config ... OK");

  if (baudrate == 0) {
    baudrate = 2400;
    appendMonitor("Baudrate war 0! Auf 2400 gesetzt.", "WARNING");
  }
  RS232.begin(baudrate, SERIAL_8N1, 16, 17);
  appendMonitor("RS232 initialisiert mit Baudrate " + String(baudrate), "INFO");
  bootPrint("Init RS232 ... OK");

  bootPrint("Verbinde WLAN ...");
  bool wifiOk = tryConnectWiFi();
  if (!wifiOk) {
    bootPrint("WLAN fehlgeschlagen!");
    appendMonitor("Starte Konfigurationsportal...", "WARNING");
    bootPrint("Starte Config-Portal!");
    startConfigPortal();
  }

  bootPrint("Starte Webserver ...");
  startWebserver();
  bootPrint("Webserver online");

  if (serverIp[0] == 0) {
    appendMonitor("Keine Server-IP konfiguriert! Starte Konfigurationsportal...", "WARNING");
    bootPrint("Keine Server-IP! Portal!");
    if (!apActive) startConfigPortal();
  }

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  appendMonitor("NTP initialisiert (at.pool.ntp.org, Europe/Vienna)", "INFO");
  bootPrint("Init NTP ... OK");

  if (!MDNS.begin("udmprig-client")) {
    appendMonitor("mDNS konnte nicht gestartet werden", "ERROR");
    bootPrint("mDNS ... FEHLER");
  } else {
    appendMonitor("mDNS gestartet als udmprig-client.local", "INFO");
    bootPrint("mDNS ... OK");
  }

  serverConnected = false;
  lastServerConnectTry = millis() - SERVER_CONNECT_INTERVAL;
  lastKeepalive = millis();
}

String getTimestamp() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &timeinfo);
    return String(buf);
  } else {
    unsigned long ms = millis();
    unsigned long s = ms / 1000;
    unsigned long m = s / 60;
    unsigned long h = m / 60;
    unsigned long d = h / 24;
    h = h % 24;
    m = m % 60;
    s = s % 60;
    ms = ms % 1000;
    char buf[32];
    snprintf(buf, sizeof(buf), "UP-%02luT%02lu:%02lu:%02lu.%03lu", d, h, m, s, ms);
    return String(buf);
  }
}

void appendMonitor(const String& msg, const char* level) {
  String line = "[";
  line += getTimestamp();
  line += "] [";
  line += level;
  line += "] ";
  line += msg;
  line += "\n";
  monitorBuf += line;
  if (monitorBuf.length() > MONITOR_BUF_SIZE) {
    monitorBuf = monitorBuf.substring(monitorBuf.length() - MONITOR_BUF_SIZE);
  }
}

void loop() {
  server.handleClient();

  static unsigned long lastOled = 0;
  if (millis() - lastOled > 50) {
    updateOLED();
    lastOled = millis();
  }

  if (!client.connected()) {
    serverConnected = false;
    if (millis() - lastServerConnectTry > SERVER_CONNECT_INTERVAL) {
      lastServerConnectTry = millis();
      appendMonitor("Versuche Verbindung zum Server...", "INFO");
      if (client.connect(serverIp, serverPort)) {
        client.setNoDelay(true);
        client.print(String(callsign) + "\n");
        appendMonitor("Callsign an Server gesendet: " + String(callsign), "DEBUG");
        unsigned long authStart = millis();
        String response = "";
        while (millis() - authStart < 2000) {
          while (client.available()) {
            char c = client.read();
            if (c == '\n' || c == '\r') goto gotresp;
            response += c;
          }
          delay(1);
        }
        gotresp:
        response.trim();
        if (response != "OK") {
          appendMonitor("Server lehnt Verbindung ab (" + response + ")", "ERROR");
          client.stop();
          serverConnected = false;
          delay(3000);
          return;
        }
        appendMonitor("Server verbunden und authentifiziert!", "INFO");
        serverConnected = true;
        checkForUpdates(); // OTA-Check!
      } else {
        appendMonitor("Kann nicht zum Server verbinden.", "WARNING");
      }
    }
    return;
  }

  serverConnected = true;

  if (WiFi.status() != WL_CONNECTED) {
    digitalWrite(LED_PIN, LOW);
    appendMonitor("WiFi Verbindung verloren!", "ERROR");
    return;
  } else if (!client.connected()) {
    blinkLED();
  } else {
    digitalWrite(LED_PIN, HIGH);
  }

  if (!RS232) {
    appendMonitor("RS232 nicht verfügbar! Überprüfe Hardware.", "ERROR");
  }

  uint8_t buf[256];
  int n = RS232.available();
  while (n > 0) {
    int readLen = n > (int)sizeof(buf) ? sizeof(buf) : n;
    int actual = RS232.readBytes(buf, readLen);
    if (client.connected() && actual > 0) {
      client.write(buf, actual);
      lastTX = millis();
      if(logLevel == 3) {
        for(int i=0;i<actual;i++) {
          char hexbuf[4];
          snprintf(hexbuf, sizeof(hexbuf), "%02X ", buf[i]);
          rs232HexBuf += hexbuf;
          if(buf[i] >= 32 && buf[i] <= 126) rs232AscBuf += (char)buf[i];
          else rs232AscBuf += '.';
        }
        if(rs232HexBuf.length() > 500) {
          rs232HexBuf = rs232HexBuf.substring(rs232HexBuf.length()-500);
          rs232AscBuf = rs232AscBuf.substring(rs232AscBuf.length()-166);
        }
      }
    }
    lastRS232 = millis();
    n -= actual;
  }

  n = client.available();
  while (n > 0) {
    int readLen = n > (int)sizeof(buf) ? sizeof(buf) : n;
    int actual = client.readBytes(buf, readLen);
    if (RS232 && actual > 0) {
      RS232.write(buf, actual);
      lastRX = millis();
      lastRS232 = millis();
    }
    n -= actual;
  }

  if (client.connected() && millis() - lastKeepalive > KEEPALIVE_INTERVAL) {
    if (millis() - lastRS232 > KEEPALIVE_INTERVAL) {
      uint8_t keepalive = 0xFF;
      client.write(&keepalive, 1);
      if(logLevel >= 3)
        appendMonitor("Keepalive gesendet", "DEBUG");
      lastKeepalive = millis();
    }
  }

  if (WiFi.status() == WL_CONNECTED && !client.connected()) {
    appendMonitor("TCP Verbindung verloren, versuche erneut...", "WARNING");
    int tries = 0;
    while (!client.connect(serverIp, serverPort)) {
      blinkLED();
      delay(100);
      tries++;
      if(tries == 25) {
        appendMonitor("Kann keine neue Verbindung zum Server aufbauen!", "ERROR");
      }
    }
    client.setNoDelay(true);
    appendMonitor("Wieder verbunden!", "INFO");
    digitalWrite(LED_PIN, HIGH);
    lastKeepalive = millis();
  }
}

void blinkLED() {
  unsigned long now = millis();
  if (now - lastBlink > 250) {
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState ? HIGH : LOW);
    lastBlink = now;
  }
}