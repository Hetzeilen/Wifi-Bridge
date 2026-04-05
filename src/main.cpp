/*
 * PROJECT: ESP32-C3 SuperMini Ultimate WiFi-UART Bridge
 * FUNCTIES: 
 * - USB-C naar UART1 (GPIO 20/21) Bridge: UART1 want Serial is USB-CDC, de bridge werkt voor Serial1
 * - Web Interface met AJAX Live Log (geen refresh nodig)
 * - Auto-scrolling monitor box
 * - LittleFS Flash opslag voor WiFi & mDNS settings
 * - SAFE MODE: Houd BOOT-knop ingedrukt bij opstarten voor nood-AP (192.168.4.1)
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <ESPmDNS.h>
#include <iostream>
#include <vector>
#include <string>
#include <time.h>
//#include <print>
#include <fmt.h>

const char*  clockFaces[] PROGMEM = {
  "\xF0\x9F\x95\x9B", // 12:00
  "\xF0\x9F\x95\xA7", // 12:30
  "\xF0\x9F\x95\x90", // 1:00
  "\xF0\x9F\x95\x9C", // 1:30
  "\xF0\x9F\x95\x91", // 2:00
  "\xF0\x9F\x95\x9D", // 2:30
  "\xF0\x9F\x95\x92", // 3:00
  "\xF0\x9F\x95\x9E", // 3:30
  "\xF0\x9F\x95\x93", // 4:00
  "\xF0\x9F\x95\x9F", // 4:30
  "\xF0\x9F\x95\x94", // 5:00
  "\xF0\x9F\x95\xA0", // 5:30
  "\xF0\x9F\x95\x95", // 6:00
  "\xF0\x9F\x95\xA1", // 6:30
  "\xF0\x9F\x95\x96", // 7:00
  "\xF0\x9F\x95\xA2", // 7:30
  "\xF0\x9F\x95\x97", // 8:00
  "\xF0\x9F\x95\xA3", // 8:30
  "\xF0\x9F\x95\x98", // 9:00
  "\xF0\x9F\x95\xA4", // 9:30
  "\xF0\x9F\x95\x99", // 10:00
  "\xF0\x9F\x95\xA5", // 10:30
  "\xF0\x9F\x95\x9A", // 11:00
  "\xF0\x9F\x95\xA6"  // 11:30
};

struct tm timeInfo;
typedef uint8_t UInt8;

// --- HARDWARE PIN DEFINITIES ---
#define BOOT_BUTTON_PIN 9   // De fysieke BOOT knop op de C3 SuperMini
#define UART1_RX 6          // Standaard RX pin voor externe hardware
#define UART1_TX 7          // Standaard TX pin voor externe hardware

// --- Hulpstructuren voor HTML formatting

struct InputOption {
    std::string id;
    std::string value;
    std::string label;
    bool isSelected;
};

// --- CONFIGURATIE STRUCTUUR ---
struct Config {
  String version = "v1";
  String ssid = "navtex";
  String pass = "navtex78";
  String mdns = "navtex";
  long baud = 38400;
  String parity = "N"; // N=none, E=even, O=odd
  String stopBits = "1"; // 1, 1.5, 2
  String dataBits = "8"; // 5, 6, 7, 8
} myCfg;

std::vector<InputOption> stopBitOptions = {
  {"opt1", "1", "1", false},
  {"opt2", "1.5", "1.5", false},
  {"opt3", "2", "2", false}
};

std::vector<InputOption> dataBitOptions = {
  {"opt1", "5", "5", false},
  {"opt2", "6", "6", false},
  {"opt3", "7", "7", false},
  {"opt4", "8", "8", false}
};

std::vector<InputOption> parityOptions = {
  {"opt1", "N", "No Parity", false},
  {"opt2", "E", "Even", false},
  {"opt3", "O", "Odd",   false}
};
// Globale variable serverOuput mag reeds voldoende groot gekozen worden
std::string serverOutput(8192, ' '); // Buffer voor server output, groot genoeg om de volledige HTML pagina te bevatten
// We hergebruiken de BOOT knop voor andere functionaliteiten met korte druk
unsigned long buttonPressedTime = 0;
const long LONG_PRESS_TIME = 3000; // 3 seconden voor lange druk
bool isPressing = false;
int buttonState = 0;

const char* CFG_PATH = "/config.txt";
uint8_t configuration_UART =  0x06; // SERIAL_8N1
uint8_t bit_parite;
uint8_t bit_arret;
uint8_t longueur_donnees;
WebServer server(80);
String eventLog = "";
const int maxLogLines = 25;
bool safeModeActive = false;

std::string parityRadioOptions; // Globale variabele om de gegenereerde Parity Radio HTML op te slaan
std::string stopBitRadioOptions; // Globale variabele om de gegenereerde Stop Bit Dropdown HTML op te slaan
std::string dataBitRadioOptions; // Globale variabele om de gegenereerde Data Bit Dropdown HTML op te slaan

String htmlHeaderString; // Globale variabele om de HTML header op te slaan
String htmlCSS;          // Globale variabele om de CSS op te slaan
String htmlSafeModeHeader; // Globale variabele om de Safe Mode header op te slaan
String htmlWifiConfig;   // Globale variabele om het WiFi Config formulier op te slaan
String htmlDeviceHeader;   // Globale variabele om de WiFi Config HTML op te slaan
String htmlUartSend;     // Globale variabele om de UART Send HTML op te slaan
String htmlLog;          // Globale variabele om de Log HTML op te slaan
String htmlUploadLogFunction; // Globale variabele om de AJAX upload functie op te slaan
std::string htmlDateTimeButton_UI; // Globale variabele DateTime submit button te decoreren
std::string htmlConfigurationButton; // Globale variabele om de configuratie buttons HTML op te slaan

// Pas de selectie aan in de vector op basis van een label-match
void selectByLabel(std::vector<InputOption>& options, const std::string& targetLabel) {
    for (auto& opt : options) {
        // Zet op true als label matcht, anders op false (radio-logica)
        opt.isSelected = (strcmp(opt.label.c_str(), targetLabel.c_str()) == 0);
    }
}

// Pas de selectie aan in de vector op basis van een value-match
void selectByValue(std::vector<InputOption>& options, std::string& targetValue) {
    for (auto& opt : options) {
        // Zet op true als value matcht, anders op false (radio-logica)
        opt.isSelected = (strcmp(opt.value.c_str(), targetValue.c_str()) == 0);
    }
}

// Handle date and time, hour 0-23, minute 0-59
const char* getCurrentClockEmoji(const int hour, const int minute) {
  // Convert to 12‑hour format
  int moduloTwelveHour = hour % 12;
  // Determine index (0–23)
  int index = moduloTwelveHour * 2 + (minute >= 30 ? 1 : 0);
  return clockFaces[index];
}

// Genereer HTML Dropdown string
std::string buildDropdownHtml(const std::vector<InputOption>& options, const std::string& name) {
    std::string rows;
    for (const auto& opt : options) {
        rows += fmt::format("  <option value=\"{}\"{}>{}</option>\n", 
                            opt.value, opt.isSelected ? " selected" : "", opt.label);                 
    }
    return fmt::format("<select name=\"{}\">\n{}</select>", name, rows);
}

// Genereer HTML Radio string
std::string buildRadioHtml(const std::vector<InputOption>& options, const std::string& groupName) {
    std::string rows;
    for (const auto& opt : options) {
        rows += fmt::format("  <input type=\"radio\" id=\"{}\" name=\"{}\" value=\"{}\"{}>\n", 
                            opt.id, groupName, opt.value, opt.isSelected ? " checked" : "");
        rows += fmt::format("  <label for=\"{}\">{}</label><br>\n", opt.id, opt.label);
    }
    return fmt::format("<fieldset>\n{}</fieldset>", rows);
}

void createParityOptions() {
  // --- Maak de radiobutttons voor parity opties
  // 1. Simuleer een wijziging op basis van tekst
  std::string choice = "No Parity"; // Dit zou kunnen komen van een gebruikersinvoer
  selectByLabel(parityOptions, choice);

  // 3. Bouw de verschillende UI componenten
  //std::string dropdown = buildDropdownHtml(myOptions, "drank_select");
  std::string radio    = buildRadioHtml(parityOptions, "parity_group");
}

// --- HULPFUNCTIES VOOR LOGBOEK ---
void addToLog(String msg) {
  eventLog += "[" + String(millis()/1000) + "s] " + msg + "<br>";
  int pos = 0, count = 0;
  while ((pos = eventLog.indexOf("<br>", pos + 1)) != -1) count++;
  if (count > maxLogLines) {
    eventLog = eventLog.substring(eventLog.indexOf("<br>") + 4);
  }
}

// --- LITTLEFS BESTANDSBEHEER ---
void saveToFlash() {
  File f = LittleFS.open(CFG_PATH, "w");
  if (f) {
    f.println(myCfg.version);
    f.println(myCfg.ssid);
    f.println(myCfg.pass);
    f.println(myCfg.mdns);
    f.println(String(myCfg.baud));
    std::vector<String> validParities = {"N", "E", "O"};
    if (find(validParities.begin(), validParities.end(), myCfg.parity) != validParities.end()) {
      f.println(myCfg.parity);
    } else {
      f.println("N"); // Default to "N" if invalid
    }
    std::vector<String> validStopBits = {"1", "1.5", "2"};
    if (find(validStopBits.begin(), validStopBits.end(), myCfg.stopBits) != validStopBits.end()) {
      f.println(myCfg.stopBits);
    } else {
      f.println("1"); // Default to "1" if invalid
    }
    std::vector<String> validDataBits = {"5", "6", "7", "8"};
    if (find(validDataBits.begin(), validDataBits.end(), myCfg.dataBits) != validDataBits.end()) {
      f.println(myCfg.dataBits);
    } else {
      f.println("8"); // Default to "8" if invalid
    }
    f.close();
    Serial.println("Configuration saved to flash.");
  }
  uint8_t parity =
  (myCfg.parity == "N") ? 0 :
  (myCfg.parity == "E") ? 2 :
  (myCfg.parity == "O") ? 3 : 0;

  uint8_t stopbits = (myCfg.stopBits == "2") ? 1 : 0;   // 0 = 1 stop, 1 = 2 stops
  uint8_t databits = (myCfg.dataBits == "7") ? 0 : 1;   // 0 = 7 bits, 1 = 8 bits

  
  // As one-liner:
  // configuration_UART1 = ( ( ( bit_parite * 4 ) + ( longueur_donnees == 7 ? 0 : 2 ) + ( bit_arret ? 1 : 0 ) )[ "emgo\x95\x9D\x97\x9F\x85\x8D\x87\x8F" ] ) - 'a';
  uint8_t configuration_UART =
    (parity << 2) | (databits << 1) | stopbits;

  // Build Parity HTML Radio
  std::string parityStr(myCfg.parity.c_str());
  selectByValue(parityOptions, parityStr);
  parityRadioOptions = buildRadioHtml(parityOptions, "parity_group").c_str();

  // Build Stop Bits HTML Dropdown
  std::string stopBitsStr(myCfg.stopBits.c_str());
  selectByValue(stopBitOptions, stopBitsStr);
  stopBitRadioOptions = buildRadioHtml(stopBitOptions, "stopBits_group");

  // Build Data Bits HTML Dropdown
  std::string dataBitsStr(myCfg.dataBits.c_str());
  selectByValue(dataBitOptions, dataBitsStr);
  dataBitRadioOptions = buildRadioHtml(dataBitOptions, "dataBits_group");
}

void loadFromFlash() {
  if (!LittleFS.exists(CFG_PATH)) {
    saveToFlash(); // Maak bestand aan met defaults bij eerste keer
    return;
  }
  File f = LittleFS.open(CFG_PATH, "r");
  if (f) {
    myCfg.version = f.readStringUntil('\n'); myCfg.version.trim();
    addToLog("Wifi Bridge version " + myCfg.version + ";");
    myCfg.ssid = f.readStringUntil('\n'); myCfg.ssid.trim();
    myCfg.pass = f.readStringUntil('\n'); myCfg.pass.trim();
    myCfg.mdns = f.readStringUntil('\n'); myCfg.mdns.trim();
    String baudAsString = f.readStringUntil('\n'); baudAsString.trim();
    if(baudAsString.length() > 0) myCfg.baud = baudAsString.toInt();
    myCfg.parity = f.readStringUntil('\n'); myCfg.parity.trim(); // Parity als string (N, E, O)
    myCfg.stopBits = f.readStringUntil('\n'); myCfg.stopBits.trim();
    myCfg.dataBits = f.readStringUntil('\n'); myCfg.dataBits.trim();
    f.close();
  }

  uint8_t parity =
    (myCfg.parity == "N") ? 0 :
    (myCfg.parity == "E") ? 2 :
    (myCfg.parity == "O") ? 3 : 0;

  uint8_t stopbits = (myCfg.stopBits == "2") ? 1 : 0;   // 0 = 1 stop, 1 = 2 stops
  uint8_t databits = (myCfg.dataBits == "7") ? 0 : 1;   // 0 = 7 bits, 1 = 8 bits

  
  // As one-liner:
  // configuration_UART1 = ( ( ( bit_parite * 4 ) + ( longueur_donnees == 7 ? 0 : 2 ) + ( bit_arret ? 1 : 0 ) )[ "emgo\x95\x9D\x97\x9F\x85\x8D\x87\x8F" ] ) - 'a';
  uint8_t configuration_UART =
    (parity << 2) | (databits << 1) | stopbits;

  // Build Parity HTML Radio
  std::string parityStr(myCfg.parity.c_str());
  selectByValue(parityOptions, parityStr);
  parityRadioOptions = buildRadioHtml(parityOptions, "parity_group").c_str();

  // Build Stop Bits HTML Dropdown
  std::string stopBitsStr(myCfg.stopBits.c_str());
  selectByValue(stopBitOptions, stopBitsStr);
  stopBitRadioOptions = buildRadioHtml(stopBitOptions, "stopBits_group");

  // Build Data Bits HTML Dropdown
  std::string dataBitsStr(myCfg.dataBits.c_str());
  selectByValue(dataBitOptions, dataBitsStr);
  dataBitRadioOptions = buildRadioHtml(dataBitOptions, "dataBits_group");

}

// Cast wallclock time to global variable and update Html button representation
void castWallClockTime() {
  // Obtain proper clock face based on current time
  time_t now = time(nullptr);
  localtime_r(&now, &timeInfo);
  int wallClockHour = timeInfo.tm_hour;
  int wallClockMinute = timeInfo.tm_min; 
  int wallClockSecond = timeInfo.tm_sec;
  std::string clockFace = getCurrentClockEmoji(wallClockHour, wallClockMinute); // Voorbeeld: 14:45
  std::string wallClockTime = fmt::format("{:02d}:{:02d}:{:02d}", wallClockHour, wallClockMinute, wallClockSecond);
  htmlDateTimeButton_UI = fmt::format("<button type='submit' form='wallclock'> {} {}</button><form id='wallclock' method='get' action='/wallclock'></form>", clockFace, wallClockTime);
  Serial.print("Casted wallClock: ");
  Serial.println(wallClockTime.c_str());
}

// --- WEB INTERFACE HTML ---
const String UI_SAFE_MODE_HEADER = "⚠️ SAFE MODE ACTIEF";
const String UI_SAFE_MODE_DESC = "Verbonden via Access Point (192.168.4.1)";
const String UI_TITLE = "ESP32-C3 SuperMini Bridge";
const String UI_NET_SETTINGS = "📶 Netwerk Instellingen";
const String UI_DEV_SETTINGS = " 🔧 Instellingen";
const String UI_UART_COMMAND = "📤 UART Command";
const String UI_SERIAL_MONITOR = "📟 Seriële Monitor";
const String UI_SAVE_BUTTON = "Opslaan & Herstarten";
const String UI_SEND_BUTTON = "Verzenden";
const String UI_PLACEHOLDER = "Typ bericht naar hardware...";
const String UI_CONFIG_BUTTON = "<button type='submit' form='uiConfig'>Wijzig</button><form id='uiConfig' method='get' action='/uiConfig'></form>";

void createHtmlTemplates() {
castWallClockTime(); // Update global variable with current time and clock emoji, sets htmlDateTimeButton_UI

htmlHeaderString = "<html><head><title>C3 Bridge</title><meta name='viewport' content='width=device-width, initial-scale=1'>";
  // AJAX Script
htmlUploadLogFunction = R"rawliteral(
<script>
function upLog() {
  const b = document.getElementById('lb');
  const s = b.scrollHeight - b.clientHeight <= b.scrollTop + 50;
  fetch('/getlog')
    .then(r => r.text())
    .then(d => {
      b.innerHTML = d;
      if (s) b.scrollTop = b.scrollHeight;
    });
}
setInterval(upLog, 1200);
</script></div>
)rawliteral";
  // CSS
  const String SAFE_MODE_BG_COLOR = "#fff0f0";
  const String NORMAL_BG_COLOR = "#f4f7f6";
  // Set page background color based on safe mode status
  String pageBackgroundColor = safeModeActive ? SAFE_MODE_BG_COLOR : NORMAL_BG_COLOR;
  htmlCSS = R"rawliteral(
  <style>
body {
  font-family: sans-serif;
  background: )rawliteral" + pageBackgroundColor + R"rawliteral(; 
  padding: 15px;
}

.container {
  display: flex;
  flex-wrap: wrap;
  gap: 20px;
  max-width: 1200px;
  margin: 0 auto;
  padding: 20px;
}
.card {
  background: #fff;
  padding: 15px;
  border-radius: 10px;
  box-shadow: 0 2px 5px rgba(0,0,0,0.1);
  margin-bottom: 15px;
}
.secDiv input {
  width: 48%;
  display: inline-block;
}
.secDiv input:first-child {
  margin-right: 4%;
}
.box {
  flex: 1 1 300px;
  background: #f4f4f4;
  border: 1px solid #ccc;
  padding: 20px;
  border-radius: 8px;
}
input[type=text], input[type=password] {
  width: 100%;
  padding: 10px;
  margin: 5px 0;
  box-sizing: border-box;
}
.btn {
  padding: 10px 20px;
  background: #007bff;
  color: #fff;
  border: none;
  border-radius: 5px;
  cursor: pointer;
  width: 100%;
}
</style>
</head><body>
  )rawliteral";

     if(safeModeActive) {
       htmlSafeModeHeader = "<h2 style='color:red;'>" + UI_SAFE_MODE_HEADER + "</h2><p>" + UI_SAFE_MODE_DESC + "</p>";
     } else {
       htmlSafeModeHeader = "<h2>" + UI_TITLE + "</h2>";
     }

  // WiFi Config
  htmlWifiConfig = R"rawliteral(
<div class='container'><div class='box'>
  <h3>)rawliteral" + UI_NET_SETTINGS + R"rawliteral(</h3>
  <form action='/savecfg' method='POST'>
    <label>SSID:<input name='s' type='text' value=')rawliteral" + myCfg.ssid + R"rawliteral(' required></label>
    <label>Password:<input name='p' type='password' value='/\/\' required></label>
    <label>mDNS:<input name='m' type='text' value=')rawliteral" + myCfg.mdns + R"rawliteral(' required></label>
    <label>Baudrate:<input name='b' type='text' value=')rawliteral" + String(myCfg.baud) + R"rawliteral(' required></label>
    <p>Parity</p>)rawliteral" + parityRadioOptions.c_str() + R"rawliteral(
    <p>Data Bits</p>)rawliteral" + dataBitRadioOptions.c_str() + R"rawliteral(
    <p>Stop Bits</p>)rawliteral" + stopBitRadioOptions.c_str() + R"rawliteral(</label>
    <div class='secDiv'>
    <input type='submit' class='btn' value=')rawliteral" + UI_SAVE_BUTTON + R"rawliteral(' style='background:#28a745;'>
    </div>
  </form>
</div>
)rawliteral";

htmlDeviceHeader = R"rawliteral(
<div class='container'><div class='box'>
  <h3>)rawliteral" + UI_DEV_SETTINGS + R"rawliteral(</h3>
  <table>
    <th>Configuratie</th>
    <tr><td>)rawliteral"+ htmlDateTimeButton_UI.c_str() + R"rawliteral(<td>Versie:</td><td>)rawliteral" + myCfg.version + R"rawliteral(</td></tr>
    <tr><td>Communicatie:</td><td>BaudRate:</td><td>)rawliteral" + String(myCfg.baud) + R"rawliteral(</td></tr>
    <tr><td>Data Bits:</td><td>)rawliteral" + myCfg.dataBits + R"rawliteral(</td></tr>
    <tr><td>Parity:</td><td>)rawliteral" + myCfg.parity + R"rawliteral(</td></tr>
    <tr>)rawliteral" + UI_CONFIG_BUTTON + R"rawliteral(<td>Stop Bits:</td><td>)rawliteral" + myCfg.stopBits + R"rawliteral(</td></tr>
  </table>
</div>
)rawliteral";

  // UART Send
  htmlUartSend = R"rawliteral(
</div><div class='container'><div class='box'>
  <h3>)rawliteral" + UI_UART_COMMAND + R"rawliteral(</h3>
  <form action='/write' method='POST'>
    <label><input type='text' name='t' id='command' placeholder=')rawliteral" + UI_PLACEHOLDER + R"rawliteral('>Command:</label>
    <input type='submit' class='btn' value=')rawliteral" + UI_SEND_BUTTON + R"rawliteral('>
  </form>
</div>
)rawliteral";

  // Log
  htmlLog = R"rawliteral(
<div class='box'>
  <h3>)rawliteral" + UI_SERIAL_MONITOR + R"rawliteral(</h3>
  <div id='lb' class='box'>)rawliteral" + eventLog + R"rawliteral(</div>
</div>
</body></html>
)rawliteral";
}

void handleRoot() {
  serverOutput.clear(); // Clear buffer before generating new output
  createHtmlTemplates(); // Regenerate templates to update dynamic content like clock emoji
  if (safeModeActive) {
    fmt::format_to(std::back_inserter(serverOutput), "{}{}{}{}{}{}{}", htmlHeaderString, htmlCSS, htmlSafeModeHeader, htmlWifiConfig, htmlUartSend, htmlLog, htmlUploadLogFunction);
  } else {
    fmt::format_to(std::back_inserter(serverOutput), "{}{}{}{}{}{}{}", htmlHeaderString, htmlCSS, htmlSafeModeHeader, htmlDeviceHeader, htmlUartSend, htmlLog, htmlUploadLogFunction);
  } 
  server.send(200, "text/html; charset=UTF-8", serverOutput.c_str());
}

// Herstart
void systemConfig() {
  Serial.println("System config by button push...");
  addToLog("System config by button push...");
  safeModeActive = true;
  delay(500);
  server.sendHeader("Location", "/"); server.send(303);
}

// --- SETUP ---
void setup() {

  Serial.begin(115200); // USB-CDC
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

  if (!LittleFS.begin(true)) {
    delay(1000);
    Serial.println("FS Fout");
    return;
  }
  loadFromFlash();
  createHtmlTemplates();

  // Hardware UART1 configuratie
  Serial1.begin(myCfg.baud, SERIAL_8N1, UART1_RX, UART1_TX);

  // CHECK VOOR SAFE MODE (BOOT knop ingedrukt tijdens start)
  delay(100);
  WiFi.mode(WIFI_AP);
  if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
    safeModeActive = true;
    myCfg.ssid = "C3-SAFE-MODE";
    myCfg.pass = "12345678";
    WiFi.softAP(myCfg.ssid, myCfg.pass);
    addToLog("!!! SAFE MODE ACTIEF !!!");
    addToLog("Ga naar 192.168.4.1 en verbind met 'C3-SAFE-MODE' (WW: 12345678)");
    Serial.println("Ga naar 192.168.4.1 en verbind met 'C3-SAFE-MODE' (WW: 12345678)");
  } else {
    WiFi.softAP(myCfg.ssid.c_str(), myCfg.pass.c_str());
    MDNS.begin(myCfg.mdns.c_str());
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    int retry = 0;
    while (!WiFi.softAPSSID().equals(myCfg.ssid) && retry < 40) {
      Serial.print(".");
      delay(1200); retry++;
    }
    addToLog("WiFi available! Browser: http://" + myCfg.mdns + ".local");
    Serial.println("WiFi available! Browser: http://" + myCfg.mdns + ".local");
  }

  // Routes
  server.on("/", handleRoot);
  server.on("/getlog", [](){ server.send(200, "text/plain", eventLog); });
  server.on("/savecfg", [](){
    myCfg.ssid = server.arg("s"); // Lees ssid van formulier
    String passFromForm = server.arg("p"); // Lees password van formulier
    if(passFromForm == "/\\/\\") {
      // Als het wachtwoord veld ongewijzigd is (placeholder), behoud dan het oude wachtwoord
      Serial.println("Wachtwoord ongewijzigd, behoud huidig wachtwoord.");
    } else {
       myCfg.pass = passFromForm; // Lees password van formulier
    }
    myCfg.mdns = server.arg("m"); // Lees mDNS van formulier
    String baudAsString = server.arg("b"); // Lees baudrate van formulier
    myCfg.baud = baudAsString.toInt(); // Converteer naar integer
    myCfg.parity = server.arg("parity_group").c_str(); // Lees parity_group van formulier
    myCfg.stopBits = server.arg("stopBits_group").c_str(); // Lees stopBits_group van formulier
    myCfg.dataBits = server.arg("dataBits_group").c_str(); // Lees dataBits_group van formulier
    Serial.println("parityBits van portaal: " + String(myCfg.parity.c_str()));
    Serial.println("stopBits van portaal: " + String(myCfg.stopBits.c_str()));
    Serial.println("dataBits van portaal: " + String(myCfg.dataBits.c_str()));
    safeModeActive = false; // Zet safe mode uit bij opslaan van nieuwe config

    saveToFlash();
    createHtmlTemplates();
    addToLog("Nieuwe configuratie opgeslagen. Herstarten...");
    Serial.println("Nieuwe configuratie opgeslagen. Herstarten...");
    server.sendHeader("Location", "/"); server.send(303);
    Serial1.begin(myCfg.baud, SERIAL_8N1, UART1_RX, UART1_TX);
    delay(2000);
    ESP.restart();
  });
  server.on("/write", [](){
    if(server.hasArg("t")){ 
      Serial1.println(server.arg("t")); 
      addToLog("<b>WEB-OUT:</b> " + server.arg("t")); 
    }
    server.sendHeader("Location", "/"); server.send(303);
  });
  server.on("/wallclock", [](){ server.send(200, "text/plain", "Hier komen wallclock functies."); });

  server.begin();
}

// --- MAIN LOOP ---
void loop() {
  server.handleClient();

  // UART1 naar WEB/USB
  if (Serial1.available()) {
    String in = Serial1.readStringUntil('\n'); in.trim();
    addToLog("<b>UART-IN:</b> " + in);
    Serial.println("UART: " + in);
  }

  // Dubbele functionaliteit op BOOT knop: korte druk voor herstart, lange druk voor safe mode toggle
  buttonState = digitalRead(BOOT_BUTTON_PIN);
  if (buttonState == LOW && !isPressing) {
    buttonPressedTime = millis();
    isPressing = true;
  }
  // Knop wordt vastgehouden: check voor lange druk
  if (buttonState == LOW && isPressing) {
    if(millis() - buttonPressedTime >= LONG_PRESS_TIME) {
      Serial.println("Herstart! Houd ingedrukt voor Bootloader.");
      addToLog("Herstart! Houd ingedrukt voor Bootloader.");
      delay(500);
      ESP.restart(); // De chip herstart nu
    }
  }
  // Knop wordt lostgelaten: check voor korte druk
  if (buttonState == HIGH && isPressing) {
    unsigned long holdDuration = millis() - buttonPressedTime;
    if (holdDuration < LONG_PRESS_TIME) {
      systemConfig(); // Korte druk: configuratieScherm tonen
    }
    isPressing = false;
  }

  // USB naar UART0
  if (Serial.available()) {
    String usb = Serial.readStringUntil('\n');
    usb.trim();
    Serial.println(usb);
    addToLog("<b>USB-IN:</b> " + usb);
  }
}