# Filament Dryer Controller

## Overview

The Filament Dryer Controller is an ESP32-based solution designed to precisely control the temperature and humidity inside a filament drying enclosure. It features a local TFT display for real-time monitoring and control, and a comprehensive web interface accessible from any device on the same network. This project aims to provide an intelligent, automated, and user-friendly system for maintaining optimal filament conditions, crucial for high-quality 3D printing.

## Features

*   **Real-time Monitoring:** Displays current temperature and humidity on both local TFT and web UI.
*   **Multiple Operating Modes:**
    *   **DRY Mode:** Heats to a target temperature until a target humidity is reached, then transitions to a `WARM` state. Includes stall detection and re-drying logic.
    *   **HEAT Mode:** Heats to a target temperature for a user-defined duration, with configurable completion actions (Stop or Warm).
    *   **WARM Mode:** Maintains a lower temperature indefinitely to keep filament ready.
*   **Web User Interface (UI):** Responsive web interface for full control and monitoring from any browser.
*   **In-Place Editing:** Adjust settings directly on the web UI by clicking on values.
*   **Contextual UI:** Automatically shows/hides relevant settings based on the selected operating mode.
*   **Detailed Process State:** Provides a clear, chained status (e.g., `Dry / WARMING (Stalled)`) for better understanding of the controller's operation.
*   **Preset Management:**
    *   Save, Load, Rename, Delete, and Set Default presets for different filament types.
    *   Presets include all settings and the desired operating mode.
    *   Download all presets as a JSON file for backup.
*   **Real-time Logging:**
    *   Stream live log data (timestamps, events, T/H) to the web UI via WebSockets.
    *   Log events include timed intervals, heater ON/OFF, and status changes.
    *   Clear and Download log data as a CSV file from the browser.
*   **Help System:** Integrated help icons (`<i class="fas fa-info-circle"></i>`) provide contextual explanations for each setting.
*   **Persistent Settings:** Presets are stored on the ESP32's SPIFFS filesystem and persist across reboots.

## Getting Started (For Developers / Users Pulling from Git)
This section guides you through setting up the development environment, building the firmware, and flashing it to your ESP32.

### Prerequisites

*   **PlatformIO IDE:** Installed as an extension for Visual Studio Code.
*   **ESP32 Development Board:** (e.g., ESP32 DevKitC, NodeMCU-32S).
*   **Hardware Components:** SHT31 Temperature/Humidity Sensor, TFT Display (ILI9341 compatible), Solid State Relay (SSR), Heater element.

### 1. Clone the Repository

```bash
git clone <your-repo-url>
cd FilamentDryerController
```

### 2. PlatformIO Setup

Ensure your `platformio.ini` file is correctly configured.

*   **Library Dependencies:** The project relies on several libraries. These are listed in `platformio.ini` under `lib_deps`. PlatformIO will automatically install them on the first build.
    *   `bodmer/TFT_eSPI`
    *   `adafruit/Adafruit BusIO`
    *   `adafruit/Adafruit SHT31 Library`
    *   `lvgl/lvgl`
    *   `bblanchon/ArduinoJson`
    *   `esphome/ESPAsyncWebServer-esphome`
    *   `esphome/AsyncTCP-esphome`
*   **TFT_eSPI Configuration:** The `build_flags` section in `platformio.ini` contains specific definitions for your TFT display (ILI9341, pin assignments, etc.). **You may need to adjust these flags to match your specific display and wiring.**
*   **Wi-Fi Credentials:** Update the `ssid` and `password` variables in `src/main.cpp` with your local Wi-Fi network credentials.
    ```cpp
    // Configure your local Wi-Fi network credentials
    const char* ssid = "YOUR_WIFI_SSID";
    const char* password = "YOUR_WIFI_PASSWORD";
    ```
*   **Heater Pin & Serial Monitor:** The heater relay is controlled by `GPIO1` (the `TX` pin). **This means the PlatformIO Serial Monitor will be unavailable for debugging output.** All operational feedback must be viewed via the web interface or the local TFT display.

### 3. Upload Firmware

1.  Open the project in VS Code with the PlatformIO extension.
2.  Build the project (PlatformIO: Build).
3.  Connect your ESP32 board to your computer via USB.
4.  Upload the firmware (PlatformIO: Upload).

### 4. Upload Filesystem Image (SPIFFS)

The web interface (`index.html`) and presets (`presets.json`) are stored on the ESP32's SPIFFS filesystem.

1.  **Prepare `data` directory:**
    *   Ensure your `index.html` is in the `data/` directory.
    *   Create a `presets.json` file in the `data/` directory. You can use the example provided in the repository or create your own. This file will be uploaded as the initial set of presets.
2.  Run the PlatformIO task: **"Upload Filesystem Image"**.

### 5. Access the Web Interface

1.  After uploading both firmware and filesystem, open the PlatformIO Serial Monitor.
2.  The ESP32 will connect to your Wi-Fi network. Since the Serial Monitor is disabled by the heater pin, you must **find the device's IP address on the local TFT display**.
3.  Open a web browser on a device connected to the *same Wi-Fi network* and navigate to the IP address shown on the TFT screen.

## Out-of-Box Setup (For End-Users)
## Web Interface (UI) Overview

The web interface provides a comprehensive dashboard for your filament dryer.

*   **Real-time Data:** Top section displays live Temperature and Humidity readings.
*   **Presets:** Manage your filament-specific settings.
*   **Mode Selection:** Choose between Dry, Heat, and Warm operating modes.
*   **Control & Status:** Monitor Heater Status, Process Control (Enable/Disable), and detailed Process State.
*   **Temperature Settings:** Configure target temperatures and heat duration.
*   **Humidity Settings:** Configure humidity setpoints, hysteresis, and stall detection (visible only in Dry mode).
*   **Logging:** View real-time log data, start/stop logging, clear logs, and download as CSV.

### Interaction

*   **In-Place Editing:** Click directly on any setting value (e.g., `50.0 °C`) to bring up an input field and change it. Press Enter to save or Escape to cancel.
*   **Help Icons:** Click the ` <i class="fas fa-info-circle"></i> ` icon next to labels for contextual explanations.
*   **Process Control:** Click the `ENABLED` or `DISABLED` text in the "Process Control" box to toggle the master switch.

## Presets Management

Presets allow you to quickly switch between optimized settings for different filament types.

*   **Load:** Select a preset from the dropdown; its settings will automatically apply.
*   **Save Current As...:** Adjust settings as desired, then click this button to save them under a new name.
*   **Update Selected:** (Implicit: If you load a preset, change a setting, and then click "Save Current As..." with the *same name*, it will update that preset.)
*   **Set as Default:** Select a preset and click this button to make it the one loaded automatically on device startup.
*   **Rename:** Change the name of the currently selected preset.
*   **Delete:** Remove the currently selected preset.
*   **Download All:** Saves all presets currently on the device to a `presets_backup.json` file on your computer.

### Manual Presets Editing

You can manually edit the `presets.json` file on your computer (located in the `data/` directory of this project). This is useful for bulk changes or creating a "master" set.

*   **Units:** The `presets.json` file stores `heatDur` in hours, `logInt` in minutes, and `stallInterval` in minutes, matching the web UI.
*   **Coded Values:** The `_metadata` object at the top of `presets.json` provides mappings for coded values like `mode` (0=Dry, 1=Heat, 2=Warm) and `heatAction` (0=Stop, 1=Warm).
*   **Overwriting:** If you make changes via the web UI, they are saved on the ESP32. If you later upload a `presets.json` from your computer using "Upload Filesystem Image", it will overwrite any changes made via the web UI.

## Logging

The logging feature provides real-time data for monitoring and analysis.

*   **Start/Stop:** Begin or pause log data collection.
*   **Clear:** Erase all accumulated log entries in the browser.
*   **Download CSV:** Save the current log data from the browser as a `dryer_log.csv` file.
*   **Log Interval:** Configure how often `TIMED` log entries are generated (in minutes). Set to 0 for event-only logging.
*   **Log Record Format:** `Timestamp,Event,Temperature,Humidity`
    *   `Timestamp`: Elapsed time since logging started (HH:MM:SS).
    *   `Event`: `TIMED`, `HEAT_ON`, `HEAT_OFF`, `STATUS_IDLE`, `STATUS_DRYING`, `STATUS_WARMING_STALLED`, etc.
*   **"Fire and Forget":** Log data is streamed directly to your browser via WebSockets. The ESP32 does not store historical logs, ensuring minimal memory usage. Data is lost if the browser page is refreshed or closed.

## Troubleshooting

*   **Wi-Fi Connection Issues:**
    *   Check your `ssid` and `password` in `src/main.cpp` (for developer setup).
    *   Ensure your router is broadcasting on 2.4GHz.
    *   Verify the ESP32 is within range of your Wi-Fi network.
*   **Sensor Errors:** If temperature/humidity show "Error", check wiring to the SHT31 sensor (SDA=27, SCL=22).
*   **UI Not Updating:** Ensure your browser is connected to the correct IP address and the ESP32 is powered on and connected to Wi-Fi.
*   **Presets Not Loading:** If `presets.json` is malformed, the ESP32 will create default presets. Since the Serial Monitor is disabled, check the web UI's real-time log for any error messages that may appear on startup.

## License

This project is open-source and licensed under the MIT License. See the `LICENSE` file for details.
