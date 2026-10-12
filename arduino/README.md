# WebBrew v0.2 for Arduino IDE

This folder contains a complete Arduino IDE version of WebBrew. The firmware and dashboard are the same as the PlatformIO project. **No PlatformIO, Python, Node.js, filesystem upload or code-generation step is needed to use this committed sketch.**

## Arduino IDE setup

1. Install Arduino IDE 2.x.
2. In **File → Preferences → Additional boards manager URLs**, add:

   ```text
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```

3. Open **Tools → Board → Boards Manager**. Install **esp32 by Espressif Systems**, selecting **version 2.0.17**. Use this exact version: the firmware currently uses the 2.x watchdog API and is not compatible with core 3.x.
4. Open **Library Manager** and install these versions:

   | Library | Author | Version |
   |---|---|---|
   | OneWire | Paul Stoffregen | 2.3.8 |
   | DallasTemperature | Miles Burton and contributors | 3.11.0 |
   | ArduinoJson | Benoit Blanchon | 7.2.1 |

   WiFi, WebServer and Preferences are supplied by the ESP32 board package. Do not install unrelated libraries with those names.

5. Open **WebBrew/WebBrew.ino**. Keep `firmware.cpp`, `controller.h` and `web_page.h` in the same `WebBrew` folder. Arduino IDE will show the source files as tabs and compile them together. The small `.ino` file is intentional: the full firmware is in the `firmware.cpp` tab.
6. Select **Tools → Board → esp32 → ESP32 Dev Module** and the board's USB port. Leave the standard 4 MB flash / default partition scheme selected for a typical WROOM-32 DevKit. This is for the classic ESP32, not ESP32-S2/S3/C3.
7. **Disconnect the heating supply**, then click **Verify** followed by **Upload**. A board-specific BOOT-button press may be needed for upload.
8. Open Serial Monitor at **115200 baud**. Reset the ESP32 if necessary to see the generated credentials. Connect to **WebBrew-XXXX** with the printed password, open **http://192.168.4.1**, and sign in as **brew** with the same password.

Default wiring is SSR **GPIO 25**, DS18B20 **GPIO 4**, active-high SSR polarity. Active-low is selectable in the dashboard. Internal off-state pull-up/down is enabled, but external off-state bias is still needed for reset/power-up protection. Read [the hardware and commissioning guide](../docs/HARDWARE.md) before connecting heating power.

The web page is compiled into flash in `web_page.h`. No SPIFFS/LittleFS plugin or separate web-file upload is required. Existing NVS configuration is retained when updating firmware normally; GPIO/polarity defaults do not overwrite previously saved settings.

## Optional Arduino CLI build

```sh
arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core install esp32:esp32@2.0.17 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli lib install "OneWire@2.3.8" "DallasTemperature@3.11.0" "ArduinoJson@7.2.1"
arduino-cli compile --fqbn esp32:esp32:esp32 arduino/WebBrew
```

Run these from the repository root. Upload with `arduino-cli upload --fqbn esp32:esp32:esp32 --port YOUR_USB_PORT arduino/WebBrew` after isolating heating power.

## Maintaining both versions

The canonical sources remain `src/main.cpp`, `include/controller.h` and `web/index.html`. After editing them, regenerate the committed Arduino sketch:

```sh
python3 scripts/export_arduino.py
python3 scripts/export_arduino.py --check
```

CI checks that the sketch stays in sync and compiles it using the pinned ESP32 core and library versions. End users opening the committed sketch do not need to run the exporter.
