/*
 * PROJECT: ESP32-C3 SuperMini Ultimate WiFi-UART Bridge
 * FUNCTIES: 
 * - USB-C naar UART1 (GPIO 20/21) Bridge: UART1 want Serial is USB-CDC, de bridge werkt voor Serial1
 * - Web Interface met AJAX Live Log (geen refresh nodig)
 * - Auto-scrolling monitor box
 * - LittleFS Flash opslag voor WiFi & mDNS settings
 * - SAFE MODE: Houd BOOT-knop ingedrukt bij opstarten voor nood-AP (192.168.4.1)
 * 
 *  Latere uitbreiding:
 * DS3231 SQW  →  ESP32‑C3 GPIO (any interrupt‑capable pin)
 * GND         →  GND
 */


#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ESPmDNS.h>
#include <iostream>
#include <map>
#include <vector>
#include <string>

//#include <print>
#include <fmt.h>
#include <Wire.h>
#include <RTClib.h>

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
#define BOOT_BUTTON_PIN 10   // De fysieke BOOT knop op de C3 SuperMini
#define UART1_RX 6          // Standaard RX pin voor externe hardware
#define UART1_TX 7          // Standaard TX pin voor externe hardware
#define SDA 10               // I2C SDA pin voor RTC
#define SCL 1               // I2C SCL pin voor RTC
#define RTC_DS3231_ADDRESS 0x68 // I2C adres van de DS3231 RTC

// --- Hulpstructuren voor HTML formatting

struct InputOption {
    std::string id;
    std::string value;
    std::string label;
    bool isSelected;
};

// =========================
// HASH FUNCTION (FNV-1a)
// =========================

uint32_t fnv1aHash(const std::string& s) {
    uint32_t hash = 0x811C9DC5;
    for (size_t i = 0; i < s.length(); i++) {
        hash ^= (uint8_t)s[i];
        hash *= 0x01000193;
    }
    return hash;
}
// =========================
// NAVTEX MESSAGE STRUCTURE
// =========================

struct NavtexMessage {
  std::string raw;               // full NAVTEX message text block (for this station)
  std::string messageKey;        // EA99, GA47, TD42, ...
  bool markedForDeletion = false;    // checkbox flag. When true expand, otherwise collapse
  NavtexMessage* next = nullptr;

  NavtexMessage(const std::string& message, const std::string& key)
    : raw(message), messageKey(key), markedForDeletion(false), next(nullptr) {}
};


// --- CONFIGURATIE STRUCTUUR ---
struct Config {
  String version = "v2";
  String ssid = "navtex";
  String pass = "navtex78";
  String mdns = "navtex";
  long baud = 38400;
  String parity = "N"; // N=none, E=even, O=odd
  String stopBits = "1"; // 1, 1.5, 2
  String dataBits = "8"; // 5, 6, 7, 8
  String hasRTC = "0"; // "0" = geen RTC, "1" = RTC aanwezig
  String stationCodeToFilter = "T"; // Default to Oostende Radio
  String subjectIndicatorToFilter = "A"; // Default to Navigational Warnings
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

// Hier komen de Navtex Subject Indicator Characters
std::vector<InputOption> subjectIndicators = {
  {"opt1", "A", "Navigational Warnings", false},
  {"opt2", "B", "Meteorological Warnings", false},
  {"opt3", "C", "Ice Reports", false},
  {"opt4", "D", "Search & Rescue Information, and pirate attack warnings", false},
  {"opt5", "E", "Meteorological Forecasts", false},
  {"opt6", "F", "Pilot service Messages", false},
  {"opt7", "G", "DECCA Messages", false},
  {"opt8", "H", "LORAN Messages", false},
  {"opt9", "J", "SATNAV Messages", false},
  {"opt10","K", "Other Electronic Navaid Messages", false},
  {"opt11","L", "Navigational warnings additional to letter A", false},
  {"opt12","V", "Special Services Allocated by NAVTEX Panel (V)", false},
  {"opt13","W", "Special Services Allocated by NAVTEX Panel (W)", false},
  {"opt14","X", "Special Services Allocated by NAVTEX Panel (X)", false},
  {"opt15","Y", "Special Services Allocated by NAVTEX Panel (Y)", false},
  {"opt16","Z", "No messages on hand.", false} 
};

// Globale variable serverOuput mag reeds voldoende groot gekozen worden
std::string serverOutput(8192, ' '); // Buffer voor server output, groot genoeg om de volledige HTML pagina te bevatten
// We hergebruiken de BOOT knop voor andere functionaliteiten met korte druk
unsigned long buttonPressedTime = 0;
const long LONG_PRESS_TIME = 3000; // 3 seconden voor lange druk
bool isPressing = false;
int buttonState = 0;
String serial1In = ""; // From Serial1
String usb = "";  // From USB


const char* CFG_PATH = "/config.txt";
uint8_t configuration_UART =  0x06; // SERIAL_8N1
uint8_t bit_parite;
uint8_t bit_arret;
uint8_t longueur_donnees;
// Provide RealTime Clock DS3231 support via I2C
RTC_DS3231 rtc;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
String eventLog = ""; // Reserveer geheugen voor logboek, voorkomt fragmentatie
const int maxLogLines = 25;
const unsigned long LOG_PUSH_INTERVAL_MS = 250;
bool logUpdatePending = false;
unsigned long lastLogPushMs = 0;
bool safeModeActive = false;
bool signalHardwareChange = false; // Variabele om aan te geven dat er nieuwe hardware is gedetecteerd, kan later worden gebruikt voor notificaties
bool isParsing = false; // Zet dit 'aan' indien we $S berichten aan het parsen zijn, zodat we geen andere berichten in de log opnemen

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
String htmlUartReceive;  // Globale variabele om de UART Receive HTML op te slaan
String UI_RECEIVE_BUTTON = "<div class='box'><button onclick='window.location=\"/receive\"'>Ontvang</button></div>"; // Globale variabele om de button om te ontvangen 'aan' en 'af' te zetten
std::string htmlDateTimeButton_UI; // Globale variabele DateTime submit button te decoreren
std::string htmlConfigurationButton; // Globale variabele om de configuratie buttons HTML op te slaan

NavtexMessage* head = nullptr;
std::map<std::string, NavtexMessage*> messageIndex;
uint32_t lastHash = 0;   // hash of previous $S transmission
const size_t MAX_NAVTEX_BLOCK_SIZE = 20480;
std::string textBlock;

std::string singleMsg(1024, '\0'); // reserve a single message
std::string htmlSubjectIndicatorDropdownOptions; // Globale variable om de gegenereerde Subject Indicator Dropdown HTML op te slaan

// =========================
// FIND NAVTEX MESSAGE BY KEY
// =========================
NavtexMessage* findNavtexMessage(const std::string& messageKey) {
  auto indexEntry = messageIndex.find(messageKey);
  return indexEntry == messageIndex.end() ? nullptr : indexEntry->second;
}

// =========================
// EXTRACT STATION CODE
// =========================

std::string extractStation(const std::string& message) {
    if (message[0] == '>' && message.length() >= 3) {
      // Station is a single char
        return message.substr(1, 1);
    }
    return "N"; // Default value if not found
};

// =========================
// EXTRACT MESSAGE KEY
// =========================
std::string extractMessageKey(const std::string& message) {
  if (message[0] == '>' && message.length() >= 5) {
    return message.substr(1, 4);
  }

  return "NNNN"; // Default value if not found
}

// =========================
// EXTRACT Subject Indicator Character
// =========================

std::string extractSubjectIndicator(const std::string& message) {
    if (message[0] == '>' && message.length() >= 5) {
      // Subject is a single char
        return message.substr(2, 1);
    }
    return "N"; // Default value if not found
};

// =========================
// ADD MESSAGE TO LINKED LIST
// =========================

void addNavtexMessage(NavtexMessage*& listHead, const std::string& msg, const std::string& messageKey) {
  if (messageKey.length() == 4) { 
    if (!listHead) {
      // the first element receives listHead as next pointer, which is null pointer
      listHead = new NavtexMessage(msg, messageKey);
      messageIndex.emplace(messageKey, listHead);
      Serial.println("Start van linked list met eerste NAVTEX bericht");
      Serial.printf("%s\n",msg.c_str());
    } else {
      if (findNavtexMessage(messageKey)) {
        Serial.printf("Duplicaat bericht gevonden, niet toegevoegd. Sleutel: %s\n", messageKey);
        return;
      } else {
        // we update listHead pointing to a new NavtexMessage which we create with next reference to the original head
        NavtexMessage* newMsg = new NavtexMessage(msg, messageKey);
        newMsg->next = listHead;
        listHead = newMsg;
        messageIndex.emplace(messageKey, newMsg);
        Serial.printf("NAVTEX bericht toegevoegd aan linked list. Sleutel: %s\n", messageKey);
      }
    }
  } else {
    Serial.printf("De sleutel van het Navtex bericht is niet lang genoeg: %s\n", messageKey);
  }
}

// Pas de selectie aan in de vector op basis van een label-match
void selectByLabel(std::vector<InputOption>& options, const std::string& targetLabel) {
    for (auto& opt : options) {
        // Zet op true als label matcht, anders op false (radio-logica)
        opt.isSelected = (strcmp(opt.label.c_str(), targetLabel.c_str()) == 0);
    }
}

// Pas de selectie aan in de vector op basis van een value-match
void selectByValue(std::vector<InputOption>& options, const std::string& targetValue) {
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
std::string buildDropdownHtml(const std::vector<InputOption>& options, const std::string& groupName) {
    std::string rows;
    for (const auto& opt : options) {
        rows += fmt::format("  <option value=\"{}\"{}>{}</option>\n", 
                            opt.value, opt.isSelected ? " selected" : "", opt.label); 
        rows += fmt::format("  <label for=\"{}\">{}</label><br>\n", opt.id, opt.label);                
    }
    return fmt::format("<select name=\"{}\">\n{}</select>", groupName, rows);
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
void pushLogToClients() {
  logUpdatePending = ws.count() > 0;
}

void serviceLogUpdates() {
  const unsigned long now = millis();
  if (logUpdatePending && ws.count() > 0 && now - lastLogPushMs >= LOG_PUSH_INTERVAL_MS) {
    logUpdatePending = false;
    lastLogPushMs = now;
    ws.textAll(eventLog);
  }
}

void addToLog(String msg, bool includeTimestamp = true) {
  std::string regel(126, ' '); // Reserveer geheugen voor logregel, voorkomt fragmentatie
  struct tm now_ts;
  if (includeTimestamp) {
    getLocalTime(&now_ts);
    // Formatteer de huidige tijd zodat de logger deze herkent
    char buf[40];
    strftime(buf, sizeof(buf), "%C%y%m%d %H%M%S", &now_ts);
    regel = fmt::format("[{}] {}<br>", buf, msg.c_str());
  } else {
    regel = fmt::format("{}<br>", msg.c_str());
  }
  eventLog += regel.c_str();
  int pos = 0, count = 0;
  while ((pos = eventLog.indexOf("<br>", pos + 1)) != -1) count++;
  if (count > maxLogLines) {
    eventLog = eventLog.substring(eventLog.indexOf("<br>") + 4);
  }
  pushLogToClients();
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
    std::vector<String> validRTCStatus = {"0", "1"};
    if (find(validRTCStatus.begin(), validRTCStatus.end(), myCfg.hasRTC) != validRTCStatus.end()) {
      f.println(myCfg.hasRTC);
    } else {
      f.println("0"); // Default to "0" if invalid
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
  parityRadioOptions = buildRadioHtml(parityOptions, "parity_group");

  // Build Stop Bits HTML Radio Buttons
  std::string stopBitsStr(myCfg.stopBits.c_str());
  selectByValue(stopBitOptions, stopBitsStr);
  stopBitRadioOptions = buildRadioHtml(stopBitOptions, "stopBits_group");

  // Build Data Bits HTML Radio Buttons
  std::string dataBitsStr(myCfg.dataBits.c_str());
  selectByValue(dataBitOptions, dataBitsStr);
  dataBitRadioOptions = buildRadioHtml(dataBitOptions, "dataBits_group");

  // Build Subject Indicator HTML Dropdown
  std::string subjectIndicatorToFilterStr(myCfg.subjectIndicatorToFilter.c_str());
  selectByValue(subjectIndicators, subjectIndicatorToFilterStr);
  htmlSubjectIndicatorDropdownOptions = buildDropdownHtml(subjectIndicators, "subjectIndicator_group");
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
    myCfg.hasRTC = f.readStringUntil('\n'); myCfg.hasRTC.trim();
    // For the moment, this version of myCfg does not store stationCodeToFilter nor subjectIndicatorToFilter
    myCfg.stationCodeToFilter = "T"; // Default to Oostende Radio
    myCfg.subjectIndicatorToFilter = "A"; // Default to Navigational Warnings
      // Build Subject Indicator HTML Dropdown
    std::string subjectIndicatorToFilterStr(myCfg.subjectIndicatorToFilter.c_str());
    selectByValue(subjectIndicators, subjectIndicatorToFilterStr);
    htmlSubjectIndicatorDropdownOptions = buildDropdownHtml(subjectIndicators, "subjectIndicator_group").c_str();
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

void setSystemTimeFromRTC() { // Synchroniseer systeemklok met RTC
  DateTime now = rtc.now();

  struct tm t;
  t.tm_year = now.year() - 1900;
  t.tm_mon  = now.month() - 1;
  t.tm_mday = now.day();
  t.tm_hour = now.hour();
  t.tm_min  = now.minute();
  t.tm_sec  = now.second();
  t.tm_isdst = -1;  // auto-detect DST

  time_t epoch = mktime(&t);
  struct timeval tv;
  tv.tv_sec = epoch;
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
}

// Cast wallclock time to global variable and update Html button representation
void castWallClockTime() {
  // Obtain proper clock face based on system time
  struct tm now_ts;
  getLocalTime(&now_ts);
  int y    = now_ts.tm_year + 1900;
  int m   = now_ts.tm_mon + 1;
  int d   = now_ts.tm_mday;
  int wallClockHour = now_ts.tm_hour;
  int wallClockMinute = now_ts.tm_min;
  int wallClockSecond = now_ts.tm_sec;

  std::string clockFace = getCurrentClockEmoji(wallClockHour, wallClockMinute); // Voorbeeld: 14:45
  std::string wallClockTime = fmt::format("{:02d}:{:02d}:{:02d}", wallClockHour, wallClockMinute, wallClockSecond);
  htmlDateTimeButton_UI = fmt::format("<div class='box'><button onclick='window.location=\"/getclock\"'> {} {}</button></div>", clockFace, wallClockTime);
  Serial.print("Casted wallClock: ");
  Serial.println(wallClockTime.c_str());
}

// --- WEB INTERFACE HTML ---
const String UI_SAFE_MODE_HEADER = "⚠️ SAFE MODE ACTIEF";
const String UI_SAFE_MODE_DESC = "Verbonden via Access Point (192.168.4.1)";
const String UI_TITLE = "ESP32-C3 SuperMini Bridge";
const String UI_NET_SETTINGS = "📶 Netwerk Instellingen";
const String UI_DEV_SETTINGS = " 🔧 Instellingen";
const String UI_CLOCK_SETTINGS = " ⏰ Klok Instellingen";
const String UI_UART_COMMAND = "📤 UART Command";
const String UI_SERIAL_MONITOR = "📟 Seriële Monitor";
const String UI_SAVE_BUTTON = "Opslaan & Herstarten";
const String UI_SEND_BUTTON = "Verzenden";
const String UI_PLACEHOLDER = "Typ bericht naar hardware...";
const String UI_RECEIVEHOLDER = "Ontvang berichten van Navtex...";
const String UI_CONFIG_BUTTON = "<div class='box'><button onclick='window.location=\"/device\"'>Wijzig</button></div>";
const String SAFE_MODE_BG_COLOR = "#fff0f0";
const String NORMAL_BG_COLOR = "#f4f7f6";

// Lees lettertekens totdat einde bereikt is
bool readUntilSequence(HardwareSerial& serial,
                       std::string& result,
                       const std::string stop)
{
  //const std::string stop = delimiter; // e.g. "end"
  if (!serial.available()) {
    return false; // No data available
  };
  int counter = 0;
  while (serial.available()) {
    int character = serial.read();
    result += static_cast<char>(character);

    if (result.length() >= stop.length() &&
        result.compare(result.length() - stop.length(), stop.length(), stop) == 0)
    {
      result.erase(result.length() - stop.length());
      return true;
    };
    if (result.length() > MAX_NAVTEX_BLOCK_SIZE) {
      result.clear();
      while (serial.available()) serial.read();
      addToLog("NAVTEX block exceeded maximum size and was discarded.", false);
      return false;
    }
    counter++;
    if (counter > 1000) { // Prevent infinite loop
      delay(1);
      counter = 0;
    }
  }
  return false;
};

// We create the HTML markup for listing Navtex messages in the web interface. Each message is displayed with its key and content, allowing users to view the details of each message received. The function iterates through the linked list of NavtexMessage objects and prints their information to the serial monitor for debugging purposes.
void createNavtexMessageDisplayList() {
  std::string myCfgFilter = fmt::format("{0}{1}", myCfg.stationCodeToFilter, myCfg.subjectIndicatorToFilter).c_str();
  NavtexMessage* current = head;

  while (current) {
    std::string thisFilter = current->messageKey.substr(0, 2); // Extract station code from message key
    char thisSubject = current->messageKey[1]; // Extract subject indicator from message key 
    if (myCfgFilter.compare(thisFilter) == 0 || thisSubject == char('D')) {
      Serial.printf("NAVTEX Message Key: %s\n", current->messageKey.c_str());
      Serial.printf("NAVTEX Message Content:\n%s", current->raw.c_str());
    };
    current = current->next;
  }
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  (void)server;
  (void)client;
  (void)arg;
  (void)data;
  (void)len;

  switch (type) {
    case WS_EVT_CONNECT:
      client->printf("%s\n", eventLog.c_str());
      break;
    case WS_EVT_DISCONNECT:
      break;
    case WS_EVT_DATA:
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
    default:
      break;
  }
}

void createHtmlTemplates() {
castWallClockTime(); // Update global variable with current time and clock emoji, sets htmlDateTimeButton_UI

htmlHeaderString = "<!DOCTYPE html><html lang=nl><head><title>C3 Bridge</title><meta name='viewport' charset='UTF-8' content='width=device-width, initial-scale=1'>";
  // WebSocket Script
htmlUploadLogFunction = R"rawliteral(
<script>
const logBox = document.getElementById('lb');
const scrollToBottom = () => {
  if (!logBox) return;
  const isNearBottom = logBox.scrollHeight - logBox.clientHeight <= logBox.scrollTop + 50;
  if (isNearBottom) logBox.scrollTop = logBox.scrollHeight;
};
const protocol = (location.protocol === 'https:') ? 'wss' : 'ws';
const socket = new WebSocket(protocol + '://' + location.host + '/ws');
socket.onopen = () => console.log('WebSocket connected');
socket.onmessage = (event) => {
  if (!logBox) return;
  logBox.innerHTML = event.data;
  scrollToBottom();
};
socket.onclose = () => console.log('WebSocket closed');
socket.onerror = () => console.log('WebSocket error');
</script></div>
)rawliteral";
  // CSS
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
.custom-check {
  display: none;
}

/* Standaard stijl van de 'knop' (het label) */
.knop-stijl {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 10px 20px;
  background-color: #f0f0f0;
  border: 2px solid #ccc;
  cursor: pointer;
  border-radius: 5px;
  transition: all 0.3s;
}

/* Symbool voor de checkbox (unchecked) */
.status-icon::before {
  content: "☐";
  margin-right: 8px;
}

/* Het element dat we willen verbergen */
.content {
  display: none;
  margin-top: 20px;
  padding: 15px;
  background-color: #e7f3fe;
  border-left: 6px solid #2196F3;
}

/* DE MAGIE: Als de checkbox GECHECKED is */
.custom-check:checked + .knop-stijl {
  background-color: #4CAF50;
  color: white;
  border-color: #388E3C;
}

.custom-check:checked + .knop-stijl .status-icon::before {
  content: "☑";
}

.custom-check:checked ~ .content {
  display: block;
}

</style>
</head><body>
  )rawliteral";

     if(safeModeActive) {
       htmlSafeModeHeader = "<h2 style='color:red;'>" + UI_SAFE_MODE_HEADER + "</h2><p>" + UI_SAFE_MODE_DESC + "</p>";
       UI_RECEIVE_BUTTON = "<div class='btn'>Receive</div>";
     } else {
       htmlSafeModeHeader = "<h2>" + UI_TITLE + "</h2>";
       UI_RECEIVE_BUTTON = "<div class='box'><button onclick='window.location=\"/receive\"'>Ontvang</button></div>";
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
    <div class='box'><button onclick='window.location=\"/"'>Hoofdmenu</button></div>
    </div>
  </form>
</div>
)rawliteral";

htmlDeviceHeader = R"rawliteral(
<div class='container'><div class='box'>
  <h3>)rawliteral" + UI_DEV_SETTINGS + R"rawliteral(</h3>
  <!-- Toggle: Instellingen -->
  <input type="checkbox" id="instToggle" class="custom-check">
  <label for="instToggle" class="knop-stijl">
    <span class="status-icon"></span>
    Uitklappen
  </label>
  <div class="content"><p>
  <table><tbody><tr><th>Configuratie</th></tr>
    <tr><td>)rawliteral"+ htmlDateTimeButton_UI.c_str() + R"rawliteral(</td><td>Versie:</td><td>)rawliteral" + myCfg.version + R"rawliteral(</td></tr>
    <tr><td>Communicatie:</td><td>BaudRate:</td><td>)rawliteral" + String(myCfg.baud) + R"rawliteral(</td></tr>
    <tr><td></td><td>Data Bits:</td><td>)rawliteral" + myCfg.dataBits + R"rawliteral(</td></tr>
    <tr><td></td><td>Parity:</td><td>)rawliteral" + myCfg.parity + R"rawliteral(</td></tr>
    <tr><td></td><td>Stop Bits:</td><td>)rawliteral" + myCfg.stopBits + R"rawliteral(</td></tr>
    </tbody>
  </table>)rawliteral" + UI_CONFIG_BUTTON + R"rawliteral(</p>
</div>
</div>
)rawliteral";

  // UART Send
  htmlUartSend = R"rawliteral(
<div class='container'><div class='box'>
  <h3>)rawliteral" + UI_UART_COMMAND + R"rawliteral(</h3>
  <!-- Toggle: UART Command -->
  <input type="checkbox" id="uartToggle" class="custom-check">
  <label for="uartToggle" class="knop-stijl">
    <span class="status-icon"></span>
    Toon UART opties
  </label>
  <div class="content"><p>Command:
  <form action='/write' method='POST'>
    <label><input type='text' name='t' id='command' placeholder=')rawliteral" + UI_PLACEHOLDER + R"rawliteral('></label>
    <input type='submit' class='btn' value=')rawliteral" + UI_SEND_BUTTON + R"rawliteral('>
  </form>
  </p></div>
</div>
)rawliteral";

  // UART Receive
  htmlUartReceive = R"rawliteral(
<div class='container'>
  <div class='box'>
    <p>
      <form action='/receive' method='POST'>
        <input type='submit' class='btn' value='Ontvangen'>
      </form>
    </p>
  </div>
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
    };

void readNavtexText() {
  if (readUntilSequence(Serial1, textBlock, "end")) {
  // NAVTEX messages always start with '>'
    uint32_t  currentHash = fnv1aHash(textBlock);
    addToLog("We got the block of text", false);
    if (currentHash != lastHash) { // we have a text block that is different from the last one
      size_t pos = 0;
      size_t start;
      size_t end;
      size_t blockSize = textBlock.size(); // efficiënter zo om de lengte maar één keer te berekenen
      singleMsg.clear();
      while (pos < blockSize) {
        start = textBlock.find('>', pos);
        if (start == std::string::npos) break;   // no more starts

        end = textBlock.find('<', start + 1);
        if (end == std::string::npos) break;     // no matching end

        // Keep everything between > and <, including the delimiters
        std::string singleMsg = textBlock.substr(start, end - start + 1);

        std::string thisStation = extractStation(singleMsg);
        std::string thisSubject = extractSubjectIndicator(singleMsg);
        std::string thisKey = extractMessageKey(singleMsg);
        // De logica om de binnenkomende berichten te filteren op basis van station en subject indicator gebeurt elders
        delay(1);
        addToLog("Adding NAVTEX message: ", false);
        addNavtexMessage(head, singleMsg, thisKey);
        Serial.printf(
          "Filtered out message from station: %s with subject: %s\n",
          thisStation.c_str(),
          thisSubject.c_str()
        );
        
        addToLog("Filtered out message from station", false);
        Serial.println("Msg:");
        Serial.println(singleMsg.c_str());

        pos = end + 1;                      // continue after this block
      };
    };
    lastHash = currentHash;
    textBlock.clear();
    Serial.println("NAVTEX message received and processed.");
  } else {
    delay(10);
  }
}

void handleRoot(AsyncWebServerRequest *request) {
  serverOutput.clear(); // Clear buffer before generating new output
  createHtmlTemplates(); // Regenerate templates to update dynamic content like clock emoji
  if (safeModeActive) {
    fmt::format_to(std::back_inserter(serverOutput), "{}{}{}{}{}{}{}{}", htmlHeaderString, htmlCSS, htmlSafeModeHeader, htmlWifiConfig, htmlUartSend, htmlUartReceive, htmlLog, htmlUploadLogFunction);
  } else {
    fmt::format_to(std::back_inserter(serverOutput), "{}{}{}{}{}{}{}{}", htmlHeaderString, htmlCSS, htmlSafeModeHeader, htmlDeviceHeader, htmlUartSend, htmlUartReceive, htmlLog, htmlUploadLogFunction);
  }
  request->send(200, "text/html; charset=UTF-8", serverOutput.c_str());
}

// Herstart
void systemConfig() {
  Serial.println("System config by button push...");
  addToLog("System config by button push...");
  safeModeActive = true;
  delay(500);
}

// --- SETUP ---
void setup() {
  setenv("TZ", "CET-1CEST,M3.5.0/02:00:00,M10.5.0/03:00:00", 1);
  tzset();

  Serial.begin(115200); // USB-CDC
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

  if (!LittleFS.begin(true)) {
    delay(1000);
    Serial.println("FS Fout");
    return;
  }

  loadFromFlash(); // Laad configuratie van flash, vult myCfg en bouwt HTML componenten op basis van configuratie
  // RTC Initialisatie
  Wire.begin(SDA, SCL);
  Wire.beginTransmission(RTC_DS3231_ADDRESS);
  byte RTC_endTransmission = Wire.endTransmission();
  bool RTC_status = (RTC_endTransmission == 0) ? true : false; // 0 = RTC gevonden, 1 of andere waarde = RTC niet gevonden
  if (!RTC_status) {
    Serial.println("RTC niet gevonden op I2C bus");
    addToLog("RTC niet gevonden op I2C bus");
  } else {
    Serial.println("RTC gevonden op I2C bus");
    addToLog("RTC gevonden op I2C bus");
  }
  signalHardwareChange = false; // Variabele om aan te geven dat er nieuwe hardware is gedetecteerd, kan later worden gebruikt voor notificaties
  if (myCfg.hasRTC == "1" && !RTC_status) {
    // Als de configuratie aangeeft dat er een RTC is, maar we kunnen deze niet bereiken, log dan een fout
    Serial.println("Configuratie geeft aan dat er een RTC is, maar deze is niet bereikbaar.");
    addToLog("Configuratie geeft aan dat er een RTC is, maar deze is niet bereikbaar.");
    signalHardwareChange = true; // We geven aan dat de hardware niet meer bereikbaar is
  } else if (myCfg.hasRTC == "0" && RTC_status) {
    // Als de configuratie aangeeft dat er geen RTC is, maar we kunnen er wel een bereiken, log dan een waarschuwing
    Serial.println("Nieuw apparaat (RTC) gevonden.)");
    addToLog("Nieuw apparaat (RTC) gevonden.");
    signalHardwareChange = true; // Deze variabele kan later worden gebruikt om gebruikers te informeren over nieuwe hardware detectie, bijvoorbeeld via een melding op de webinterface
  }
  myCfg.hasRTC = (RTC_status) ? "1" : "0"; // Update configuratie op basis van RTC detectie

  if (!rtc.begin()) {
    Serial.println("RTC niet gevonden");
    addToLog("RTC niet gevonden");
    myCfg.hasRTC = "0"; // Update configuratie alsnog RTC niet gevonden

  } else {
    if (rtc.lostPower()) {
      Serial.println("RTC heeft stroom verloren, tijd opnieuw instellen.");
      addToLog("RTC heeft stroom verloren, tijd opnieuw instellen.");
      DateTime fallBackTime = DateTime(F(__DATE__), F(__TIME__)); // Compileertijd als fallback
      rtc.adjust(fallBackTime);
    }
    setSystemTimeFromRTC(); // Zet systeemklok direct bij opstarten
    struct tm t;
    if (!getLocalTime(&t)) {
      Serial.println("RTC tijd niet gesynchroniseerd");
      addToLog("RTC tijd niet gesynchroniseerd");
    }
  }
  if (signalHardwareChange) {
    saveToFlash(); // Sla de bijgewerkte configuratie op als er een verandering in hardware status is gedetecteerd
    signalHardwareChange = false; // Reset de hardware change indicator
  }
  
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

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  // Routes
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    handleRoot(request);
  });
  server.on("/getlog", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", eventLog);
  });
  server.on("/savecfg", HTTP_POST, [](AsyncWebServerRequest *request) {
    myCfg.ssid = request->arg("s"); // Lees ssid van formulier
    String passFromForm = request->arg("p"); // Lees password van formulier
    if(passFromForm == "/\\/\\") {
      // Als het wachtwoord veld ongewijzigd is (placeholder), behoud dan het oude wachtwoord
      Serial.println("Wachtwoord ongewijzigd, behoud huidig wachtwoord.");
    } else {
       myCfg.pass = passFromForm; // Lees password van formulier
    }
    myCfg.mdns = request->arg("m"); // Lees mDNS van formulier
    String baudAsString = request->arg("b"); // Lees baudrate van formulier
    myCfg.baud = baudAsString.toInt(); // Converteer naar integer
    myCfg.parity = request->arg("parity_group").c_str(); // Lees parity_group van formulier
    myCfg.stopBits = request->arg("stopBits_group").c_str(); // Lees stopBits_group van formulier
    myCfg.dataBits = request->arg("dataBits_group").c_str(); // Lees dataBits_group van formulier
    Serial.println("parityBits van portaal: " + String(myCfg.parity.c_str()));
    Serial.println("stopBits van portaal: " + String(myCfg.stopBits.c_str()));
    Serial.println("dataBits van portaal: " + String(myCfg.dataBits.c_str()));
    safeModeActive = false; // Zet safe mode uit bij opslaan van nieuwe config

    saveToFlash();
    createHtmlTemplates();
    addToLog("Nieuwe configuratie opgeslagen. Herstarten...");
    Serial.println("Nieuwe configuratie opgeslagen. Herstarten...");
    request->redirect("/");
    Serial1.begin(myCfg.baud, SERIAL_8N1, UART1_RX, UART1_TX);
    delay(2000);
    ESP.restart();
  });
  server.on("/write", HTTP_POST, [](AsyncWebServerRequest *request) {
    if(request->hasArg("t")){ 
      String msg = request->arg("t");
      Serial1.println(msg); 
      addToLog("<b>WEB-OUT:</b> " + msg);
    }
    request->redirect("/");
  });
  server.on("/receive", HTTP_POST, [](AsyncWebServerRequest *request) {
    isParsing = true; 
    String msg = "$S";
    Serial1.println(msg);
    addToLog("<b>WEB-OUT:</b> " + msg);
    request->redirect("/");
  });
  server.on("/getclock", HTTP_GET, [](AsyncWebServerRequest *request) {
    serverOutput.clear();
    // Obtain proper clock face based on system time
    struct tm now_ts;
    getLocalTime(&now_ts);
    int y = now_ts.tm_year + 1900;
    int m = now_ts.tm_mon + 1;
    int d = now_ts.tm_mday;
    int wallClockHour = now_ts.tm_hour;
    int wallClockMinute = now_ts.tm_min;
    int wallClockSecond = now_ts.tm_sec;
 
    // Formatteer de huidige tijd zodat de picker deze als 'default' herkent
    std::string datum = fmt::format("{:04d}-{:02d}-{:02d}", y, m, d);
    std::string tijd = fmt::format("{:02d}:{:02d}", wallClockHour, wallClockMinute);
    String htmlGetTimeDateForm = R"rawliteral(
<div class='container'><div class='box'>
  <h3>)rawliteral" + UI_CLOCK_SETTINGS + R"rawliteral(</h3>
    <form action='/setclock' method='POST'>
    Kies datum & tijd: <br><br>
      <input type="date" id="date" required placeholder='")rawliteral" + datum.c_str() + R"rawliteral('>
      <span>@</span>
      <input type="time" id="time" step="60" required placeholder='")rawliteral" + tijd.c_str() + R"rawliteral('>
    <input id='dt' name="dt" type="hidden">
    <br><br><button type='submit' class='btn' value='Opslaan'></button></form>
<script>
  function updateDT() {
      const d = document.getElementById("date").value;   // yyyy-mm-dd
      const t = document.getElementById("time").value;   // hh:mm
      if (d && t) {
          const iso = `${d}T${t}`;
          document.getElementById("dt").value = iso;
      }
  }
  document.getElementById("date").addEventListener("input", updateDT);
  document.getElementById("time").addEventListener("input", updateDT);
</script>
    </div><div class='box'><button onclick="window.location='/'">Hoofdmenu</button></div>)rawliteral";
    
    fmt::format_to(std::back_inserter(serverOutput), "{}{}{}{}{}{}{}", htmlHeaderString, htmlCSS, htmlSafeModeHeader, htmlGetTimeDateForm, htmlUartSend, htmlLog, htmlUploadLogFunction);
    request->send(200, "text/html; charset=UTF-8", serverOutput.c_str());
  });
  server.on("/setclock", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasArg("dt")) {
      String dt = request->arg("dt"); // Voorbeeld: "2023-10-27T14:30"
      // We gebruiken .substring of sscanf om de string te ontleden
      // Formaat: 0123 4 56 7 89 0 12 3 45
      //          YYYY - MM - DD T HH : MM
      int j   = dt.substring(0, 4).toInt();
      int m   = dt.substring(5, 7).toInt();
      int d   = dt.substring(8, 10).toInt();
      int hh  = dt.substring(11, 13).toInt();
      int mm  = dt.substring(14, 16).toInt();

      // 1. Update RTC
      if (myCfg.hasRTC == "1") {
        rtc.adjust(DateTime(j, m, d, hh, mm, 0));
        // 2. Update Systeemklok
        setSystemTimeFromRTC();

      } else {
        // Systeemtijd bijwerken, maar deze zal verloren gaan bij herstart
        struct tm t;
        t.tm_year = j - 1900;   // years since 1900
        t.tm_mon  = m - 1;      // 0–11
        t.tm_mday = d;          // 1–31
        t.tm_hour = hh;         // 0–23
        t.tm_min  = mm;         // 0–59
        t.tm_sec  = 0;
        t.tm_isdst = -1;  // auto-detect DST

        struct timeval tv;
        tv.tv_sec = mktime(&t);
        tv.tv_usec = 0; 
        settimeofday(&tv, nullptr); // timeLib allows this
      }
      addToLog("Klok bijgewerkt naar: " + dt);
      Serial.println("Klok bijgewerkt naar: " + dt);
      request->redirect("/getclock");
    } else {
      addToLog("Foutieve klok parameters ontvangen.");
      Serial.println("Foutieve klok parameters ontvangen.");
      request->redirect("/");
    }
  });
  server.on("/device", HTTP_GET, [](AsyncWebServerRequest *request) {
    serverOutput.clear(); // Clear buffer before generating new output
    fmt::format_to(std::back_inserter(serverOutput), "{}{}{}{}{}{}{}", htmlHeaderString, htmlCSS, htmlSafeModeHeader, htmlWifiConfig, htmlUartSend, htmlLog, htmlUploadLogFunction);
    request->send(200, "text/html; charset=UTF-8", serverOutput.c_str());
  });
  // Serve favicon
  server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!LittleFS.exists("/favicon.ico")) {
      request->send(404, "text/plain", "Favicon not found");
      return;
    }
    request->send(LittleFS, "/favicon.ico", "image/x-icon");
  });

  server.begin();
}

// --- MAIN LOOP ---
void loop() {
  // Wacht even
  delay(10);
  serviceLogUpdates();
  // UART1 naar WEB/USB
  if (!isParsing) {
    if (Serial1.available()) {
      serial1In = Serial1.readStringUntil('\n');
      serial1In.trim();
      Serial.println("UART: " + serial1In);
      addToLog("<b>UART:</b> " + serial1In);
    }
  } else {
    readNavtexText();
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
  // Echo ook USB naar het log
  if (Serial.available()) {
    usb = Serial.readStringUntil('\n');
    usb.trim();
    Serial.println(usb);
    addToLog("<b>USB-IN:</b> " + usb);
    Serial1.println(usb);
  }
}