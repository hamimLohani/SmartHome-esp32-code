#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <DHT.h>
#include <LiquidCrystal_I2C.h>
#include <Firebase_ESP_Client.h>

#define WIFI_SSID      "Black Box"
#define WIFI_PASSWORD  "boltechitham"
// #define WIFI_SSID      "Tp Link 2.4 GHz"
// #define WIFI_PASSWORD  "flat_6b@"
#define FIREBASE_HOST  "smart-home-122-default-rtdb.firebaseio.com"
#define FIREBASE_AUTH  "mFawsZRgC0dEaQE1VcIyBQeTHV1TVzZlmnNF448Q"

// ESP32 DevKit wiring.
// DHT11/DHT22: VCC → 3V3, GND → GND, DATA → GPIO4.
// 16x2 I2C LCD: VCC → VIN/5V, GND → GND, SDA → GPIO21, SCL → GPIO22.
// Relay module inputs: Light 1 → GPIO25, Light 2 → GPIO26, Fan → GPIO27.
// SN74HC595N shift register: SER → GPIO13, SRCLK → GPIO14, RCLK → GPIO12.
#define PIN_DHT        4
#define PIN_RELAY_L1   25
#define PIN_RELAY_L2   26
#define PIN_RELAY_FAN  27
#define PIN_I2C_SDA    21
#define PIN_I2C_SCL    22
#define PIN_SR_SER     13  // SN74HC595 SER (data)
#define PIN_SR_SRCLK   14  // SN74HC595 SRCLK (shift clock)
#define PIN_SR_RCLK    12  // SN74HC595 RCLK (latch clock)

// SN74HC595 output mapping:
// Q0: TEMP_LOW, Q1: TEMP_HIGH, Q2-Q11: TBAR_1-10, Q12: HUM_LOW, Q13: HUM_HIGH, Q14-Q18: HBAR_1-5
#define SR_TEMP_LOW    (1 << 0)
#define SR_TEMP_HIGH   (1 << 1)
#define SR_TBAR_1      (1 << 2)
#define SR_TBAR_2      (1 << 3)
#define SR_TBAR_3      (1 << 4)
#define SR_TBAR_4      (1 << 5)
#define SR_TBAR_5      (1 << 6)
#define SR_TBAR_6      (1 << 7)
#define SR_TBAR_7      (1 << 8)
#define SR_TBAR_8      (1 << 9)
#define SR_TBAR_9      (1 << 10)
#define SR_TBAR_10     (1 << 11)
#define SR_HUM_LOW     (1 << 12)
#define SR_HUM_HIGH    (1 << 13)
#define SR_HBAR_1      (1 << 14)
#define SR_HBAR_2      (1 << 15)
#define SR_HBAR_3      (1 << 16)
#define SR_HBAR_4      (1 << 17)
#define SR_HBAR_5      (1 << 18)

#define DHTTYPE DHT11
#define LCD_ADDRESS 0x27
#define LCD_COLUMNS 16
#define LCD_ROWS 2

#define SENSOR_PERIOD_MS 2000UL
#define FIREBASE_PERIOD_MS 500UL
#define DISPLAY_PERIOD_MS 1000UL
#define WIFI_RETRY_PERIOD_MS 10000UL
#define FIRMWARE_VERSION "SmartHome-DevKit-1.0.0"

const char *DB_ROOT = "/smarthome";

DHT dht(PIN_DHT, DHTTYPE);
LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLUMNS, LCD_ROWS);
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

float currentTemperature = NAN;
float currentHumidity = NAN;
bool light1On = false;
bool light2On = false;
bool fanOn = false;
unsigned long lastSensorMs = 0;
unsigned long lastFirebaseMs = 0;
unsigned long lastDisplayMs = 0;
unsigned long lastWifiRetryMs = 0;
uint32_t srData = 0;  // Current shift register data

String dbPath(const char *suffix) {
  return String(DB_ROOT) + suffix;
}

void writeShiftRegister() {
  digitalWrite(PIN_SR_RCLK, LOW);
  for (int i = 23; i >= 0; i--) {  // Send 24 bits (3 bytes)
    digitalWrite(PIN_SR_SER, (srData >> i) & 1);
    digitalWrite(PIN_SR_SRCLK, HIGH);
    digitalWrite(PIN_SR_SRCLK, LOW);
  }
  digitalWrite(PIN_SR_RCLK, HIGH);
}

bool firebaseReady() {
  return WiFi.status() == WL_CONNECTED && Firebase.ready();
}

void writeRelay(uint8_t pin, bool on) {
  // Most relay boards are active LOW: LOW energizes relay, HIGH turns it off.
  digitalWrite(pin, on ? LOW : HIGH);
}

void applyOutputs() {
  writeRelay(PIN_RELAY_L1, light1On);
  writeRelay(PIN_RELAY_L2, light2On);
  writeRelay(PIN_RELAY_FAN, fanOn);
}

void updateTempLEDs(float temperature) {
  bool low = temperature < 20.0f;
  bool high = temperature > 40.0f;
  
  // Clear temperature-related bits
  srData &= ~(SR_TEMP_LOW | SR_TEMP_HIGH | 
              SR_TBAR_1 | SR_TBAR_2 | SR_TBAR_3 | SR_TBAR_4 | SR_TBAR_5 |
              SR_TBAR_6 | SR_TBAR_7 | SR_TBAR_8 | SR_TBAR_9 | SR_TBAR_10);
  
  // Set temperature indicator bits
  if (low) srData |= SR_TEMP_LOW;
  if (high) srData |= SR_TEMP_HIGH;
  
  // Set temperature bar graph bits
  const uint32_t tbarBits[] = { SR_TBAR_1, SR_TBAR_2, SR_TBAR_3, SR_TBAR_4, SR_TBAR_5,
                               SR_TBAR_6, SR_TBAR_7, SR_TBAR_8, SR_TBAR_9, SR_TBAR_10 };
  for (int i = 0; i < 10; i++) {
    float threshold = 20.0f + (i * 2.0f);
    if (temperature >= threshold) {
      srData |= tbarBits[i];
    }
  }
  
  writeShiftRegister();
}

void updateHumidityLEDs(float humidity) {
  bool low = humidity < 60.0f;
  bool high = humidity > 80.0f;
  
  // Clear humidity-related bits
  srData &= ~(SR_HUM_LOW | SR_HUM_HIGH | 
              SR_HBAR_1 | SR_HBAR_2 | SR_HBAR_3 | SR_HBAR_4 | SR_HBAR_5);
  
  // Set humidity indicator bits
  if (low) srData |= SR_HUM_LOW;
  if (high) srData |= SR_HUM_HIGH;
  
  // Set humidity bar graph bits
  const uint32_t hbarBits[] = { SR_HBAR_1, SR_HBAR_2, SR_HBAR_3, SR_HBAR_4, SR_HBAR_5 };
  for (int i = 0; i < 5; i++) {
    float threshold = 60.0f + (i * 4.0f);
    if (humidity >= threshold) {
      srData |= hbarBits[i];
    }
  }
  
  writeShiftRegister();
}

void setupOutputs() {
  // Setup relay pins
  pinMode(PIN_RELAY_L1, OUTPUT);
  pinMode(PIN_RELAY_L2, OUTPUT);
  pinMode(PIN_RELAY_FAN, OUTPUT);

  // Setup shift register pins
  pinMode(PIN_SR_SER, OUTPUT);
  pinMode(PIN_SR_SRCLK, OUTPUT);
  pinMode(PIN_SR_RCLK, OUTPUT);

  light1On = false;
  light2On = false;
  fanOn = false;
  srData = 0;  // Clear all LEDs
  applyOutputs();
  writeShiftRegister();
}

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting WiFi");
  for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi connected: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi unavailable; local display and DHT continue.");
  }
}

void setupFirebase() {
  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.reconnectWiFi(true);
  Firebase.begin(&config, &auth);
}

void setupLcd() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SmartHome Ready");
  lcd.setCursor(0, 1);
  lcd.print("DevKit + DHT");
}

void publishDefaults() {
  if (!firebaseReady()) {
    return;
  }

  Firebase.RTDB.setBool(&fbdo, dbPath("/controls/led1"), light1On);
  Firebase.RTDB.setBool(&fbdo, dbPath("/controls/led2"), light2On);
  Firebase.RTDB.setBool(&fbdo, dbPath("/controls/fan"), fanOn);
  Firebase.RTDB.setString(&fbdo, dbPath("/status/ip"), WiFi.localIP().toString());
  Firebase.RTDB.setString(&fbdo, dbPath("/status/firmware"), FIRMWARE_VERSION);
  Firebase.RTDB.setInt(&fbdo, dbPath("/status/boot_ms"), millis());
}

void readDhtSensor() {
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();

  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("DHT read failed.");
    return;
  }

  currentTemperature = temperature;
  currentHumidity = humidity;

  // Update LEDs via shift register
  updateTempLEDs(temperature);
  updateHumidityLEDs(humidity);

  Serial.print("Temp: ");
  Serial.print(currentTemperature, 1);
  Serial.print(" C, Humidity: ");
  Serial.print(currentHumidity, 0);
  Serial.println(" %");

  if (firebaseReady()) {
    if (!Firebase.RTDB.setFloat(&fbdo, dbPath("/sensors/temperature"), currentTemperature)) {
      Serial.println("Temp write failed: " + fbdo.errorReason());
    }
    if (!Firebase.RTDB.setFloat(&fbdo, dbPath("/sensors/humidity"), currentHumidity)) {
      Serial.println("Hum write failed: " + fbdo.errorReason());
    }
    if (!Firebase.RTDB.setTimestamp(&fbdo, dbPath("/status/last_updated"))) {
      Serial.println("Timestamp write failed: " + fbdo.errorReason());
    } else {
      Serial.println("Firebase updated successfully.");
    }
  }
}

bool readControl(const char *path, bool fallback) {
  if (!firebaseReady()) {
    return fallback;
  }

  if (!Firebase.RTDB.getBool(&fbdo, dbPath(path))) {
    return fallback;
  }

  return fbdo.boolData();
}

void syncControlsFromFirebase() {
  if (!firebaseReady()) {
    return;
  }

  light1On = readControl("/controls/led1", light1On);
  light2On = readControl("/controls/led2", light2On);
  fanOn = readControl("/controls/fan", fanOn);
  applyOutputs();
}

void updateDisplay() {
  char line[17];

  lcd.setCursor(0, 0);
  if (isnan(currentTemperature) || isnan(currentHumidity)) {
    snprintf(line, sizeof(line), "T:--.-C H:--%%   ");
  } else {
    snprintf(line, sizeof(line), "T:%4.1fC H:%2.0f%%  ", currentTemperature, currentHumidity);
  }
  lcd.print(line);

  lcd.setCursor(0, 1);
  snprintf(
    line,
    sizeof(line),
    "L1:%s L2:%s F:%s",
    light1On ? "1" : "0",
    light2On ? "1" : "0",
    fanOn ? "1" : "0"
  );
  lcd.print(line);
}

void retryWiFiIfNeeded() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  unsigned long now = millis();
  if (now - lastWifiRetryMs < WIFI_RETRY_PERIOD_MS) {
    return;
  }

  lastWifiRetryMs = now;
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void setup() {
  setupOutputs();
  Serial.begin(115200);
  delay(100);

  dht.begin();
  setupLcd();
  connectWiFi();
  setupFirebase();
  publishDefaults();
}

void loop() {
  unsigned long now = millis();

  retryWiFiIfNeeded();

  if (now - lastSensorMs >= SENSOR_PERIOD_MS) {
    lastSensorMs = now;
    readDhtSensor();
  }

  if (now - lastFirebaseMs >= FIREBASE_PERIOD_MS) {
    lastFirebaseMs = now;
    syncControlsFromFirebase();
  }

  if (now - lastDisplayMs >= DISPLAY_PERIOD_MS) {
    lastDisplayMs = now;
    updateDisplay();
  }
}
