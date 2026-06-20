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

Open `SmartHome.ino`, then set:

- Board: `ESP32 Dev Module`
- CPU Frequency: `240MHz`
- Flash Size: `4MB`
- Partition Scheme: `Default 4MB with spiffs`
- Upload Speed: `921600` or `115200`

## 3. Wiring

- DHT data: `GPIO4`
- 16x2 I2C LCD SDA: `GPIO21`
- 16x2 I2C LCD SCL: `GPIO22`
- Light 1 relay input: `GPIO25`
- Light 2 relay input: `GPIO26`
- Fan relay input: `GPIO27`

The sketch assumes a common active-low relay module, where `LOW` turns the relay ON and `HIGH` turns it OFF.

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

In `SmartHome.ino`, replace:

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
arduino-cli compile --fqbn esp32:esp32:esp32 SmartHome.ino
```

## 7. Upload from Arduino IDE

1. Connect the ESP32 DevKit by USB.
2. Select the correct port from `Tools > Port`.
3. Click Upload.
4. Open Serial Monitor at `115200 baud`.

After boot:

- Relays start OFF.
- Firebase paths appear under `/smarthome`.
- The 16x2 LCD shows temperature, humidity, and the current light/fan states.
