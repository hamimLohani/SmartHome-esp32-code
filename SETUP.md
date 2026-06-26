# SmartHome Setup

## 1. Arduino IDE setup

Install Arduino IDE, then install the ESP32 board package:

1. Open Arduino IDE.
2. Go to `Arduino IDE > Settings`.
3. In `Additional boards manager URLs`, add:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
4. Go to `Tools > Board > Boards Manager`.
5. Search `esp32` and install `esp32 by Espressif Systems`.

Install these libraries from `Tools > Manage Libraries`:

- `DHT sensor library` by Adafruit
- `Adafruit Unified Sensor`
- `Firebase Arduino Client Library for ESP8266 and ESP32` by Mobizt
- `LiquidCrystal I2C`

## 2. Board settings

Open `SmartHome-esp32-code.ino`, then set:

- Board: `ESP32 Dev Module`
- CPU Frequency: `240MHz`
- Flash Size: `4MB`
- Partition Scheme: `Default 4MB with spiffs`
- Upload Speed: `921600` or `115200`

## 3. Wiring

For a focused LED-only guide, see `LED_SETUP.md`.

- DHT data: `GPIO4`
- 16x2 I2C LCD SDA: `GPIO21`
- 16x2 I2C LCD SCL: `GPIO22`
- Light 1 relay input: `GPIO25`
- Light 2 relay input: `GPIO26`
- Fan relay input: `GPIO27`
- Chip 1 SN74HC595N SER / DS / pin 14: `GPIO13`
- All SN74HC595N SRCLK / SH_CP / pin 11: `GPIO14`
- All SN74HC595N RCLK / ST_CP / pin 12: `GPIO15`
- Every SN74HC595N OE / pin 13: `GND`
- Every SN74HC595N MR / SRCLR / pin 10: `3V3`
- Every SN74HC595N VCC / pin 16: `3V3`
- Every SN74HC595N GND / pin 8: `GND`
- Chip 1 Q7S / QH' / pin 9: Chip 2 SER / DS / pin 14
- Chip 2 Q7S / QH' / pin 9: Chip 3 SER / DS / pin 14

The sketch assumes a common active-low relay module, where `LOW` turns the relay ON and `HIGH` turns it OFF.

Place the three `SN74HC595N` chips vertically on the breadboard with two empty rows between each chip, matching the screenshot layout. Connect each shift-register output to an LED through a `220-330 ohm` resistor:

| Output | Meaning |
| --- | --- |
| Chip 1 Q0 | Temperature low, below `20 C` |
| Chip 1 Q1 | Temperature high, above `40 C` |
| Chip 1 Q2 | Temperature bar `20 C` |
| Chip 1 Q3 | Temperature bar `22 C` |
| Chip 1 Q4 | Temperature bar `24 C` |
| Chip 1 Q5 | Temperature bar `26 C` |
| Chip 1 Q6 | Temperature bar `28 C` |
| Chip 1 Q7 | Temperature bar `30 C` |
| Chip 2 Q0 | Temperature bar `32 C` |
| Chip 2 Q1 | Temperature bar `34 C` |
| Chip 2 Q2 | Temperature bar `36 C` |
| Chip 2 Q3 | Temperature bar `38 C` |
| Chip 2 Q4 | Humidity low, below `65 %` |
| Chip 2 Q5 | Humidity high, above `95 %` |
| Chip 2 Q6 | Humidity bar `65 %` |
| Chip 2 Q7 | Humidity bar `70 %` |
| Chip 3 Q0 | Humidity bar `75 %` |
| Chip 3 Q1 | Humidity bar `80 %` |
| Chip 3 Q2 | Humidity bar `85 %` |
| Chip 3 Q3 | Last DHT read OK |
| Chip 3 Q4 | Last DHT read failed |
| Chip 3 Q5-Q7 | Spare |

## 4. Firebase Realtime Database setup

1. Go to `https://console.firebase.google.com`.
2. Create a project.
3. In the left menu, open `Build > Realtime Database`.
4. Click `Create Database`.
5. Pick a nearby region.
6. Start in test mode for quick testing.
7. Open the database `Data` tab.
8. Use the three-dot menu and import `firebase-database-seed.json`.
9. Open the `Rules` tab.
10. Paste the contents of `database.rules.json` and publish.

These rules are open for beginner testing. Do not use them for a public product.

## 5. Get Firebase values for the sketch

In `SmartHome-esp32-code.ino`, replace:

```cpp
#define WIFI_SSID      "YOUR_WIFI_SSID"
#define WIFI_PASSWORD  "YOUR_WIFI_PASSWORD"
#define FIREBASE_HOST  "YOUR_PROJECT.firebaseio.com"
#define FIREBASE_AUTH  "YOUR_DATABASE_SECRET"
```

Use your real WiFi name and password.

For `FIREBASE_HOST`, copy your Realtime Database URL from Firebase. Use only the host part, for example:

```cpp
#define FIREBASE_HOST "my-project-default-rtdb.firebaseio.com"
```

For `FIREBASE_AUTH`, use your Realtime Database legacy database secret:

1. In Firebase Console, open `Project settings`.
2. Open `Service accounts`.
3. Open `Database secrets`.
4. Copy the secret value.

If Firebase does not show `Database secrets`, ask me to convert the sketch to email/password Firebase authentication.

## 6. Compile from terminal

This command compiled successfully on this machine:

```sh
arduino-cli compile --fqbn esp32:esp32:esp32 .
```

## 7. Upload from Arduino IDE

1. Connect the ESP32 DevKit by USB.
2. Select `Tools > Board > esp32 > ESP32 Dev Module`.
3. Select the correct port from `Tools > Port`.
4. Click Upload.
5. Open Serial Monitor at `115200 baud`.

Do not select an `ESP32-S3` board for this hardware. If upload fails with:

```text
A fatal error occurred: This chip is ESP32, not ESP32-S3. Wrong chip argument?
```

change the selected board to `ESP32 Dev Module`, then upload again.

## 8. Upload from terminal

List connected ports:

```sh
arduino-cli board list
```

Upload with the ESP32 board target:

```sh
arduino-cli upload -p /dev/cu.usbserial-XXXX --fqbn esp32:esp32:esp32 .
```

Replace `/dev/cu.usbserial-XXXX` with the port shown by `arduino-cli board list`.

Or use the repo helper:

```sh
./esp_upload.sh /dev/cu.usbserial-XXXX
```

The helper defaults to `esp32:esp32:esp32`. Do not use `esp32:esp32:esp32s3` for this board.

After boot:

- Relays start OFF.
- Firebase paths appear under `/smarthome`.
- The 16x2 LCD shows temperature, humidity, and the current light/fan states.
