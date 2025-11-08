#include <Arduino.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include "Adafruit_SHT31.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "SPIFFS.h"
#include <ArduinoJson.h>

/* LVGL Globals */
TFT_eSPI tft = TFT_eSPI();
static const uint16_t screenWidth  = 320;
static const uint16_t screenHeight = 240;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[screenWidth * 10];

/* Sensor Globals */
Adafruit_SHT31 sht31 = Adafruit_SHT31();
float currentTemperature = 0.0; // Global to store latest temp
float currentHumidity = 0.0;    // Global to store latest hum

/* Hardware Pins */
const int HEATER_PIN = 1; // GPIO 1 (TX pin) for the ZGT-25 DA relay
bool isHeaterOn = false;  // Tracks the actual state of the heater relay

/* Network Globals */
#include "wifi_credentials.h" // Your WiFi credentials should be in this file
AsyncWebServer server(80);
AsyncWebSocket ws("/ws"); // Create a WebSocket object


/* Settings & State */
struct Preset {
  String name;
  String notes;
  bool isDefault = false;
  float dryingTemp;
  float setpointHum;
  float warmTemp;
  float humHyst;
  uint32_t stallInterval;
  float stallDelta;
  uint32_t heatDur;
  int heatAction;
  uint32_t logInt;
  int mode; // 0=Dry, 1=Heat, 2=Warm

  // Default constructor (important for std::vector and other contexts)
  Preset() : name(""), notes(""), isDefault(false), dryingTemp(0.0f), setpointHum(0.0f),
             warmTemp(0.0f), humHyst(0.0f), stallInterval(0U), stallDelta(0.0f),
             heatDur(0U), heatAction(0), logInt(0U) {}

  // Parameterized constructor for easy initialization
  Preset(String _name, String _notes, bool _isDefault, float _dryingTemp, float _setpointHum,
         float _warmTemp, float _humHyst, uint32_t _stallInterval,
         float _stallDelta, uint32_t _heatDur, int _heatAction, uint32_t _logInt, int _mode)
    : name(_name), notes(_notes), isDefault(_isDefault), dryingTemp(_dryingTemp), 
      setpointHum(_setpointHum), warmTemp(_warmTemp), humHyst(_humHyst), stallInterval(_stallInterval),
      stallDelta(_stallDelta), heatDur(_heatDur), heatAction(_heatAction), logInt(_logInt),
      mode(_mode) {}
};

std::vector<Preset> presets;

String currentNotes = "";
float dryingTemperature = 50.0;
float setpointHumidity = 30.0;
float warmTemperature = 35.0;
float humidityHysteresis = 5.0; // %RH to allow humidity to rise before re-engaging drying
float effectiveSetpointHumidity = 30.0; // The dynamically adjusted humidity target
uint32_t stallCheckInterval = 1800000; // 30 minutes in milliseconds
float stallHumidityDelta = 0.5; // Minimum %RH drop required over the interval to not be considered stalled
uint32_t heatDuration = 240 * 60000; // 4 hours in milliseconds
uint32_t heatStartTime = 0;

uint32_t lastStallCheckTime = 0;
float humidityAtLastStallCheck = 0;

enum State {
  STATE_IDLE,
  STATE_DRYING,
  STATE_HEATING,
  STATE_WARMING
};
enum Mode {
  MODE_DRY,
  MODE_HEAT,
  MODE_WARM
};
enum HeatCompletionAction {
  ACTION_STOP,
  ACTION_WARM
};
enum TransitionReason {
  REASON_NONE,
  REASON_USER_ACTION,
  REASON_TARGET_MET,
  REASON_STALLED,
  REASON_TIMER_EXPIRED,
  REASON_HYSTERESIS
};
State currentState = STATE_IDLE;
Mode selectedMode = MODE_DRY; // Default to Dry mode
HeatCompletionAction heatCompletionAction = ACTION_STOP;
TransitionReason lastTransitionReason = REASON_NONE;
bool isHeaterEnabled = false; // Master switch for the heating process, OFF by default for safety

/* Logging State */
bool isLoggingEnabled = false;
uint32_t loggingStartTime = 0;
uint32_t logIntervalMillis = 60000; // Default 1 minute
uint32_t lastTimedLogTime = 0;

enum MessageType { MSG_INFO, MSG_ERROR };
struct WebMessage {
  String text;
  MessageType type;
};

/* Web Message Queue */
std::vector<WebMessage> webMessageQueue;

/* UI Object Globals */
String currentStatusString = "IDLE";
lv_obj_t * temp_label_value;
lv_obj_t * hum_label_value;
lv_obj_t * message_label;
lv_obj_t * setpoint_label_value;
lv_obj_t * heater_status_label;
lv_obj_t * state_label;
lv_obj_t * hum_setpoint_label_value;
static lv_style_t style_error;

/* Forward Declarations */
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p);
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
void setupWiFi();
void setupWebServer();
void setupHardwarePins();
void ui_init();
void update_humidity_setpoint_display();
void loadPresets();
void savePresets();
void applyPreset(const Preset& preset);
void update_setpoint_display();
void update_process_status_display();
void update_heater_status_display();
void update_message_box(const char* message);
void heater_enable_switch_event_handler(lv_event_t * e);
void setupSensor();
void update_sensor_task(lv_timer_t * timer);
void controlHeaterTask(lv_timer_t * timer);
void logToWeb(String message, MessageType type = MSG_INFO);

void logToWeb(String message, MessageType type) {
  // Prevent queuing the same message consecutively.
  // This stops floods of identical messages (e.g., from a sensor error).
  if (!webMessageQueue.empty() && webMessageQueue.back().text == message) {
    return; // Don't add duplicate message
  }

  if (webMessageQueue.size() < 10) { // Limit queue size to prevent memory issues
    webMessageQueue.push_back({message, type});
  }
}

void setup() {
  Serial.begin(115200);

  // --- TFT_eSPI Display Initialization ---
  tft.begin();
  tft.setRotation(1);

  // Backlight workaround (must be after tft.begin())
  ledcSetup(0, 5000, 8);
  ledcAttachPin(21, 0);
  ledcWrite(0, 255);

  // --- LVGL Initialization ---
  lv_init();

  // --- Initialize SPIFFS ---
  if(!SPIFFS.begin(true)){
    // We can't log here as UI isn't ready, but this prevents a crash.
  }
  lv_disp_draw_buf_init(&draw_buf, buf, NULL, screenWidth * 10);

  /*Initialize the display*/
  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = screenWidth;
  disp_drv.ver_res = screenHeight;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  // --- Create the UI ---
  // This MUST be done before calling functions that update UI elements.
  ui_init();

  // --- Load Presets ---
  loadPresets();

  // --- Hardware Pin Setup ---
  setupHardwarePins();

  // --- Sensor Initialization ---
  setupSensor();

  // --- Network Initialization ---
  setupWiFi();
  setupWebServer();

  // --- Create a task to update sensor data ---
  lv_timer_create(update_sensor_task, 2000, NULL);

  // --- Create a task to control the heater ---
  lv_timer_create(controlHeaterTask, 1000, NULL); // Run thermostat logic every second
}

void loop() {
  lv_timer_handler(); // let the LVGL timer handler do the work
  delay(5);
}

void applyPreset(const Preset& preset) {
  currentNotes = preset.notes;
  dryingTemperature = preset.dryingTemp;
  setpointHumidity = preset.setpointHum;
  warmTemperature = preset.warmTemp;
  humidityHysteresis = preset.humHyst;
  stallCheckInterval = preset.stallInterval;
  stallHumidityDelta = preset.stallDelta;
  heatDuration = preset.heatDur;
  heatCompletionAction = (HeatCompletionAction)preset.heatAction;
  logIntervalMillis = preset.logInt;
  selectedMode = (Mode)preset.mode; // Apply the mode from the preset

  // Update any relevant UI elements immediately
  update_setpoint_display();
  update_humidity_setpoint_display();
}

void loadPresets() {
  File file = SPIFFS.open("/presets.json", "r");
  if (!file || file.size() == 0) {
    logToWeb("Presets file not found. Creating defaults.");
    // Create default presets
    presets.clear();
    Preset p1("PLA - Generic", "Standard PLA drying settings.", true, 50.0f, 30.0f, 35.0f, 5.0f, 30 * 60000U, 0.5f, 4 * 3600000U, 0, 1 * 60000U, 0); // Dry Mode
    Preset p2("PETG - Strong", "Aggressive PETG drying.", false, 65.0f, 15.0f, 40.0f, 3.0f, 60 * 60000U, 0.2f, 8 * 3600000U, 1, 5 * 60000U, 0); // Dry Mode
    presets.push_back(p1);
    presets.push_back(p2);
    savePresets();
    applyPreset(p1); // Apply the first default
    return;
  }

  StaticJsonDocument<4096> doc; // Increased size for more presets
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    logToWeb("Failed to parse presets.json. Check file for errors.", MSG_ERROR);
    return;
  }

  JsonArray array = doc.as<JsonArray>();
  presets.clear();
  bool defaultLoaded = false;
  for (JsonObject obj : array) {
    // If the object is our metadata block, skip it.
    if (obj.containsKey("_metadata")) continue;
    Preset p;
    p.name = obj["name"].as<String>();
    p.notes = obj["notes"].as<String>(); // This is safe, returns "" if 'notes' is missing
    p.isDefault = obj["isDefault"];
    p.dryingTemp = obj["dryingTemp"];
    p.setpointHum = obj["setpointHum"];
    p.warmTemp = obj["warmTemp"];
    p.humHyst = obj["humHyst"];
    p.stallInterval = obj["stallInterval"].as<unsigned long>() * 60000UL; // Use unsigned long for safe math
    p.stallDelta = obj["stallDelta"];
    p.heatDur = (uint32_t)(obj["heatDur"].as<float>() * 3600000.0f); // Keep float for hours, but cast result
    p.heatAction = obj["heatAction"];
    p.logInt = obj["logInt"].as<unsigned long>() * 60000UL; // Use unsigned long for safe math
    p.mode = obj["mode"];
    presets.push_back(p);

    if (p.isDefault && !defaultLoaded) {
      applyPreset(p);
      defaultLoaded = true;
    }
  }

  // If no default was found, apply the first preset as a fallback
  if (!defaultLoaded && !presets.empty()) {
    applyPreset(presets[0]);
  }
  // This message is too noisy for startup, so it's commented out.
  // logToWeb("Presets loaded successfully.");
}

void savePresets() {
  File file = SPIFFS.open("/presets.json", "w");
  if (!file) {
    logToWeb("Error: Failed to open presets.json for writing.", MSG_ERROR);
    return;
  }

  StaticJsonDocument<4096> doc; // Increased size for more presets
  JsonArray array = doc.to<JsonArray>();

  for (const auto& p : presets) {
    JsonObject obj = array.createNestedObject();
    obj["name"] = p.name;
    obj["notes"] = p.notes;
    obj["isDefault"] = p.isDefault;
    obj["dryingTemp"] = p.dryingTemp;
    obj["setpointHum"] = p.setpointHum;
    obj["warmTemp"] = p.warmTemp;
    obj["humHyst"] = p.humHyst;
    obj["stallInterval"] = (float)p.stallInterval / 60000.0f; // Convert ms to minutes for JSON
    obj["stallDelta"] = p.stallDelta;
    obj["heatDur"] = (float)p.heatDur / 3600000.0f; // Convert ms to hours for JSON
    obj["heatAction"] = p.heatAction;
    obj["logInt"] = (float)p.logInt / 60000.0f; // Convert ms to minutes for JSON
    obj["mode"] = p.mode;
  }

  if (serializeJson(doc, file) == 0) {
    logToWeb("Error: Failed to write to presets.json.", MSG_ERROR);
  } else {
    logToWeb("Presets saved successfully.", MSG_INFO);
  }
  file.close();
}

/* Display flushing */
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t *)color_p, w * h, true);
  tft.endWrite();
  lv_disp_flush_ready(disp);
}

void setupSensor() {
  Wire.begin(27, 22); // SDA=27, SCL=22
  if (!sht31.begin(0x44)) {
    // Do NOT block here. Log the error and allow the system to continue.
    update_message_box("Sensor Init Failed!");
    logToWeb("CRITICAL: SHT31 sensor initialization failed!", MSG_ERROR);
  }
}

void setupWiFi() {
  update_message_box("Connecting to WiFi...");

  WiFi.begin(ssid, password);
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    if (++retries > 20) { // 10-second timeout
      update_message_box("WiFi Connect Failed!");
      logToWeb("WiFi connection failed. Check credentials.", MSG_ERROR);
      return;
    }
  }

  char msgBuffer[50];
  sprintf(msgBuffer, "IP: %s", WiFi.localIP().toString().c_str());
  logToWeb("WiFi Connected. IP: " + WiFi.localIP().toString());
  update_message_box(msgBuffer);
}

void setupWebServer() {
  // Route for the main web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/index.html", "text/html");
  });

  // Route for sensor readings (JSON endpoint)
  server.on("/readings", HTTP_GET, [](AsyncWebServerRequest *request){
    String json = "{";
    json += "\"temperature\":" + (isnan(currentTemperature) ? "null" : String(currentTemperature, 1));
    json += ",\"humidity\":" + (isnan(currentHumidity) ? "null" : String(currentHumidity, 1));
    json += ",\"drying_temp\":";
    json += String(dryingTemperature, 1);
    json += ",\"setpoint_hum\":";
    json += String(setpointHumidity, 1);
    json += ",\"warm_temp\":";
    json += String(warmTemperature, 1);
    json += ",\"process_state\":\"" + currentStatusString + "\"";
    json += ",\"heater_on\":";
    json += isHeaterOn ? "true" : "false";
    json += ",\"is_enabled\":";
    json += isHeaterEnabled ? "true" : "false";
    json += ",\"hum_hyst\":";
    json += String(humidityHysteresis, 1);
    json += ",\"stall_interval\":";
    json += String(stallCheckInterval);
    json += ",\"stall_delta\":";
    json += String(stallHumidityDelta, 1);
    json += ",\"heat_duration\":";
    json += String(heatDuration);
    json += ",\"heat_remaining\":";
    long remaining = 0;
    if (currentState == STATE_HEATING && isHeaterEnabled) remaining = heatDuration - (millis() - heatStartTime);
    json += String(remaining > 0 ? remaining : 0);
    json += ",\"log_interval\":";
    json += String(logIntervalMillis / 60000.0, 1);
    json += ",\"selected_mode\":";
    json += String(selectedMode);
    json += ",\"heat_action\":\"" + String(heatCompletionAction == ACTION_STOP ? "Stop" : "Warm") + "\"";
    json += "}";
    request->send(200, "application/json", json);
  });

  // --- Logging Endpoints ---
  server.on("/start_log", HTTP_POST, [](AsyncWebServerRequest *request){
    isLoggingEnabled = true;
    loggingStartTime = millis();
    lastTimedLogTime = loggingStartTime; // Reset timed log on start
    // Send header as first log entry
    String header = "SETUP,DryingTemp:" + String(dryingTemperature) + ",WarmingTemp:" + String(warmTemperature) + ",HumSet:" + String(setpointHumidity) + ",HumHyst:" + String(humidityHysteresis) + ",StallInt:" + String(stallCheckInterval/60000) + ",StallDelta:" + String(stallHumidityDelta) + ",HeatDur:" + String(heatDuration/3600000.0, 1) + ",HeatAction:" + (heatCompletionAction == ACTION_STOP ? "Stop" : "Warm") + ",LogIntervalMin:" + String(logIntervalMillis / 60000.0, 1);
    ws.textAll(header);
    request->send(200, "text/plain", "OK");
  });
  server.on("/stop_log", HTTP_POST, [](AsyncWebServerRequest *request){
    isLoggingEnabled = false;
    request->send(200, "text/plain", "OK");
  });

  // Route to set the log interval
  server.on("/setloginterval", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("value", true)) {
      float minutes = request->getParam("value", true)->value().toFloat();
      if (minutes > 0) {
        logIntervalMillis = minutes * 60000;
      }
      request->send(200, "text/plain", "OK");
    } else {
      request->send(400, "text/plain", "Bad Request");
    }
  });


  // --- Message Queue Endpoint ---
  server.on("/getmessage", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!webMessageQueue.empty()) {
      WebMessage msg = webMessageQueue.front();
      webMessageQueue.erase(webMessageQueue.begin()); // Dequeue the message
      String json = "{\"type\":\"" + String(msg.type == MSG_INFO ? "info" : "error") + "\",";
      json += "\"text\":\"" + msg.text + "\"}";
      request->send(200, "application/json", json);
    } else {
      request->send(204, "text/plain", ""); // Send "No Content" if no message
    }
  });


  // --- Preset Endpoints ---
  server.on("/presets/list", HTTP_GET, [](AsyncWebServerRequest *request){
    // Use ArduinoJson to safely serialize the list. This correctly handles
    // special characters in any field, including the 'notes' field.
    StaticJsonDocument<2048> doc;
    JsonArray array = doc.to<JsonArray>();
    for (const auto& p : presets) {
      JsonObject obj = array.createNestedObject();
      obj["name"] = p.name;
      obj["isDefault"] = p.isDefault;
      obj["notes"] = p.notes;
    }
    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
  });

  server.on("/presets/load", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("name", true)) {
      String name = request->getParam("name", true)->value();
      for (const auto& p : presets) {
        if (p.name == name) {
          applyPreset(p);
          request->send(200, "text/plain", "OK");
          return;
        }
      }
    }
    request->send(404, "text/plain", "Preset not found");
  });

  server.on("/presets/download", HTTP_GET, [](AsyncWebServerRequest *request){
    File file = SPIFFS.open("/presets.json", "r");
    if (!file) {
      request->send(500, "text/plain", "Could not read presets file.");
      return;
    }
    String content = file.readString();
    file.close();
    request->send(200, "application/json", content);
  });

  server.on("/presets/save", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("name", true)) {
      String notes_from_request = "";
      if (request->hasParam("notes", true)) {
        notes_from_request = request->getParam("notes", true)->value();
      }

      String name = request->getParam("name", true)->value();
      // Check if preset with this name already exists to update it
      for (auto& p : presets) {
        if (p.name == name) {
          // Update existing preset
          p.notes = notes_from_request; 
          p.dryingTemp = dryingTemperature; p.setpointHum = setpointHumidity; p.warmTemp = warmTemperature; p.humHyst = humidityHysteresis;
          p.stallInterval = stallCheckInterval; p.stallDelta = stallHumidityDelta; p.heatDur = heatDuration;
          p.heatAction = heatCompletionAction; p.logInt = logIntervalMillis; p.mode = selectedMode;
          savePresets();
          request->send(200, "text/plain", "Updated");
          return;
        }
      }
      // If not found, create a new one
      Preset p_new;
      p_new.name = name;
      p_new.notes = notes_from_request; 
      p_new.dryingTemp = dryingTemperature; p_new.setpointHum = setpointHumidity; p_new.warmTemp = warmTemperature; p_new.humHyst = humidityHysteresis;
      p_new.stallInterval = stallCheckInterval; p_new.stallDelta = stallHumidityDelta; p_new.heatDur = heatDuration;
      p_new.heatAction = heatCompletionAction; p_new.logInt = logIntervalMillis; p_new.mode = selectedMode;
      presets.push_back(p_new);
      savePresets();
      request->send(200, "text/plain", "Saved");
    } else {
      request->send(400, "text/plain", "Bad Request");
    }
  });

  server.on("/presets/delete", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("name", true)) {
      String name = request->getParam("name", true)->value();
      presets.erase(std::remove_if(presets.begin(), presets.end(), [&](const Preset& p){ return p.name == name; }), presets.end());
      savePresets();
      request->send(200, "text/plain", "Deleted");
    } else {
      request->send(400, "text/plain", "Bad Request");
    }
  });

  server.on("/presets/rename", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("old_name", true) && request->hasParam("new_name", true)) {
      String old_name = request->getParam("old_name", true)->value();
      String new_name = request->getParam("new_name", true)->value();

      for (auto& p : presets) {
        if (p.name == old_name) {
          p.name = new_name;
          savePresets();
          request->send(200, "text/plain", "Renamed");
          return;
        }
      }
      request->send(404, "text/plain", "Preset not found");
    } else {
      request->send(400, "text/plain", "Bad Request");
    }
  });

  server.on("/presets/setdefault", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("name", true)) {
      String name = request->getParam("name", true)->value();
      for (auto& p : presets) { p.isDefault = (p.name == name); }
      savePresets();
      request->send(200, "text/plain", "OK");
    } else {
      request->send(400, "text/plain", "Bad Request");
    }
  });

  // Route to set the notes
  server.on("/setnotes", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("value", true)) {
      currentNotes = request->getParam("value", true)->value();
      request->send(200, "text/plain", "OK");
    } else {
      request->send(400, "text/plain", "Bad Request");
    }
  });

  // Route to set the temperature setpoint
  server.on("/setdryingtemp", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("value", true)) { // "true" means it's a POST parameter
      String value = request->getParam("value", true)->value();
      dryingTemperature = value.toFloat();
      update_setpoint_display(); // Update the LVGL display immediately
      request->send(200, "text/plain", "OK");
    } else {
      request->send(400, "text/plain", "Bad Request");
    }
  });

  // Route to set the humidity setpoint
  server.on("/setpointhum", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("value", true)) {
      String value = request->getParam("value", true)->value();
      setpointHumidity = value.toFloat();
      update_humidity_setpoint_display(); // Update the LVGL display immediately
      request->send(200, "text/plain", "OK");
    } else {
      request->send(400, "text/plain", "Bad Request");
    }
  });

  // Route to set the maintenance temperature setpoint
  server.on("/setwarmtemp", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("value", true)) {
      String value = request->getParam("value", true)->value();
      warmTemperature = value.toFloat();
      request->send(200, "text/plain", "OK");
    } else {
      request->send(400, "text/plain", "Bad Request");
    }
  });

  // Route to set the humidity hysteresis
  server.on("/sethumhyst", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("value", true)) {
      String value = request->getParam("value", true)->value();
      humidityHysteresis = value.toFloat();
      request->send(200, "text/plain", "OK");
    } else {
      request->send(400, "text/plain", "Bad Request");
    }
  });

  // Route to set the stall check interval
  server.on("/setstallinterval", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("value", true)) {
      String value = request->getParam("value", true)->value();
      stallCheckInterval = value.toInt();
      request->send(200, "text/plain", "OK");
    } else {
      request->send(400, "text/plain", "Bad Request");
    }
  });

  // Route to set the stall delta
  server.on("/setstalldelta", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("value", true)) {
      String value = request->getParam("value", true)->value();
      stallHumidityDelta = value.toFloat();
      request->send(200, "text/plain", "OK");
    } else {
      request->send(400, "text/plain", "Bad Request");
    }
  });

  // Route to set the control mode
  server.on("/setmode", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("mode", true)) {
      int mode = request->getParam("mode", true)->value().toInt();
      if (mode >= 0 && mode <= 2) {
        selectedMode = (Mode)mode;

        // If the process is already running, force an immediate state change.
        if (isHeaterEnabled) {
          switch(selectedMode) {
            case MODE_DRY:
              currentState = STATE_DRYING;
              // Reset stall detection on mode change
              lastStallCheckTime = millis();
              humidityAtLastStallCheck = currentHumidity;
              lastTransitionReason = REASON_USER_ACTION;
              break;
            case MODE_HEAT:
              currentState = STATE_HEATING;
              heatStartTime = millis(); // Restart the heat timer
              break;
            case MODE_WARM:
              currentState = STATE_WARMING;
              lastTransitionReason = REASON_USER_ACTION;
              break;
          }
        }
      }
      request->send(200, "text/plain", "OK");
    } else {
      request->send(400, "text/plain", "Bad Request");
    }
  });

  // Route to set the heat duration
  server.on("/setheatduration", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("value", true)) {
      float hours = request->getParam("value", true)->value().toFloat();
      heatDuration = hours * 3600000; // Convert hours to milliseconds
      request->send(200, "text/plain", "OK");
    } else {
      request->send(400, "text/plain", "Bad Request");
    }
  });

  // Route to set the heat completion action
  server.on("/setheataction", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("action", true)) {
      int action = request->getParam("action", true)->value().toInt();
      if (action == 0) heatCompletionAction = ACTION_STOP;
      else if (action == 1) heatCompletionAction = ACTION_WARM;
      request->send(200, "text/plain", "OK");
    } else {
      request->send(400, "text/plain", "Bad Request");
    }
  });

  // Route to toggle the master enable state
  server.on("/toggle_enable", HTTP_POST, [](AsyncWebServerRequest *request){
    isHeaterEnabled = !isHeaterEnabled;
    lastTransitionReason = REASON_USER_ACTION;
    request->send(200, "text/plain", "OK");
  });

  // Attach the WebSocket handler
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.begin();
}

void setupHardwarePins() {
  // Heater Relay Pin
  pinMode(HEATER_PIN, OUTPUT);
  digitalWrite(HEATER_PIN, LOW); // Ensure heater is off initially
}

void ui_init() {
  // --- Define Styles ---
  static lv_style_t style_title;
  lv_style_init(&style_title);
  lv_style_set_text_font(&style_title, &lv_font_montserrat_24);

  static lv_style_t style_label;
  lv_style_init(&style_label);
  lv_style_set_text_font(&style_label, &lv_font_montserrat_16);

  static lv_style_t style_value;
  lv_style_init(&style_value);
  lv_style_set_text_font(&style_value, &lv_font_montserrat_28);

  static lv_style_t style_setpoint;
  lv_style_init(&style_setpoint);
  lv_style_set_text_font(&style_setpoint, &lv_font_montserrat_28);
  lv_style_set_text_color(&style_setpoint, lv_color_hex(0x00FFFF)); // Cyan

  // Style for the humidity setpoint value
  static lv_style_t style_setpoint_hum;
  lv_style_init(&style_setpoint_hum);
  lv_style_set_text_font(&style_setpoint_hum, &lv_font_montserrat_28);
  lv_style_set_text_color(&style_setpoint_hum, lv_color_hex(0xFF00FF)); // Magenta

  lv_style_init(&style_error);
  lv_style_set_text_font(&style_error, &lv_font_montserrat_28);
  lv_style_set_text_color(&style_error, lv_color_hex(0xFFFF00)); // Yellow

  static lv_style_t style_message;
  lv_style_init(&style_message);
  lv_style_set_text_color(&style_message, lv_color_hex(0xFFFF00)); // Yellow

  // --- Create UI Layout (3-Column Design) ---
  // Column 1: Labels, Column 2: Values
  const int col1_x = 10;
  const int col2_x = 120;

  lv_obj_t * title_label = lv_label_create(lv_scr_act());
  lv_obj_set_width(title_label, 200);
  lv_label_set_text(title_label, "Filament Dryer");
  lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 10);
  lv_obj_add_style(title_label, &style_title, 0);

  // --- Temperature Row ---
  lv_obj_t * temp_label_static = lv_label_create(lv_scr_act());
  lv_label_set_text(temp_label_static, "Temp");
  lv_obj_add_style(temp_label_static, &style_label, 0);
  lv_obj_align(temp_label_static, LV_ALIGN_TOP_LEFT, col1_x, 60);

  temp_label_value = lv_label_create(lv_scr_act());
  lv_obj_add_style(temp_label_value, &style_value, 0);
  lv_obj_align(temp_label_value, LV_ALIGN_TOP_LEFT, col2_x, 55);

  // Create the Temp Setpoint value label (Column 3)
  setpoint_label_value = lv_label_create(lv_scr_act());
  lv_obj_add_style(setpoint_label_value, &style_setpoint, 0);
  lv_obj_align(setpoint_label_value, LV_ALIGN_TOP_LEFT, 220, 55); // Position in 3rd column

  // --- Humidity Row ---
  lv_obj_t * hum_label_static = lv_label_create(lv_scr_act());
  lv_label_set_text(hum_label_static, "Humid:");
  lv_obj_add_style(hum_label_static, &style_label, 0);
  lv_obj_align(hum_label_static, LV_ALIGN_TOP_LEFT, col1_x, 120);

  hum_label_value = lv_label_create(lv_scr_act());
  lv_obj_add_style(hum_label_value, &style_value, 0);
  lv_obj_align(hum_label_value, LV_ALIGN_TOP_LEFT, col2_x, 115);

  // Create the Humidity Setpoint value label (Column 3)
  hum_setpoint_label_value = lv_label_create(lv_scr_act());
  lv_obj_add_style(hum_setpoint_label_value, &style_setpoint_hum, 0);
  lv_obj_align(hum_setpoint_label_value, LV_ALIGN_TOP_LEFT, 220, 115); // Position in 3rd column

  // --- Heater Status ---
  lv_obj_t * heater_label_static = lv_label_create(lv_scr_act());
  lv_label_set_text(heater_label_static, "Heater:");
  lv_obj_add_style(heater_label_static, &style_label, 0);
  lv_obj_align(heater_label_static, LV_ALIGN_TOP_LEFT, col1_x, 165);

  heater_status_label = lv_label_create(lv_scr_act());
  lv_obj_add_style(heater_status_label, &style_value, 0);
  lv_obj_align(heater_status_label, LV_ALIGN_TOP_LEFT, col2_x, 165);
  update_heater_status_display();

  // --- State Display ---
  state_label = lv_label_create(lv_scr_act());
  lv_obj_add_style(state_label, &style_setpoint, 0); // Use cyan style
  lv_obj_set_width(state_label, 300);
  lv_obj_set_style_text_align(state_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(state_label, LV_ALIGN_BOTTOM_MID, 0, -35);
  update_process_status_display();

  // --- Message Box ---
  message_label = lv_label_create(lv_scr_act());
  lv_label_set_text(message_label, "Initializing...");
  lv_obj_add_style(message_label, &style_message, 0);
  lv_obj_set_width(message_label, screenWidth - 20);
  lv_label_set_long_mode(message_label, LV_LABEL_LONG_WRAP);
  lv_obj_align(message_label, LV_ALIGN_BOTTOM_LEFT, 10, -2);

  // Set initial placeholder text for dynamic labels
  lv_label_set_text(temp_label_value, "--.- C");
  lv_label_set_text(hum_label_value, "--.- %");
  update_setpoint_display(); // Set initial value
  update_humidity_setpoint_display();
}

void update_setpoint_display() {
  char setpointStr[8];
  dtostrf(dryingTemperature, 4, 1, setpointStr);
  lv_label_set_text_fmt(setpoint_label_value, "%s C", setpointStr);
}

void update_humidity_setpoint_display() {
  char setpointStr[8];
  dtostrf(setpointHumidity, 4, 1, setpointStr);
  lv_label_set_text_fmt(hum_setpoint_label_value, "%s %%", setpointStr);
}

void update_heater_status_display() {
  if (isHeaterOn) {
    lv_obj_set_style_text_color(heater_status_label, lv_color_hex(0xFF0000), 0); // Red
    lv_label_set_text(heater_status_label, "ON");
  } else {
    lv_obj_set_style_text_color(heater_status_label, lv_color_hex(0x808080), 0); // Grey
    lv_label_set_text(heater_status_label, "OFF");
  }
}

void update_process_status_display() {
  // This function now just updates the LVGL label with the global status string
  lv_label_set_text(state_label, currentStatusString.c_str());
}

void update_message_box(const char* message) {
  lv_label_set_text(message_label, message);
}

void sendLog(String event) {
  if (!isLoggingEnabled) return;

  // Calculate elapsed time HH:MM:SS
  uint32_t elapsed_ms = millis() - loggingStartTime;
  uint32_t h = elapsed_ms / 3600000;
  uint32_t m = (elapsed_ms % 3600000) / 60000;
  uint32_t s = (elapsed_ms % 60000) / 1000;
  char timeStr[10];
  sprintf(timeStr, "%02d:%02d:%02d", h, m, s);

  // Format: Timestamp,Event,Temp,Humidity
  String logEntry = String(timeStr) + "," + event + "," + String(currentTemperature, 1) + "," + String(currentHumidity, 1);
  
  ws.textAll(logEntry);
  Serial.println("Log: " + logEntry);
}

void update_sensor_task(lv_timer_t * timer) {
  float t = sht31.readTemperature();
  float h = sht31.readHumidity();

  char msgBuffer[50];

  if (isnan(t) || isnan(h)) {
    currentTemperature = NAN; // Indicate error
    currentHumidity = NAN;    // Indicate error

    lv_obj_add_style(temp_label_value, &style_error, 0);
    lv_obj_add_style(hum_label_value, &style_error, 0);
    lv_label_set_text(temp_label_value, "Error");
    lv_label_set_text(hum_label_value, "Error");
    update_message_box("Sensor read error!");
    logToWeb("Sensor read error! Check wiring.", MSG_ERROR);
  } else {
    currentTemperature = t; // Update global
    currentHumidity = h;    // Update global

    lv_obj_remove_style(temp_label_value, &style_error, 0);
    lv_obj_remove_style(hum_label_value, &style_error, 0);

    char tempStr[8];
    dtostrf(t, 4, 1, tempStr);
    lv_label_set_text_fmt(temp_label_value, "%s C", tempStr);

    char humStr[8];
    dtostrf(h, 4, 1, humStr);
    lv_label_set_text_fmt(hum_label_value, "%s %%", humStr);

    sprintf(msgBuffer, "Temp: %.1f C, Humidity: %.1f %%", t, h);
    update_message_box(msgBuffer);
  }
}

void controlHeaterTask(lv_timer_t * timer) {
  // --- Process State Machine ---
  State previousState = currentState;

  if (!isHeaterEnabled) {
    currentState = STATE_IDLE;
    if (previousState != STATE_IDLE) lastTransitionReason = REASON_USER_ACTION;
  } else {
    // If enabled, decide whether to start or continue a process
    if (currentState == STATE_IDLE) {
      
      // Transition to the user's selected mode and perform initial setup
      if (selectedMode == MODE_DRY) {
        currentState = STATE_DRYING;
        lastStallCheckTime = millis();
        humidityAtLastStallCheck = currentHumidity;
        effectiveSetpointHumidity = setpointHumidity;
        lastTransitionReason = REASON_USER_ACTION;
      }
      else if (selectedMode == MODE_HEAT) {
        currentState = STATE_HEATING;
        heatStartTime = millis(); // Start the timer
        lastTransitionReason = REASON_USER_ACTION;
      }
      else if (selectedMode == MODE_WARM) {
        currentState = STATE_WARMING;
        lastTransitionReason = REASON_USER_ACTION;
      }
    }
    
    // --- State Transition Logic ---
    if (currentState == STATE_DRYING) {
      // Check for completion by humidity target
      if (currentHumidity <= effectiveSetpointHumidity && currentHumidity > 0) {
        update_message_box("Dry point reached. Switching to Warm.");
        lastTransitionReason = REASON_TARGET_MET;
        currentState = STATE_WARMING;
      } 
      // Check for stall condition
      else if (millis() - lastStallCheckTime > stallCheckInterval) {
        if ((humidityAtLastStallCheck - currentHumidity) < stallHumidityDelta) {
          update_message_box("Stall detected. Switching to Warm.");
          lastTransitionReason = REASON_STALLED;
          currentState = STATE_WARMING;
          effectiveSetpointHumidity = currentHumidity; // The stall point becomes the new effective setpoint
        } else {
          lastStallCheckTime = millis();
          humidityAtLastStallCheck = currentHumidity;
        }
      }
    } else if (currentState == STATE_WARMING) {
      // Check if humidity has crept up, but only if the previous state was DRYING
      if (previousState == STATE_DRYING && currentHumidity > (effectiveSetpointHumidity + humidityHysteresis)) {
        update_message_box("Humidity rose. Re-engaging Dry mode.");
        lastTransitionReason = REASON_HYSTERESIS;
        currentState = STATE_DRYING;
      }
    } else if (currentState == STATE_HEATING) {
      // Check for timer completion
      if (millis() - heatStartTime > heatDuration) {
        if (heatCompletionAction == ACTION_STOP) {
          update_message_box("Heat timer finished. Stopping.");
          lastTransitionReason = REASON_TIMER_EXPIRED;
          isHeaterEnabled = false; // This will force state to IDLE
          currentState = STATE_IDLE;
        } else { // ACTION_WARM
          update_message_box("Heat timer finished. Switching to Warm.");
          lastTransitionReason = REASON_TIMER_EXPIRED;
          currentState = STATE_WARMING;
        }
      }
    }
  }

  // --- Construct Status String ---
  if (currentState == STATE_IDLE) {
    if (previousState == STATE_HEATING && lastTransitionReason == REASON_TIMER_EXPIRED) {
      currentStatusString = "IDLE (Heat Stopped)";
    } else {
      currentStatusString = "IDLE";
    }
  } else if (selectedMode == MODE_DRY) {
    if (currentState == STATE_DRYING) {
      if (lastTransitionReason == REASON_HYSTERESIS) {
        currentStatusString = "Dry / RE-DRYING (Maintaining)";
      } else {
        currentStatusString = "Dry / DRYING";
      }
    } else if (currentState == STATE_WARMING) {
      if (lastTransitionReason == REASON_TARGET_MET) {
        currentStatusString = "Dry / WARMING (Setpoint Reached)";
      } else if (lastTransitionReason == REASON_STALLED) {
        currentStatusString = "Dry / WARMING (Stalled)";
      } else {
        currentStatusString = "Dry / WARMING"; // Fallback
      }
    }
  } else if (selectedMode == MODE_HEAT) {
    if (currentState == STATE_HEATING) currentStatusString = "Heat / HEATING";
    else if (currentState == STATE_WARMING) currentStatusString = "Heat / WARMING (Time Expired)";
  } else if (selectedMode == MODE_WARM) {
    currentStatusString = "Warm / WARMING";
  }

  // Update the display if the state changed
  if (previousState != currentState) {
    update_process_status_display();
    sendLog("STATUS_" + currentStatusString);
  }

  // --- Thermostat Logic based on State ---
  bool newHeaterState = isHeaterOn;
  const float hysteresis = 1.0; // Hysteresis of 1 degree to prevent rapid cycling
  float targetTemp = 0.0;
  bool heatingRequired = false;

  switch(currentState) {
    case STATE_DRYING:
    case STATE_HEATING:
      targetTemp = dryingTemperature;
      heatingRequired = true;
      break;
    case STATE_WARMING:
      targetTemp = warmTemperature;
      heatingRequired = true;
      break;
    case STATE_IDLE:
      heatingRequired = false;
      break;
  }

  if (!heatingRequired) {
    // If heating is not required (e.g., IDLE or humidity target met), force heater off.
    newHeaterState = false;
  } else {
    // If heating IS required, apply standard thermostat logic against the target temp.
    // This provides the safety cutoff if the temperature is already too high.
    if (currentTemperature < targetTemp) {
      newHeaterState = true;
    } else if (currentTemperature > (targetTemp + hysteresis)) {
      newHeaterState = false;
    }
  }

  // --- Update Hardware and UI only if state changes ---
  if (newHeaterState != isHeaterOn) {
    isHeaterOn = newHeaterState;
    digitalWrite(HEATER_PIN, isHeaterOn ? HIGH : LOW);
    update_heater_status_display();

    sendLog(isHeaterOn ? "HEAT_ON" : "HEAT_OFF");

    char msg[30];
    sprintf(msg, "Heater turned %s", isHeaterOn ? "ON" : "OFF");
    update_message_box(msg);
  }

  // --- Timed Logging ---
  if (isLoggingEnabled && (millis() - lastTimedLogTime >= logIntervalMillis)) {
    sendLog("TIMED");
    lastTimedLogTime = millis();
  }
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  //Handle WebSocket events
}