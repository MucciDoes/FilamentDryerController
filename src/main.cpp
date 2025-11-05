#include <Arduino.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include "Adafruit_SHT31.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "SPIFFS.h"

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
// IMPORTANT: Replace with your network credentials
const char* ssid = "meshMucci2427";
const char* password = "9275cabfed";
AsyncWebServer server(80);


/* Settings & State */
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
void setupWiFi();
void setupWebServer();
void setupHardwarePins();
void ui_init();
void update_humidity_setpoint_display();
void update_setpoint_display();
void update_process_status_display();
void update_heater_status_display();
void update_message_box(const char* message);
void heater_enable_switch_event_handler(lv_event_t * e);
void setupSensor();
void update_sensor_task(lv_timer_t * timer);
void controlHeaterTask(lv_timer_t * timer);

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
    update_message_box("SPIFFS Mount Failed!");
    return;
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
  if (!sht31.begin(0x44)) { while (1) delay(10); }
}

void setupWiFi() {
  update_message_box("Connecting to WiFi...");

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  char msgBuffer[50];
  sprintf(msgBuffer, "IP: %s", WiFi.localIP().toString().c_str());
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
    json += "\"temperature\":";
    json += String(currentTemperature, 1); // Format to 1 decimal place
    json += ",\"humidity\":";
    json += String(currentHumidity, 1); // Format to 1 decimal place
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
    json += ",\"selected_mode\":";
    json += String(selectedMode);
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
    json += ",\"heat_action\":\"" + String(heatCompletionAction == ACTION_STOP ? "Stop" : "Warm") + "\"";
    json += "}";
    request->send(200, "application/json", json);
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
      Serial.println("Transitioning from IDLE");
      char msgBuffer[50];
      sprintf(msgBuffer, "Mode: %d", selectedMode);
      update_message_box(msgBuffer);
      
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

    char msg[30];
    sprintf(msg, "Heater turned %s", isHeaterOn ? "ON" : "OFF");
    update_message_box(msg);
  }
}