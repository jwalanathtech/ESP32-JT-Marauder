// JwalaTech RF jammer for wifi and bluetooth
#include "RF24.h"
#include <SPI.h>
#include "esp_bt.h"
#include "esp_wifi.h"
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>

// --- TFT ST7735 Pin Definitions (Software SPI) ---
#define TFT_SCK   17  // SCK
#define TFT_SDA   4   // SDA (MOSI)
#define TFT_CS    25  // CS
#define TFT_DC    26  // DC / A0
#define TFT_RST   27  // RST

// --- Control Pin Definitions ---
#define BTN_TOGGLE  32  // Joystick SW pin (or GPIO 33 / GPIO 5)

// Initialize TFT Display using Software SPI
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_SDA, TFT_SCK, TFT_RST);

// --- NRF24 Pins ---
// HSPI: SCK=14, MISO=12, MOSI=13, CSN=15, CE=16
// VSPI: SCK=18, MISO=19, MOSI=23, CSN=21, CE=22
RF24 radio(16, 15, 16000000);   // HSPI radio
RF24 radio1(22, 21, 16000000);  // VSPI radio

SPIClass *hp = nullptr; // HSPI bus
SPIClass *sp = nullptr; // VSPI bus

bool systemActive = false;
bool hpInitSuccess = false;
bool spInitSuccess = false;

// Button debounce tracking
bool lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

int ch = 45;   // Channel HP
int ch1 = 45;  // Channel SP

void drawUI() {
  tft.fillScreen(ST7735_BLACK);
  tft.drawRect(2, 2, 124, 156, ST7735_WHITE);
  
  tft.setCursor(12, 12);
  tft.setTextColor(ST7735_CYAN);
  tft.setTextSize(1);
  tft.println("RF CONTROLLER");

  // Status Indicator Box
  if (systemActive) {
    tft.fillRect(10, 30, 108, 30, ST7735_GREEN);
    tft.setCursor(22, 38);
    tft.setTextColor(ST7735_BLACK);
    tft.setTextSize(2);
    tft.println("ACTIVE");
  } else {
    tft.fillRect(10, 30, 108, 30, ST7735_RED);
    tft.setCursor(18, 38);
    tft.setTextColor(ST7735_WHITE);
    tft.setTextSize(2);
    tft.println("STOPPED");
  }

  // Details Display
  tft.setTextSize(1);
  tft.setTextColor(ST7735_YELLOW);
  tft.setCursor(10, 75);
  tft.print("HSPI (nRF1): ");
  tft.println(systemActive && hpInitSuccess ? "ACTIVE" : "OFF");

  tft.setCursor(10, 95);
  tft.print("VSPI (nRF2): ");
  tft.println(systemActive && spInitSuccess ? "ACTIVE" : "OFF");

  tft.setTextColor(ST7735_WHITE);
  tft.setCursor(8, 125);
  tft.println("Press SW/Button");
  tft.setCursor(8, 137);
  tft.println("to Toggle Radios");
}

// Initial hardware configuration (Run once in setup)
void setupRadios() {
  // HSPI Radio Setup
  hp = new SPIClass(HSPI);
  hp->begin(14, 12, 13, 15); // SCK, MISO, MOSI, SS
  if (radio.begin(hp)) {
    Serial.println("HP Started Successfully!");
    radio.setAutoAck(false);
    radio.stopListening();
    radio.setRetries(0, 0);
    radio.setPALevel(RF24_PA_MAX, true);
    radio.setDataRate(RF24_2MBPS);
    radio.setCRCLength(RF24_CRC_DISABLED);
    radio.powerDown(); // Start in powered-down standby mode
    hpInitSuccess = true;
  } else {
    Serial.println("HP Initialization Failed!");
    hpInitSuccess = false;
  }

  // VSPI Radio Setup
  sp = new SPIClass(VSPI);
  sp->begin(18, 19, 23, 21); // SCK, MISO, MOSI, SS
  if (radio1.begin(sp)) {
    Serial.println("SP Started Successfully!");
    radio1.setAutoAck(false);
    radio1.stopListening();
    radio1.setRetries(0, 0);
    radio1.setPALevel(RF24_PA_MAX, true);
    radio1.setDataRate(RF24_2MBPS);
    radio1.setCRCLength(RF24_CRC_DISABLED);
    radio1.powerDown(); // Start in powered-down standby mode
    spInitSuccess = true;
  } else {
    Serial.println("SP Initialization Failed!");
    spInitSuccess = false;
  }
}

// Start continuous carrier transmission without re-initializing SPI
void startRadios() {
  if (hpInitSuccess) {
    radio.powerUp();
    delay(5);
    radio.startConstCarrier(RF24_PA_MAX, ch);
  }
  if (spInitSuccess) {
    radio1.powerUp();
    delay(5);
    radio1.startConstCarrier(RF24_PA_MAX, ch1);
  }
  Serial.println("Radios Transmission Started");
}

// Safely stop continuous carrier transmission
void stopRadios() {
  if (hpInitSuccess) {
    radio.stopConstCarrier();
    radio.powerDown();
  }
  if (spInitSuccess) {
    radio1.stopConstCarrier();
    radio1.powerDown();
  }
  Serial.println("Radios Powered Down");
}

void one() {
  if (!systemActive) return;

  // Random Channel Hopping during active carrier mode
  if (hpInitSuccess) radio.setChannel(random(80));
  if (spInitSuccess) radio1.setChannel(random(80));
  delayMicroseconds(random(60));
}

void checkButton() {
  int reading = digitalRead(BTN_TOGGLE);

  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    static bool buttonPressed = false;
    if (reading == LOW && !buttonPressed) {
      buttonPressed = true;
      systemActive = !systemActive; // Toggle state

      if (systemActive) {
        startRadios();
      } else {
        stopRadios();
      }
      drawUI();
    } else if (reading == HIGH) {
      buttonPressed = false;
    }
  }

  lastButtonState = reading;
}

void setup() {
  Serial.begin(115200);

  // Disable WiFi and Bluetooth for power efficiency
  esp_bt_controller_deinit();
  esp_wifi_stop();
  esp_wifi_deinit();
  esp_wifi_disconnect();

  pinMode(BTN_TOGGLE, INPUT_PULLUP);

  // Initialize TFT Display
  tft.initR(INITR_BLACKTAB); 
  tft.setRotation(0);
  
  // Setup nRF24 modules once
  setupRadios();

  // Draw Initial Screen
  drawUI();
}

void loop() {
  checkButton();

  if (systemActive) {
    one();
  }
}
