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
// DHT11/DHT22: VCC -> 3V3, GND -> GND, DATA -> GPIO4.
// MQ2 gas module: VCC -> 5V, GND -> GND, AO -> GPIO34 through voltage divider, DO -> GPIO33.
// Fire/flame module: VCC -> 3V3, GND -> GND, AO -> GPIO35, DO -> GPIO32.
// 16x2 I2C LCD: VCC -> VIN/5V, GND -> GND, SDA -> GPIO21, SCL -> GPIO22.
// Relay module inputs: Light 1 -> GPIO25, Light 2 -> GPIO26, Fan -> GPIO27.
// Three cascaded SN74HC595N shift registers:
// SER/DS -> GPIO13, SRCLK/SH_CP -> GPIO14, RCLK/ST_CP -> GPIO15.
// Tie every OE pin to GND, every MR/SRCLR pin to 3V3, VCC to 3V3, and GND to GND.
#define PIN_DHT        4
#define PIN_RELAY_L1   25
#define PIN_RELAY_L2   26
#define PIN_RELAY_FAN  27
#define PIN_I2C_SDA    21
#define PIN_I2C_SCL    22
#define PIN_SR_SER     13  // SN74HC595 SER (data)
#define PIN_SR_SRCLK   14  // SN74HC595 SRCLK (shift clock)
#define PIN_SR_RCLK    15  // SN74HC595 RCLK/ST_CP (latch clock)
#define PIN_MQ2_AO     34  // MQ2 analog output
#define PIN_MQ2_DO     33  // MQ2 digital output, active LOW on most modules
#define PIN_FIRE_AO    35  // Flame sensor analog output
#define PIN_FIRE_DO    32  // Flame sensor digital output, active LOW on most modules

// Three SN74HC595N output mapping. Bit 0 is chip 1 Q0, bit 23 is chip 3 Q7.
// Connect each Q output to an LED through a 220-330 ohm resistor.
#define SR_TEMP_LOW     (1UL << 0)   // Chip 1 Q0: temperature < 20 C
#define SR_TEMP_HIGH    (1UL << 1)   // Chip 1 Q1: temperature > 40 C
#define SR_TBAR_1       (1UL << 2)   // Chip 1 Q2: 20 C
#define SR_TBAR_2       (1UL << 3)   // Chip 1 Q3: 22 C
#define SR_TBAR_3       (1UL << 4)   // Chip 1 Q4: 24 C
#define SR_TBAR_4       (1UL << 5)   // Chip 1 Q5: 26 C
#define SR_TBAR_5       (1UL << 6)   // Chip 1 Q6: 28 C
#define SR_TBAR_6       (1UL << 7)   // Chip 1 Q7: 30 C
#define SR_TBAR_7       (1UL << 8)   // Chip 2 Q0: 32 C
#define SR_TBAR_8       (1UL << 9)   // Chip 2 Q1: 34 C
#define SR_TBAR_9       (1UL << 10)  // Chip 2 Q2: 36 C
#define SR_TBAR_10      (1UL << 11)  // Chip 2 Q3: 38 C
#define SR_HUM_LOW      (1UL << 12)  // Chip 2 Q4: humidity < 65 %
#define SR_HUM_HIGH     (1UL << 13)  // Chip 2 Q5: humidity > 95 %
#define SR_HBAR_1       (1UL << 14)  // Chip 2 Q6: 65 %
#define SR_HBAR_2       (1UL << 15)  // Chip 2 Q7: 70 %
#define SR_HBAR_3       (1UL << 16)  // Chip 3 Q0: 75 %
#define SR_HBAR_4       (1UL << 17)  // Chip 3 Q1: 80 %
#define SR_HBAR_5       (1UL << 18)  // Chip 3 Q2: 85 %
#define SR_SENSOR_OK    (1UL << 19)  // Chip 3 Q3: last DHT read succeeded
#define SR_SENSOR_ERROR (1UL << 20)  // Chip 3 Q4: last DHT read failed

#define DHTTYPE DHT11
#define LCD_ADDRESS 0x27
#define LCD_COLUMNS 16
#define LCD_ROWS 2

#define SENSOR_PERIOD_MS 2000UL
#define FIREBASE_PERIOD_MS 500UL
#define DISPLAY_PERIOD_MS 1000UL
#define WIFI_RETRY_PERIOD_MS 10000UL
#define FIRMWARE_VERSION "SmartHome-DevKit-1.1.0"

#define TEMP_LOW_LIMIT_C 20.0f
#define TEMP_HIGH_LIMIT_C 40.0f
#define HUM_LOW_LIMIT_PERCENT 65.0f
#define HUM_HIGH_LIMIT_PERCENT 95.0f
#define GAS_ALERT_PPM 800
#define GAS_WARMUP_MS 60000UL
#define FIRE_ANALOG_ALERT_RAW 1800

const char *DB_ROOT = "/smarthome";

DHT dht(PIN_DHT, DHTTYPE);
LiquidCrystal_I2C lcd(LCD_ADDRESS, LCD_COLUMNS, LCD_ROWS);
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

float currentTemperature = NAN;
float currentHumidity = NAN;
int currentGasRaw = 0;
int currentGasPpm = -1;
int currentFireRaw = 0;
bool gasDetected = false;
bool fireDetected = false;
bool gasAlert = false;
bool fireAlert = false;
bool light1On = false;
bool light2On = false;
bool fanOn = false;
unsigned long lastSensorMs = 0;
unsigned long lastFirebaseMs = 0;
unsigned long lastDisplayMs = 0;
unsigned long lastWifiRetryMs = 0;
uint32_t srData = 0;

String dbPath(const char *suffix) {
  return String(DB_ROOT) + suffix;
}

void writeShiftRegister() {
  uint8_t chip1 = srData & 0xFF;
  uint8_t chip2 = (srData >> 8) & 0xFF;
  uint8_t chip3 = (srData >> 16) & 0xFF;

  digitalWrite(PIN_SR_RCLK, LOW);
  shiftOut(PIN_SR_SER, PIN_SR_SRCLK, MSBFIRST, chip3);
  shiftOut(PIN_SR_SER, PIN_SR_SRCLK, MSBFIRST, chip2);
  shiftOut(PIN_SR_SER, PIN_SR_SRCLK, MSBFIRST, chip1);
  digitalWrite(PIN_SR_RCLK, HIGH);
}

void setShiftRegister(uint32_t value) {
  srData = value;
  writeShiftRegister();
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

void showSensorError() {
  setShiftRegister(SR_SENSOR_ERROR);
}

void updateEnvironmentLEDs(float temperature, float humidity) {
  bool tempLow = temperature < TEMP_LOW_LIMIT_C;
  bool tempHigh = temperature > TEMP_HIGH_LIMIT_C;
  bool humLow = humidity < HUM_LOW_LIMIT_PERCENT;
  bool humHigh = humidity > HUM_HIGH_LIMIT_PERCENT;
  uint32_t nextData = SR_SENSOR_OK;
  const uint32_t tempBarBits[] = {
    SR_TBAR_1, SR_TBAR_2, SR_TBAR_3, SR_TBAR_4, SR_TBAR_5,
    SR_TBAR_6, SR_TBAR_7, SR_TBAR_8, SR_TBAR_9, SR_TBAR_10
  };
  const uint32_t humBarBits[] = {
    SR_HBAR_1, SR_HBAR_2, SR_HBAR_3, SR_HBAR_4, SR_HBAR_5
  };

  if (tempLow) {
    nextData |= SR_TEMP_LOW;
  } else if (tempHigh) {
    nextData |= SR_TEMP_HIGH;
  }

  if (humLow) {
    nextData |= SR_HUM_LOW;
  } else if (humHigh) {
    nextData |= SR_HUM_HIGH;
  }

  for (int i = 0; i < 10; i++) {
    float threshold = TEMP_LOW_LIMIT_C + (i * 2.0f);
    if (temperature >= threshold) {
      nextData |= tempBarBits[i];
    }
  }

  for (int i = 0; i < 5; i++) {
    float threshold = HUM_LOW_LIMIT_PERCENT + (i * 5.0f);
    if (humidity >= threshold) {
      nextData |= humBarBits[i];
    }
  }

  setShiftRegister(nextData);
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
  applyOutputs();
  setShiftRegister(0);
}

void setupSensors() {
  pinMode(PIN_MQ2_DO, INPUT_PULLUP);
  pinMode(PIN_FIRE_DO, INPUT_PULLUP);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_MQ2_AO, ADC_11db);
  analogSetPinAttenuation(PIN_FIRE_AO, ADC_11db);
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
  Firebase.RTDB.setInt(&fbdo, dbPath("/sensors/gas_raw"), currentGasRaw);
  Firebase.RTDB.setInt(&fbdo, dbPath("/sensors/gas_ppm"), currentGasPpm);
  Firebase.RTDB.setBool(&fbdo, dbPath("/sensors/gas_detected"), gasDetected);
  Firebase.RTDB.setInt(&fbdo, dbPath("/sensors/fire_raw"), currentFireRaw);
  Firebase.RTDB.setBool(&fbdo, dbPath("/sensors/fire"), fireDetected);
  Firebase.RTDB.setBool(&fbdo, dbPath("/alerts/gas_alert"), gasAlert);
  Firebase.RTDB.setBool(&fbdo, dbPath("/alerts/fire_alert"), fireAlert);
  Firebase.RTDB.setString(&fbdo, dbPath("/status/ip"), WiFi.localIP().toString());
  Firebase.RTDB.setString(&fbdo, dbPath("/status/firmware"), FIRMWARE_VERSION);
  Firebase.RTDB.setInt(&fbdo, dbPath("/status/boot_ms"), millis());
}

void readEnvironmentSensors() {
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  bool gasWarmedUp = millis() >= GAS_WARMUP_MS;

  currentGasRaw = analogRead(PIN_MQ2_AO);
  currentGasPpm = gasWarmedUp ? map(currentGasRaw, 0, 4095, 0, 10000) : -1;
  gasDetected = digitalRead(PIN_MQ2_DO) == LOW;

  currentFireRaw = analogRead(PIN_FIRE_AO);
  fireDetected = digitalRead(PIN_FIRE_DO) == LOW || currentFireRaw < FIRE_ANALOG_ALERT_RAW;
  gasAlert = gasWarmedUp && (currentGasPpm > GAS_ALERT_PPM || gasDetected);
  fireAlert = fireDetected;

  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("DHT read failed.");
    showSensorError();
  } else {
    currentTemperature = temperature;
    currentHumidity = humidity;
    updateEnvironmentLEDs(temperature, humidity);
  }

  Serial.print("Temp: ");
  if (isnan(currentTemperature)) {
    Serial.print("--.-");
  } else {
    Serial.print(currentTemperature, 1);
  }
  Serial.print(" C, Humidity: ");
  if (isnan(currentHumidity)) {
    Serial.print("--");
  } else {
    Serial.print(currentHumidity, 0);
  }
  Serial.print(" %, Gas raw: ");
  Serial.print(currentGasRaw);
  Serial.print(", Gas ppm: ");
  Serial.print(currentGasPpm);
  Serial.print(", Gas DO: ");
  Serial.print(gasDetected ? "ALERT" : "OK");
  Serial.print(", Fire raw: ");
  Serial.print(currentFireRaw);
  Serial.print(", Fire: ");
  Serial.println(fireDetected ? "DETECTED" : "OK");

  if (firebaseReady()) {
    if (!isnan(currentTemperature) && !isnan(currentHumidity)) {
      if (!Firebase.RTDB.setFloat(&fbdo, dbPath("/sensors/temperature"), currentTemperature)) {
        Serial.println("Temp write failed: " + fbdo.errorReason());
      }
      if (!Firebase.RTDB.setFloat(&fbdo, dbPath("/sensors/humidity"), currentHumidity)) {
        Serial.println("Hum write failed: " + fbdo.errorReason());
      }
      Firebase.RTDB.setBool(&fbdo, dbPath("/alerts/temp_low"), currentTemperature < TEMP_LOW_LIMIT_C);
      Firebase.RTDB.setBool(&fbdo, dbPath("/alerts/temp_high"), currentTemperature > TEMP_HIGH_LIMIT_C);
      Firebase.RTDB.setBool(&fbdo, dbPath("/alerts/hum_low"), currentHumidity < HUM_LOW_LIMIT_PERCENT);
      Firebase.RTDB.setBool(&fbdo, dbPath("/alerts/hum_high"), currentHumidity > HUM_HIGH_LIMIT_PERCENT);
    }
    Firebase.RTDB.setInt(&fbdo, dbPath("/sensors/gas_raw"), currentGasRaw);
    Firebase.RTDB.setInt(&fbdo, dbPath("/sensors/gas_ppm"), currentGasPpm);
    Firebase.RTDB.setBool(&fbdo, dbPath("/sensors/gas_detected"), gasDetected);
    Firebase.RTDB.setInt(&fbdo, dbPath("/sensors/fire_raw"), currentFireRaw);
    Firebase.RTDB.setBool(&fbdo, dbPath("/sensors/fire"), fireDetected);
    Firebase.RTDB.setBool(&fbdo, dbPath("/alerts/gas_alert"), gasAlert);
    Firebase.RTDB.setBool(&fbdo, dbPath("/alerts/fire_alert"), fireAlert);
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
  if (fireAlert) {
    snprintf(line, sizeof(line), "FIRE! G:%4d   ", currentGasPpm);
  } else if (gasAlert) {
    snprintf(line, sizeof(line), "GAS! %4d ppm  ", currentGasPpm);
  } else {
    snprintf(line, sizeof(line), "Gas:%4d F:%s  ", currentGasPpm, fireDetected ? "Y" : "N");
  }
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
  setupSensors();
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
    readEnvironmentSensors();
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
