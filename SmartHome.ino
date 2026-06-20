#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <DHT.h>
#include <LiquidCrystal_I2C.h>
#include <Firebase_ESP_Client.h>

#define WIFI_SSID      "Black Box"
#define WIFI_PASSWORD  "boltechitham"
#define FIREBASE_HOST  "smart-home-122-default-rtdb.firebaseio.com"
#define FIREBASE_AUTH  "mFawsZRgC0dEaQE1VcIyBQeTHV1TVzZlmnNF448Q"

// ESP32 DevKit wiring.
// DHT11/DHT22: VCC -> 3V3, GND -> GND, DATA -> GPIO4.
// 16x2 I2C LCD: VCC -> VIN/5V, GND -> GND, SDA -> GPIO21, SCL -> GPIO22.
// Relay module inputs: Light 1 -> GPIO25, Light 2 -> GPIO26, Fan -> GPIO27.
#define PIN_DHT        4
#define PIN_RELAY_L1   25
#define PIN_RELAY_L2   26
#define PIN_RELAY_FAN  27
#define PIN_I2C_SDA    21
#define PIN_I2C_SCL    22

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

String dbPath(const char *suffix) {
  return String(DB_ROOT) + suffix;
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

void setupOutputs() {
  pinMode(PIN_RELAY_L1, OUTPUT);
  pinMode(PIN_RELAY_L2, OUTPUT);
  pinMode(PIN_RELAY_FAN, OUTPUT);

  light1On = false;
  light2On = false;
  fanOn = false;
  applyOutputs();
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

  Serial.print("Temp: ");
  Serial.print(currentTemperature, 1);
  Serial.print(" C, Humidity: ");
  Serial.print(currentHumidity, 0);
  Serial.println(" %");

  if (firebaseReady()) {
    Firebase.RTDB.setFloat(&fbdo, dbPath("/sensors/temperature"), currentTemperature);
    Firebase.RTDB.setFloat(&fbdo, dbPath("/sensors/humidity"), currentHumidity);
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

#if 0
// Previous extended ESP32-S3 implementation is intentionally disabled.
// It included gas/fire sensors, indicator bars, OLED graphics, I2S audio,
// and speech recognition. The active sketch above only runs fan, two lights,
// DHT, Firebase controls, and a 16x2 I2C LCD on an ESP32 DevKit.
#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <DHT.h>
#include <U8g2lib.h>
#include <Firebase_ESP_Client.h>
#include "driver/i2s.h"

#if __has_include("model_path.h") && __has_include("esp_wn_iface.h") && __has_include("esp_wn_models.h") && __has_include("esp_mn_iface.h") && __has_include("esp_mn_models.h") && __has_include("esp_mn_speech_commands.h")
#include "model_path.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"
#define SMART_HOME_HAS_ESP_SR 1
#else
#define SMART_HOME_HAS_ESP_SR 0
#endif

#define WIFI_SSID      "Black Box"
#define WIFI_PASSWORD  "boltechitham"
#define FIREBASE_HOST  "smart-home-122-default-rtdb.firebaseio.com"
#define FIREBASE_AUTH  "mFawsZRgC0dEaQE1VcIyBQeTHV1TVzZlmnNF448Q"

#define PIN_DHT11        4
#define PIN_MQ2          5
#define PIN_FIRE         6
#define PIN_SDA          8
#define PIN_SCL          9
#define PIN_I2S_BCK     15
#define PIN_I2S_WS      16
#define PIN_I2S_SD      17
#define PIN_RELAY_LED1  10
#define PIN_RELAY_LED2  11
#define PIN_RELAY_FAN   12
#define PIN_TEMP_LOW    13
#define PIN_TEMP_HIGH   14
#define PIN_TBAR_1      18
#define PIN_TBAR_2      21
#define PIN_TBAR_3      38
#define PIN_TBAR_4      39
#define PIN_TBAR_5      40
#define PIN_TBAR_6      41
#define PIN_TBAR_7      42
#define PIN_TBAR_8       1
#define PIN_TBAR_9       2
#define PIN_TBAR_10      3
#define PIN_HUM_LOW     35
#define PIN_HUM_HIGH    36
#define PIN_HBAR_1      37
#define PIN_HBAR_2      47
#define PIN_HBAR_3      48
#define PIN_HBAR_4       7
#define PIN_HBAR_5      33

#define DHTTYPE DHT11
#define GAS_ALERT_PPM 800
#define GAS_WARMUP_MS 60000UL
#define SENSOR_PERIOD_MS 2000
#define FIREBASE_PERIOD_MS 500
#define SAFETY_PERIOD_MS 500
#define DISPLAY_PERIOD_MS 1000
#define FIRMWARE_VERSION "SmartHome-1.0.0"

const char *DB_ROOT = "/smarthome";
const int TEMP_BAR_PINS[] = {PIN_TBAR_1, PIN_TBAR_2, PIN_TBAR_3, PIN_TBAR_4, PIN_TBAR_5, PIN_TBAR_6, PIN_TBAR_7, PIN_TBAR_8, PIN_TBAR_9, PIN_TBAR_10};
const int HUM_BAR_PINS[] = {PIN_HBAR_1, PIN_HBAR_2, PIN_HBAR_3, PIN_HBAR_4, PIN_HBAR_5};
const int INDICATOR_PINS[] = {
  PIN_TEMP_LOW, PIN_TEMP_HIGH, PIN_TBAR_1, PIN_TBAR_2, PIN_TBAR_3, PIN_TBAR_4, PIN_TBAR_5,
  PIN_TBAR_6, PIN_TBAR_7, PIN_TBAR_8, PIN_TBAR_9, PIN_TBAR_10, PIN_HUM_LOW, PIN_HUM_HIGH,
  PIN_HBAR_1, PIN_HBAR_2, PIN_HBAR_3, PIN_HBAR_4, PIN_HBAR_5
};

DHT dht(PIN_DHT11, DHTTYPE);
U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
SemaphoreHandle_t stateMutex;
SemaphoreHandle_t firebaseMutex;

volatile bool safetyTriggered = false;
float currentTemperature = NAN;
float currentHumidity = NAN;
int currentGasPpm = -1;
bool currentFire = false;
bool currentTempLow = false;
bool currentTempHigh = false;
bool currentHumLow = false;
bool currentHumHigh = false;
bool currentGasAlert = false;
String lastVoiceCommand = "";
bool statusPublished = false;

#if SMART_HOME_HAS_ESP_SR
srmodel_list_t *srModels = nullptr;
const esp_wn_iface_t *wakeNet = nullptr;
model_iface_data_t *wakeData = nullptr;
const esp_mn_iface_t *multiNet = nullptr;
model_iface_data_t *multiData = nullptr;
int srChunkSamples = 512;
bool srReady = false;
#else
int srChunkSamples = 512;
bool srReady = false;
#endif

String pathOf(const char *suffix) {
  return String(DB_ROOT) + suffix;
}

bool firebaseReady() {
  return WiFi.status() == WL_CONNECTED && Firebase.ready();
}

void setSharedSensorState(float temperature, float humidity, int gasPpm, bool fire, bool tempLow, bool tempHigh, bool humLow, bool humHigh, bool gasAlert) {
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  currentTemperature = temperature;
  currentHumidity = humidity;
  currentGasPpm = gasPpm;
  currentFire = fire;
  currentTempLow = tempLow;
  currentTempHigh = tempHigh;
  currentHumLow = humLow;
  currentHumHigh = humHigh;
  currentGasAlert = gasAlert;
  xSemaphoreGive(stateMutex);
}

void setRelay(uint8_t pin, bool on) {
  if (safetyTriggered && on) {
    digitalWrite(pin, HIGH);
    return;
  }
  digitalWrite(pin, on ? LOW : HIGH);
}

int readGasPpm() {
  int raw = analogRead(PIN_MQ2);
  return map(raw, 0, 4095, 0, 10000);
}

bool gasWarmupDone() {
  return millis() >= GAS_WARMUP_MS;
}

void updateTempLEDs(float temperature) {
  bool low = temperature < 20.0f;
  bool high = temperature > 40.0f;
  digitalWrite(PIN_TEMP_LOW, low ? HIGH : LOW);
  digitalWrite(PIN_TEMP_HIGH, high ? HIGH : LOW);
  for (int i = 0; i < 10; i++) {
    float threshold = 20.0f + (i * 2.0f);
    digitalWrite(TEMP_BAR_PINS[i], temperature >= threshold ? HIGH : LOW);
  }
}

void updateHumidityLEDs(float humidity) {
  bool low = humidity < 60.0f;
  bool high = humidity > 80.0f;
  digitalWrite(PIN_HUM_LOW, low ? HIGH : LOW);
  digitalWrite(PIN_HUM_HIGH, high ? HIGH : LOW);
  for (int i = 0; i < 5; i++) {
    float threshold = 60.0f + (i * 4.0f);
    digitalWrite(HUM_BAR_PINS[i], humidity >= threshold ? HIGH : LOW);
  }
}

void pushBool(const char *suffix, bool value) {
  Firebase.RTDB.setBool(&fbdo, pathOf(suffix), value);
}

void pushInt(const char *suffix, int value) {
  Firebase.RTDB.setInt(&fbdo, pathOf(suffix), value);
}

void pushFloat(const char *suffix, float value) {
  Firebase.RTDB.setFloat(&fbdo, pathOf(suffix), value);
}

void pushString(const char *suffix, const String &value) {
  Firebase.RTDB.setString(&fbdo, pathOf(suffix), value);
}

bool readControlBool(const char *suffix, bool *value) {
  if (!Firebase.RTDB.getBool(&fbdo, pathOf(suffix))) {
    return false;
  }
  *value = fbdo.boolData();
  return true;
}

void initializeFirebaseDefaults() {
  if (!firebaseReady()) {
    return;
  }
  xSemaphoreTake(firebaseMutex, portMAX_DELAY);
  pushBool("/controls/led1", false);
  pushBool("/controls/led2", false);
  pushBool("/controls/fan", false);
  pushFloat("/sensors/temperature", 0.0f);
  pushFloat("/sensors/humidity", 0.0f);
  pushInt("/sensors/gas_ppm", -1);
  pushBool("/sensors/fire", false);
  pushBool("/alerts/temp_low", false);
  pushBool("/alerts/temp_high", false);
  pushBool("/alerts/hum_low", false);
  pushBool("/alerts/hum_high", false);
  pushBool("/alerts/gas_alert", false);
  pushBool("/alerts/fire_alert", false);
  pushString("/voice/last_command", "");
  pushInt("/status/boot_ms", millis());
  pushString("/status/ip", WiFi.localIP().toString());
  pushString("/status/firmware", FIRMWARE_VERSION);
  statusPublished = true;
  xSemaphoreGive(firebaseMutex);
}

void publishStatusIfNeeded() {
  if (statusPublished || !firebaseReady()) {
    return;
  }
  xSemaphoreTake(firebaseMutex, portMAX_DELAY);
  pushInt("/status/boot_ms", millis());
  pushString("/status/ip", WiFi.localIP().toString());
  pushString("/status/firmware", FIRMWARE_VERSION);
  statusPublished = true;
  xSemaphoreGive(firebaseMutex);
}

void setupRelaysFirst() {
  pinMode(PIN_RELAY_LED1, OUTPUT);
  digitalWrite(PIN_RELAY_LED1, HIGH);
  pinMode(PIN_RELAY_LED2, OUTPUT);
  digitalWrite(PIN_RELAY_LED2, HIGH);
  pinMode(PIN_RELAY_FAN, OUTPUT);
  digitalWrite(PIN_RELAY_FAN, HIGH);
}

void setupIndicatorPins() {
  for (size_t i = 0; i < sizeof(INDICATOR_PINS) / sizeof(INDICATOR_PINS[0]); i++) {
    pinMode(INDICATOR_PINS[i], OUTPUT);
    digitalWrite(INDICATOR_PINS[i], LOW);
  }
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  for (int attempt = 0; attempt < 20 && WiFi.status() != WL_CONNECTED; attempt++) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi connected: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi failed; local mode continues.");
  }
}

void setupFirebase() {
  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.reconnectWiFi(true);
  Firebase.begin(&config, &auth);
  initializeFirebaseDefaults();
}

void setupDisplay() {
  Wire.begin(PIN_SDA, PIN_SCL);
  display.begin();
  display.clearBuffer();
  display.setFont(u8g2_font_6x10_tf);
  display.drawStr(0, 12, "SmartHome booting");
  display.sendBuffer();
}

void setupI2S() {
  i2s_config_t i2sConfig = {};
  i2sConfig.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  i2sConfig.sample_rate = 16000;
  i2sConfig.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  i2sConfig.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  i2sConfig.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  i2sConfig.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  i2sConfig.dma_buf_count = 6;
  i2sConfig.dma_buf_len = 512;
  i2sConfig.use_apll = false;
  i2sConfig.tx_desc_auto_clear = false;
  i2sConfig.fixed_mclk = 0;
  i2s_driver_install(I2S_NUM_0, &i2sConfig, 0, NULL);

  i2s_pin_config_t pinConfig = {};
  pinConfig.bck_io_num = PIN_I2S_BCK;
  pinConfig.ws_io_num = PIN_I2S_WS;
  pinConfig.data_out_num = I2S_PIN_NO_CHANGE;
  pinConfig.data_in_num = PIN_I2S_SD;
  i2s_set_pin(I2S_NUM_0, &pinConfig);
  i2s_zero_dma_buffer(I2S_NUM_0);
}

void addVoiceCommands() {
#if SMART_HOME_HAS_ESP_SR
  esp_mn_commands_alloc(multiNet, multiData);
  esp_mn_commands_clear();
  esp_mn_commands_add(1, "light one on");
  esp_mn_commands_add(2, "light one off");
  esp_mn_commands_add(3, "light two on");
  esp_mn_commands_add(4, "light two off");
  esp_mn_commands_add(5, "fan on");
  esp_mn_commands_add(6, "fan off");
  esp_mn_commands_add(7, "all off");
  esp_mn_commands_update();
#endif
}

void setupSpeechRecognition() {
#if SMART_HOME_HAS_ESP_SR
  srModels = esp_srmodel_init("model");
  if (!srModels) {
    Serial.println("ESP-SR model partition not found.");
    return;
  }
  char *wakeName = esp_srmodel_filter(srModels, ESP_WN_PREFIX, "wn9_hilexin");
  if (!wakeName) {
    wakeName = esp_srmodel_filter(srModels, ESP_WN_PREFIX, NULL);
  }
  char *commandName = esp_srmodel_filter(srModels, ESP_MN_PREFIX, NULL);
  if (!wakeName || !commandName) {
    Serial.println("ESP-SR wake or command model missing.");
    return;
  }
  wakeNet = esp_wn_handle_from_name(wakeName);
  multiNet = esp_mn_handle_from_name(commandName);
  if (!wakeNet || !multiNet) {
    Serial.println("ESP-SR model handle failed.");
    return;
  }
  wakeData = wakeNet->create(wakeName, DET_MODE_90);
  multiData = multiNet->create(commandName, 6000);
  if (!wakeData || !multiData) {
    Serial.println("ESP-SR model create failed.");
    return;
  }
  addVoiceCommands();
  int wakeChunk = wakeNet->get_samp_chunksize(wakeData);
  int commandChunk = multiNet->get_samp_chunksize(multiData);
  srChunkSamples = max(wakeChunk, commandChunk);
  srReady = true;
  Serial.println("ESP-SR ready.");
#else
  srReady = false;
  Serial.println("ESP-SR headers not available in this Arduino install; voice task will keep audio input alive.");
#endif
}

void flashWakeFeedback() {
  digitalWrite(PIN_TEMP_LOW, HIGH);
  digitalWrite(PIN_HUM_LOW, HIGH);
  vTaskDelay(pdMS_TO_TICKS(200));
  digitalWrite(PIN_TEMP_LOW, LOW);
  digitalWrite(PIN_HUM_LOW, LOW);
}

bool readAudioFrame(int16_t *samples, int sampleCount) {
  static int32_t raw[1024];
  int remaining = sampleCount;
  int offset = 0;
  while (remaining > 0) {
    int block = min(remaining, (int)(sizeof(raw) / sizeof(raw[0])));
    size_t bytesRead = 0;
    esp_err_t result = i2s_read(I2S_NUM_0, raw, block * sizeof(int32_t), &bytesRead, pdMS_TO_TICKS(200));
    if (result != ESP_OK || bytesRead == 0) {
      memset(samples + offset, 0, block * sizeof(int16_t));
      return false;
    }
    int got = bytesRead / sizeof(int32_t);
    for (int i = 0; i < got; i++) {
      samples[offset + i] = (int16_t)(raw[i] >> 14);
    }
    offset += got;
    remaining -= got;
  }
  return true;
}

int detectWakeWord(int16_t *samples) {
#if SMART_HOME_HAS_ESP_SR
  if (!srReady) {
    return 0;
  }
  return wakeNet->detect(wakeData, samples);
#else
  (void)samples;
  return 0;
#endif
}

int detectCommand(int16_t *samples) {
#if SMART_HOME_HAS_ESP_SR
  if (!srReady) {
    return 0;
  }
  esp_mn_state_t state = multiNet->detect(multiData, samples);
  if (state == ESP_MN_STATE_DETECTED) {
    esp_mn_results_t *results = multiNet->get_results(multiData);
    if (results && results->num > 0) {
      return results->command_id[0];
    }
  }
  return 0;
#else
  (void)samples;
  return 0;
#endif
}

String commandText(int commandId) {
  switch (commandId) {
    case 1: return "light one on";
    case 2: return "light one off";
    case 3: return "light two on";
    case 4: return "light two off";
    case 5: return "fan on";
    case 6: return "fan off";
    case 7: return "all off";
    default: return "";
  }
}

void writeVoiceCommandToFirebase(int commandId) {
  if (!firebaseReady()) {
    return;
  }
  String text = commandText(commandId);
  xSemaphoreTake(firebaseMutex, portMAX_DELAY);
  if (!safetyTriggered) {
    if (commandId == 1) pushBool("/controls/led1", true);
    if (commandId == 3) pushBool("/controls/led2", true);
    if (commandId == 5) pushBool("/controls/fan", true);
  }
  if (commandId == 2) pushBool("/controls/led1", false);
  if (commandId == 4) pushBool("/controls/led2", false);
  if (commandId == 6) pushBool("/controls/fan", false);
  if (commandId == 7) {
    pushBool("/controls/led1", false);
    pushBool("/controls/led2", false);
    pushBool("/controls/fan", false);
  }
  pushString("/voice/last_command", text);
  xSemaphoreGive(firebaseMutex);

  xSemaphoreTake(stateMutex, portMAX_DELAY);
  lastVoiceCommand = text;
  xSemaphoreGive(stateMutex);
}

void sensorTask(void *parameter) {
  (void)parameter;
  for (;;) {
    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();
    if (isnan(temperature) || isnan(humidity)) {
      Serial.println("DHT11 read failed.");
      vTaskDelay(pdMS_TO_TICKS(SENSOR_PERIOD_MS));
      continue;
    }

    int gasPpm = gasWarmupDone() ? readGasPpm() : -1;
    bool fire = digitalRead(PIN_FIRE) == LOW;
    bool tempLow = temperature < 20.0f;
    bool tempHigh = temperature > 40.0f;
    bool humLow = humidity < 60.0f;
    bool humHigh = humidity > 80.0f;
    bool gasAlert = gasWarmupDone() && gasPpm > GAS_ALERT_PPM;

    updateTempLEDs(temperature);
    updateHumidityLEDs(humidity);
    setSharedSensorState(temperature, humidity, gasPpm, fire, tempLow, tempHigh, humLow, humHigh, gasAlert);

    if (firebaseReady()) {
      xSemaphoreTake(firebaseMutex, portMAX_DELAY);
      if (!statusPublished) {
        pushInt("/status/boot_ms", millis());
        pushString("/status/ip", WiFi.localIP().toString());
        pushString("/status/firmware", FIRMWARE_VERSION);
        statusPublished = true;
      }
      pushFloat("/sensors/temperature", temperature);
      pushFloat("/sensors/humidity", humidity);
      pushInt("/sensors/gas_ppm", gasPpm);
      pushBool("/sensors/fire", fire);
      pushBool("/alerts/temp_low", tempLow);
      pushBool("/alerts/temp_high", tempHigh);
      pushBool("/alerts/hum_low", humLow);
      pushBool("/alerts/hum_high", humHigh);
      pushBool("/alerts/gas_alert", gasAlert);
      pushBool("/alerts/fire_alert", fire);
      xSemaphoreGive(firebaseMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(SENSOR_PERIOD_MS));
  }
}

void firebaseListenerTask(void *parameter) {
  (void)parameter;
  for (;;) {
    publishStatusIfNeeded();
    if (firebaseReady() && !safetyTriggered) {
      bool value = false;
      xSemaphoreTake(firebaseMutex, portMAX_DELAY);
      if (readControlBool("/controls/led1", &value)) setRelay(PIN_RELAY_LED1, value);
      if (readControlBool("/controls/led2", &value)) setRelay(PIN_RELAY_LED2, value);
      if (readControlBool("/controls/fan", &value)) setRelay(PIN_RELAY_FAN, value);
      xSemaphoreGive(firebaseMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(FIREBASE_PERIOD_MS));
  }
}

void safetyTask(void *parameter) {
  (void)parameter;
  for (;;) {
    bool fire = digitalRead(PIN_FIRE) == LOW;
    int ppm = readGasPpm();
    bool gasAlert = gasWarmupDone() && ppm > GAS_ALERT_PPM;
    if (fire || gasAlert) {
      digitalWrite(PIN_RELAY_LED1, HIGH);
      digitalWrite(PIN_RELAY_LED2, HIGH);
      digitalWrite(PIN_RELAY_FAN, HIGH);
      safetyTriggered = true;
      for (int i = 0; i < 5; i++) {
        digitalWrite(PIN_TEMP_HIGH, HIGH);
        vTaskDelay(pdMS_TO_TICKS(100));
        digitalWrite(PIN_TEMP_HIGH, LOW);
        vTaskDelay(pdMS_TO_TICKS(100));
      }
    }
    vTaskDelay(pdMS_TO_TICKS(SAFETY_PERIOD_MS));
  }
}

void drawNormalDisplay(float temperature, float humidity, int gasPpm, bool fire, bool gasAlert) {
  char line[24];
  display.clearBuffer();
  display.setFont(u8g2_font_6x10_tf);
  if (isnan(temperature)) snprintf(line, sizeof(line), "Temp:  --.- C");
  else snprintf(line, sizeof(line), "Temp:  %4.1f C", temperature);
  display.drawStr(0, 12, line);
  if (isnan(humidity)) snprintf(line, sizeof(line), "Hum:   -- %%");
  else snprintf(line, sizeof(line), "Hum:   %2.0f %%", humidity);
  display.drawStr(0, 26, line);
  if (!gasWarmupDone()) snprintf(line, sizeof(line), "Gas:   Warming...");
  else snprintf(line, sizeof(line), "Gas:   %4d ppm", gasPpm);
  display.drawStr(0, 40, line);
  const char *status = "OK";
  if (fire) status = "FIRE!";
  else if (gasAlert) status = "GAS!";
  else if (WiFi.status() != WL_CONNECTED) status = "No WiFi";
  snprintf(line, sizeof(line), "Status: %s", status);
  display.drawStr(0, 54, line);
  display.sendBuffer();
}

void drawAlertDisplay() {
  static bool blink = false;
  blink = !blink;
  display.clearBuffer();
  if (blink) {
    display.setFont(u8g2_font_ncenB14_tr);
    display.drawStr(6, 32, "!! ALERT !!");
  }
  display.sendBuffer();
}

void displayTask(void *parameter) {
  (void)parameter;
  for (;;) {
    float temperature;
    float humidity;
    int gasPpm;
    bool fire;
    bool gasAlert;
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    temperature = currentTemperature;
    humidity = currentHumidity;
    gasPpm = currentGasPpm;
    fire = currentFire;
    gasAlert = currentGasAlert;
    xSemaphoreGive(stateMutex);

    if (fire || gasAlert || safetyTriggered) {
      drawAlertDisplay();
    } else {
      drawNormalDisplay(temperature, humidity, gasPpm, fire, gasAlert);
    }
    vTaskDelay(pdMS_TO_TICKS(DISPLAY_PERIOD_MS));
  }
}

void voiceTask(void *parameter) {
  (void)parameter;
  int16_t *audio = (int16_t *)heap_caps_malloc(srChunkSamples * sizeof(int16_t), MALLOC_CAP_8BIT);
  if (!audio) {
    Serial.println("Voice audio buffer allocation failed.");
    vTaskDelete(NULL);
  }

  for (;;) {
    readAudioFrame(audio, srChunkSamples);
    if (detectWakeWord(audio) > 0) {
      flashWakeFeedback();
      uint32_t deadline = millis() + 5000UL;
      while (millis() < deadline) {
        readAudioFrame(audio, srChunkSamples);
        int commandId = detectCommand(audio);
        if (commandId >= 1 && commandId <= 7) {
          bool onCommand = commandId == 1 || commandId == 3 || commandId == 5;
          if (!(safetyTriggered && onCommand)) {
            writeVoiceCommandToFirebase(commandId);
          }
          break;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void setup() {
  setupRelaysFirst();
  setupIndicatorPins();
  pinMode(PIN_FIRE, INPUT_PULLUP);
  Serial.begin(115200);
  delay(100);
  dht.begin();
  stateMutex = xSemaphoreCreateMutex();
  firebaseMutex = xSemaphoreCreateMutex();
  analogReadResolution(12);
  connectWiFi();
  setupFirebase();
  setupDisplay();
  setupI2S();
  setupSpeechRecognition();

  xTaskCreatePinnedToCore(sensorTask, "sensorTask", 4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(firebaseListenerTask, "firebaseListenerTask", 4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(safetyTask, "safetyTask", 2048, NULL, 4, NULL, 0);
  xTaskCreatePinnedToCore(displayTask, "displayTask", 4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(voiceTask, "voiceTask", 8192, NULL, 3, NULL, 0);
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
#endif
