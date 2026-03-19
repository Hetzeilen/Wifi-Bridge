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
//#include <print>
#include <fmt.h>

typedef uint8_t UInt8;

// --- HARDWARE PIN DEFINITIES ---
#define BOOT_BUTTON_PIN 9   // De fysieke BOOT knop op de C3 SuperMini
#define UART1_RX 20         // Standaard RX pin voor externe hardware
#define UART1_TX 21         // Standaard TX pin voor externe hardware

// --- Hulpstructuren voor HTML formatting

struct InputOption {
    std::string id;
    std::string value;
    std::string label;
    bool isSelected;
};

// --- CONFIGURATIE STRUCTUUR ---
struct Config {
  String ssid = "navtex";
  String pass = "navtex";
  String mdns = "supermini";
  long baud = 4800;
  std::string parity = "N"; // N=none, E=even, O=odd
  std::string stopBits = "1"; // 1, 1.5, 2
  std::string dataBits = "8"; // 5, 6, 7, 8
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

const char* CFG_PATH = "/config.txt";
uint8_t configuration_UART1 = SERIAL_8N1;
uint8_t bit_parite;
uint8_t bit_arret;
uint8_t longueur_donnees;
WebServer server(80);
String eventLog = "";
const int maxLogLines = 25;
bool safeModeActive = false;

std::string parityRadioOptions; // Globale variabele om de gegenereerde Parity Radio HTML op te slaan
std::string stopBitDropdown; // Globale variabele om de gegenereerde Stop Bit Dropdown HTML op te slaan
std::string dataBitDropdown; // Globale variabele om de gegenereerde Data Bit Dropdown HTML op te slaan

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
    f.println(myCfg.ssid);
    f.println(myCfg.pass);
    f.println(myCfg.mdns);
    f.println(String(myCfg.baud));
    std::vector<String> validParities = {"N", "E", "O"};
    if (find(validParities.begin(), validParities.end(), myCfg.parity.c_str()) != validParities.end()) {
      f.println(myCfg.parity.c_str());
    } else {
      f.println("N"); // Default to "N" if invalid
    }
    std::vector<String> validStopBits = {"1", "1.5", "2"};
    if (find(validStopBits.begin(), validStopBits.end(), myCfg.stopBits.c_str()) != validStopBits.end()) {
      f.println(myCfg.stopBits.c_str());
    } else {
      f.println("1"); // Default to "1" if invalid
    }
    std::vector<String> validDataBits = {"5", "6", "7", "8"};
    if (find(validDataBits.begin(), validDataBits.end(), myCfg.dataBits.c_str()) != validDataBits.end()) {
      f.println(myCfg.dataBits.c_str());
    } else {
      f.println("8"); // Default to "8" if invalid
    }
    f.close();
  }
}

void loadFromFlash() {
  if (!LittleFS.exists(CFG_PATH)) {
    saveToFlash(); // Maak bestand aan met defaults bij eerste keer
    return;
  }
  File f = LittleFS.open(CFG_PATH, "r");
  if (f) {
    myCfg.ssid = f.readStringUntil('\n'); myCfg.ssid.trim();
    myCfg.pass = f.readStringUntil('\n'); myCfg.pass.trim();
    myCfg.mdns = f.readStringUntil('\n'); myCfg.mdns.trim();
    String baudAsString = f.readStringUntil('\n'); baudAsString.trim();
    if(baudAsString.length() > 0) myCfg.baud = baudAsString.toInt();
    myCfg.parity = f.readStringUntil('\n').c_str(); // Parity als string (N, E, O)
    String stopBitsAsString = f.readStringUntil('\n'); stopBitsAsString.trim();
    if(stopBitsAsString.length() > 0) myCfg.stopBits = stopBitsAsString.toInt();
    String dataBitsAsString = f.readStringUntil('\n'); dataBitsAsString.trim();
    if(dataBitsAsString.length() > 0) myCfg.dataBits = dataBitsAsString.toInt();
    f.close();
  }
  bit_parite = (myCfg.parity == "N") ? 0 : (myCfg.parity == "E" ? 2 : 3);
  bit_arret = (myCfg.stopBits == "1.5" || myCfg.stopBits == "2") ? 1 : 0;
  longueur_donnees = (myCfg.dataBits == "7") ? 0 : 1; // 0 voor 7 bits, 1 voor 8 bits
  
  // As one-liner:
  configuration_UART1 = ( ( ( bit_parite * 4 ) + ( longueur_donnees == 7 ? 0 : 2 ) + ( bit_arret ? 1 : 0 ) )[ "emgo\x95\x9D\x97\x9F\x85\x8D\x87\x8F" ] ) - 'a';
  // Build Parity HTML Radio
  selectByValue(parityOptions, myCfg.parity);
  parityRadioOptions = buildRadioHtml(parityOptions, "parity_group").c_str();

  // Build Stop Bits HTML Dropdown
  selectByValue(stopBitOptions, myCfg.stopBits);
  stopBitDropdown = buildDropdownHtml(stopBitOptions, "stopBits_group");

  // Build Data Bits HTML Dropdown
  selectByValue(dataBitOptions, myCfg.dataBits);
  dataBitDropdown = buildDropdownHtml(dataBitOptions, "dataBits_group");

}

// --- WEB INTERFACE HTML ---
const String UI_SAFE_MODE_HEADER = "⚠️ SAFE MODE ACTIEF";
const String UI_SAFE_MODE_DESC = "Verbonden via Access Point (192.168.4.1)";
const String UI_TITLE = "ESP32-C3 SuperMini Bridge";
const String UI_NET_SETTINGS = "📶 Netwerk Instellingen";
const String UI_UART_COMMAND = "📤 UART Command";
const String UI_SERIAL_MONITOR = "📟 Seriële Monitor";
const String UI_SAVE_BUTTON = "Opslaan & Herstarten";
const String UI_SEND_BUTTON = "Verzenden";
const String UI_PLACEHOLDER = "Typ bericht naar hardware...";

void handleRoot() {
  String htmlHeaderString = "<html><head><title>C3 Bridge</title><meta name='viewport' content='width=device-width, initial-scale=1'>";
  // AJAX Script
  String htmlUploadLogFunction = R"rawliteral(
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
</script>
)rawliteral";
  // CSS
  const String SAFE_MODE_BG_COLOR = "#fff0f0";
  const String NORMAL_BG_COLOR = "#f4f7f6";
  // Set page background color based on safe mode status
  String pageBackgroundColor = safeModeActive ? SAFE_MODE_BG_COLOR : NORMAL_BG_COLOR;
  String htmlCSS = R"rawliteral(
  <style>
  body {
    font-family: sans-serif;
    background: )rawliteral" + pageBackgroundColor + R"rawliteral(; 
    padding: 15px;
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
    background: #1e1e1e;
    color: #0f0;
    padding: 10px;
    height: 280px;
    overflow-y: auto;
    font-family: monospace;
    border-radius: 5px;
    white-space: pre-wrap;
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

  String htmlSafeModeHeader;
     if(safeModeActive) {
       htmlSafeModeHeader = "<h2 style='color:red;'>" + UI_SAFE_MODE_HEADER + "</h2><p>" + UI_SAFE_MODE_DESC + "</p>";
     } else {
       htmlSafeModeHeader = "<h2>" + UI_TITLE + "</h2>";
     }

  // WiFi Config
  String htmlWifiConfig = R"rawliteral(
<div class='card'>
  <h3>)rawliteral" + UI_NET_SETTINGS + R"rawliteral(</h3>
  <form action='/savecfg' method='POST'>
    <label>SSID:<input name='s' type='text' value=')rawliteral" + myCfg.ssid + R"rawliteral('></label>
    <label>Password:<input name='p' type='password' value=')rawliteral" + myCfg.pass + R"rawliteral('></label>
    <label>mDNS:<input name='m' type='text' value=')rawliteral" + myCfg.mdns + R"rawliteral('></label>
    <label>Baudrate:<input name='b' type='text' value=')rawliteral" + String(myCfg.baud) + R"rawliteral('></label>
    <p>Parity</p>)rawliteral" + parityRadioOptions.c_str() + R"rawliteral(
    <p>Data Bits</p>)rawliteral" + dataBitDropdown.c_str() + R"rawliteral(
    <p>Stop Bits</p>')rawliteral" + stopBitDropdown.c_str() + R"rawliteral('></label>
    <div class='secDiv'>
    <input type='submit' class='btn' value=')rawliteral" + UI_SAVE_BUTTON + R"rawliteral(' style='background:#28a745;'>
    </div>
  </form>
</div>
)rawliteral";

  // UART Send
  String htmlUartSend = R"rawliteral(
<div class='card'>
  <h3>)rawliteral" + UI_UART_COMMAND + R"rawliteral(</h3>
  <form action='/write' method='POST'>
    <label><input type='text' name='t' id='command' placeholder=')rawliteral" + UI_PLACEHOLDER + R"rawliteral('>Command:</label>
    <input type='submit' class='btn' value=')rawliteral" + UI_SEND_BUTTON + R"rawliteral('>
  </form>
</div>
)rawliteral";

  // Log
  String htmlLog = R"rawliteral(
<div class='card'>
  <h3>)rawliteral" + UI_SERIAL_MONITOR + R"rawliteral(</h3>
  <div id='lb' class='box'>)rawliteral" + eventLog + R"rawliteral(</div>
</div>
</body></html>
)rawliteral";
  String serverOutput = htmlHeaderString + htmlCSS + htmlSafeModeHeader + htmlWifiConfig + htmlUartSend + htmlLog + htmlUploadLogFunction;
  server.send(200, "text/html", serverOutput);
}

// --- SETUP ---
void setup() {

  Serial.begin(115200); // USB-CDC
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

  if (!LittleFS.begin(true)) { Serial.println("FS Fout"); return; }
  loadFromFlash();

  // Hardware UART1 configuratie
  Serial1.begin(myCfg.baud, configuration_UART1, UART1_RX, UART1_TX);

  // CHECK VOOR SAFE MODE (BOOT knop ingedrukt tijdens start)
  delay(100); 
  if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
    safeModeActive = true;
    WiFi.softAP("C3-SAFE-MODE", "12345678");
    addToLog("!!! SAFE MODE ACTIEF !!!");
    addToLog("Verbind met 'C3-SAFE-MODE' (WW: 12345678) en ga naar 192.168.4.1");
  } else {
    WiFi.begin(myCfg.ssid.c_str(), myCfg.pass.c_str());
    int retry = 0;
    while (WiFi.status() != WL_CONNECTED && retry < 20) { delay(500); retry++; }
    
    if (WiFi.status() == WL_CONNECTED) {
      MDNS.begin(myCfg.mdns.c_str());
      addToLog("WiFi Verbonden! Browser: http://" + myCfg.mdns + ".local");
    } else {
      addToLog("WiFi FOUT. Houd BOOT ingedrukt bij reset voor Safe Mode.");
    }
  }

  // Routes
  server.on("/", handleRoot);
  server.on("/getlog", [](){ server.send(200, "text/plain", eventLog); });
  server.on("/savecfg", [](){
    myCfg.ssid = server.arg("s"); // Lees ssid van formulier
    myCfg.pass = server.arg("p"); // Lees password van formulier
    myCfg.mdns = server.arg("m"); // Lees mDNS van formulier
    String baudAsString = server.arg("b"); // Lees baudrate van formulier
    myCfg.baud = baudAsString.toInt(); // Converteer naar integer
    myCfg.parity = server.arg("parity_group").c_str(); // Lees parity_group van formulier
    myCfg.stopBits = server.arg("stopBits_group").c_str(); // Lees stopBits_group van formulier
    myCfg.dataBits = server.arg("dataBits_group").c_str(); // Lees dataBits_group van formulier 
    saveToFlash();
    server.send(200, "text/plain", "Opgeslagen! ESP32 herstart nu...");
    delay(2000); ESP.restart();
  });
  server.on("/write", [](){
    if(server.hasArg("t")){ 
      Serial1.println(server.arg("t")); 
      addToLog("<b>WEB-OUT:</b> " + server.arg("t")); 
    }
    server.sendHeader("Location", "/"); server.send(303);
  });

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

  // USB naar UART1
  if (Serial.available()) {
    String usb = Serial.readStringUntil('\n'); usb.trim();
    Serial1.println(usb);
    addToLog("<b>USB-IN:</b> " + usb);
  }
}