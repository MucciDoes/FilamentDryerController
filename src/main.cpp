#include <Arduino.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include "Adafruit_SHT31.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>

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
float setpointTemperature = 50.0;
float setpointHumidity = 30.0;
float maintenanceTemperature = 35.0;
float humidityHysteresis = 5.0; // %RH to allow humidity to rise before re-engaging drying
float effectiveSetpointHumidity = 30.0; // The dynamically adjusted humidity target
uint32_t stallCheckInterval = 1800000; // 30 minutes in milliseconds
float stallHumidityDelta = 0.5; // Minimum %RH drop required over the interval to not be considered stalled

uint32_t lastStallCheckTime = 0;
float humidityAtLastStallCheck = 0;

enum ProcessState {
  IDLE,
  DRYING,
  MAINTAINING
};
enum ControlMode {
  HUMIDITY_MODE,
  TEMP_ONLY_MODE
};
ControlMode currentControlMode = HUMIDITY_MODE; // Default to smart humidity control
ProcessState currentProcessState = IDLE;
bool isHeaterEnabled = false; // Master switch for the heating process, OFF by default for safety

/* UI Object Globals */
lv_obj_t * temp_label_value;
lv_obj_t * hum_label_value;
lv_obj_t * message_label;
lv_obj_t * setpoint_label_value;
lv_obj_t * heater_status_label;
lv_obj_t * process_status_label;
lv_obj_t * control_mode_label;
lv_obj_t * hum_setpoint_label_value;
static lv_style_t style_error;

/* Forward Declarations */
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p);
void setupWiFi();
void setupWebServer();
void setupHardwarePins();
void ui_init();
void update_control_mode_display();
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
    String html = R"rawliteral(<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'><title>Filament Dryer</title><style>body{font-family:Arial,sans-serif;text-align:center;margin:20px;}h1{color:#333;}.grid-container{display:grid;grid-template-columns:1fr 1fr 1fr;gap:10px;max-width:600px;margin:auto;}.grid-item{background-color:#f2f2f2;padding:15px;border-radius:8px;}.data{font-size:1.5em;font-weight:bold;}.label{font-size:0.9em;color:#555;cursor:pointer;}.edit-btn{cursor:pointer;font-size:1em;margin-left:10px;}.status-on{color:red;}.status-off{color:grey;}</style><script>let currentData={};function fetchData(){var x=new XMLHttpRequest();x.onreadystatechange=function(){if(this.readyState==4&&this.status==200){currentData=JSON.parse(this.responseText);document.getElementById('temp_val').innerHTML=currentData.temperature+' &deg;C';document.getElementById('hum_val').innerHTML=currentData.humidity+' %';document.getElementById('temp_set_val').innerHTML=currentData.setpoint+' &deg;C';document.getElementById('hum_set_val').innerHTML=currentData.setpoint_hum+' %';document.getElementById('maint_temp_val').innerHTML=currentData.maint_temp+' &deg;C';document.getElementById('hum_hyst_val').innerHTML=currentData.hum_hyst+' %';document.getElementById('stall_interval_val').innerHTML=currentData.stall_interval/60000+' min';document.getElementById('stall_delta_val').innerHTML=currentData.stall_delta+' %';var h=document.getElementById('heater_status');h.innerHTML=currentData.heater_on?'ON':'OFF';h.className=currentData.heater_on?'data status-on':'data status-off';var e=document.getElementById('enable_status');e.innerHTML=currentData.is_enabled?'ENABLED':'DISABLED';e.className=currentData.is_enabled?'data status-on':'data status-off';var p=document.getElementById('process_status');p.innerHTML=currentData.process_state;var cm=document.getElementById('control_mode');cm.innerHTML=currentData.control_mode;}};x.open('GET','/readings',true);x.send();}
function postData(endpoint,params){var x=new XMLHttpRequest();x.open('POST',endpoint,true);x.setRequestHeader('Content-type','application/x-www-form-urlencoded');x.onreadystatechange=function(){if(this.readyState==4&&this.status==200){fetchData();}};x.send(params);}
function editTemp(){var v=prompt('Enter new Drying Temp setpoint:',currentData.setpoint);if(v!=null&&v!=='')postData('/setpoint','value='+v);}
function editHum(){var v=prompt('Enter new Humidity setpoint:',currentData.setpoint_hum);if(v!=null&&v!=='')postData('/setpointhum','value='+v);}
function editMaintTemp(){var v=prompt('Enter new Maintenance Temp setpoint:',currentData.maint_temp);if(v!=null&&v!=='')postData('/setpointmaint','value='+v);}
function editHumHyst(){var v=prompt('Enter new Humidity Hysteresis (%):',currentData.hum_hyst);if(v!=null&&v!=='')postData('/sethumhyst','value='+v);}
function editStallInterval(){var v=prompt('Enter new Stall Interval (minutes):',currentData.stall_interval/60000);if(v!=null&&v!=='')postData('/setstallinterval','value='+v*60000);}
function editStallDelta(){var v=prompt('Enter new Stall Delta (%):',currentData.stall_delta);if(v!=null&&v!=='')postData('/setstalldelta','value='+v);}
function toggleEnable(){postData('/toggle_enable','');}
function setMode(mode){postData('/setmode','mode='+mode);}
setInterval(fetchData,2000);window.onload=fetchData;</script></head><body><h1>Filament Dryer Control</h1><div class='grid-container'><div class='grid-item'><div class='label' onclick="alert('Current measured temperature inside the dryer.')">Temperature</div><div id='temp_val' class='data'>--.- &deg;C</div></div><div class='grid-item'><div class='label' onclick="alert('Target temperature during the main DRYING phase.')">Drying Temp</div><div id='temp_set_val' class='data'>--.- &deg;C</div><span class='edit-btn' onclick='editTemp()'>&#9998;</span></div><div class='grid-item'><div class='label' onclick="alert('Lower target temperature for the MAINTAINING phase after drying is complete.')">Maint. Temp</div><div id='maint_temp_val' class='data'>--.- &deg;C</div><span class='edit-btn' onclick='editMaintTemp()'>&#9998;</span></div><div class='grid-item'><div class='label' onclick="alert('Current measured relative humidity inside the dryer.')">Humidity</div><div id='hum_val' class='data'>--.- %</div></div><div class='grid-item'><div class='label' onclick="alert('Target humidity. The DRYING phase ends when humidity drops below this value.')">Hum Setpoint</div><div id='hum_set_val' class='data'>--.- %</div><span class='edit-btn' onclick='editHum()'>&#9998;</span></div><div class='grid-item'><div class='label' onclick="alert('Allowed humidity increase before re-engaging the DRYING phase from MAINTAINING.')">Hum Hysteresis</div><div id='hum_hyst_val' class='data'>--.- %</div><span class='edit-btn' onclick='editHumHyst()'>&#9998;</span></div><div class='grid-item'><div class='label' onclick="alert('Time period to check for drying progress. If humidity does not drop enough in this interval, the process is stalled.')">Stall Interval</div><div id='stall_interval_val' class='data'>-- min</div><span class='edit-btn' onclick='editStallInterval()'>&#9998;</span></div><div class='grid-item'><div class='label' onclick="alert('The minimum humidity drop required during the Stall Interval to continue the DRYING phase.')">Stall Delta</div><div id='stall_delta_val' class='data'>--.- %</div><span class='edit-btn' onclick='editStallDelta()'>&#9998;</span></div><div class='grid-item'><div class='label' onclick="alert('Selects the control logic. HUMIDITY: Smart mode with drying and maintaining phases. TEMP ONLY: Simple mode to hold the Drying Temp.')">Control Mode</div><div id='control_mode' class='data'>HUMIDITY</div><select onchange='setMode(this.value)'><option value='0'>Humidity</option><option value='1'>Temp Only</option></select></div></div><div class='grid-container' style='margin-top:20px;'><div class='grid-item'><div class='label' onclick="alert('Actual current state of the heater relay.')">Heater Status</div><div id='heater_status' class='data status-off'>OFF</div></div><div class='grid-item' style='grid-column: span 2;'><div class='label' onclick="alert('Master switch to enable or disable the entire drying process.')">Process Control</div><div id='enable_status' class='data status-off'>DISABLED</div><button onclick='toggleEnable()'>Toggle</button></div></div><div class='grid-item' style='grid-column: span 3;'><div class='label' onclick="alert('Current phase of the drying process: IDLE, DRYING, or MAINTAINING.')">Process State</div><div id='process_status' class='data'>IDLE</div></div></body></html>)rawliteral";
    request->send(200, "text/html", html);
  });

  // Route for sensor readings (JSON endpoint)
  server.on("/readings", HTTP_GET, [](AsyncWebServerRequest *request){
    String json = "{";
    json += "\"temperature\":";
    json += String(currentTemperature, 1); // Format to 1 decimal place
    json += ",\"humidity\":";
    json += String(currentHumidity, 1); // Format to 1 decimal place
    json += ",\"setpoint\":";
    json += String(setpointTemperature, 1);
    json += ",\"setpoint_hum\":";
    json += String(setpointHumidity, 1);
    json += ",\"maint_temp\":";
    json += String(maintenanceTemperature, 1);
    json += ",\"process_state\":\"" + String(currentProcessState == IDLE ? "IDLE" : (currentProcessState == DRYING ? "DRYING" : "MAINTAINING")) + "\"";
    json += ",\"heater_on\":";
    json += isHeaterOn ? "true" : "false";
    json += ",\"is_enabled\":";
    json += isHeaterEnabled ? "true" : "false";
    json += ",\"control_mode\":\"" + String(currentControlMode == HUMIDITY_MODE ? "HUMIDITY" : "TEMP ONLY") + "\"";
    json += ",\"hum_hyst\":";
    json += String(humidityHysteresis, 1);
    json += ",\"stall_interval\":";
    json += String(stallCheckInterval);
    json += ",\"stall_delta\":";
    json += String(stallHumidityDelta, 1);
    json += "}";
    request->send(200, "application/json", json);
  });

  // Route to set the temperature setpoint
  server.on("/setpoint", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("value", true)) { // "true" means it's a POST parameter
      String value = request->getParam("value", true)->value();
      setpointTemperature = value.toFloat();
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
  server.on("/setpointmaint", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("value", true)) {
      String value = request->getParam("value", true)->value();
      maintenanceTemperature = value.toFloat();
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
      if (mode == 0) currentControlMode = HUMIDITY_MODE;
      else if (mode == 1) currentControlMode = TEMP_ONLY_MODE;
      update_control_mode_display();
      request->send(200, "text/plain", "OK");
    } else {
      request->send(400, "text/plain", "Bad Request");
    }
  });

  // Route to toggle the master enable state
  server.on("/toggle_enable", HTTP_POST, [](AsyncWebServerRequest *request){
    isHeaterEnabled = !isHeaterEnabled;
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

  // --- Process Status Display ---
  process_status_label = lv_label_create(lv_scr_act());
  lv_obj_add_style(process_status_label, &style_setpoint, 0); // Use cyan style
  lv_obj_set_width(process_status_label, 120);
  lv_obj_set_style_text_align(process_status_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(process_status_label, LV_ALIGN_BOTTOM_LEFT, 10, -35);
  update_process_status_display();

  control_mode_label = lv_label_create(lv_scr_act());
  lv_obj_add_style(control_mode_label, &style_setpoint_hum, 0); // Use magenta style
  lv_obj_set_width(control_mode_label, 120);
  lv_obj_set_style_text_align(control_mode_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(control_mode_label, LV_ALIGN_BOTTOM_RIGHT, -10, -35);
  update_control_mode_display();

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
  dtostrf(setpointTemperature, 4, 1, setpointStr);
  lv_label_set_text_fmt(setpoint_label_value, "%s C", setpointStr);
}

void update_humidity_setpoint_display() {
  char setpointStr[8];
  dtostrf(setpointHumidity, 4, 1, setpointStr);
  lv_label_set_text_fmt(hum_setpoint_label_value, "%s %%", setpointStr);
}

void update_control_mode_display() {
  if (currentControlMode == HUMIDITY_MODE) {
    lv_label_set_text(control_mode_label, "Humidity");
  } else {
    lv_label_set_text(control_mode_label, "Temp Only");
  }
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
  switch (currentProcessState) {
    case IDLE:
      lv_label_set_text(process_status_label, "IDLE");
      break;
    case DRYING:
      lv_label_set_text(process_status_label, "DRYING");
      break;
    case MAINTAINING:
      lv_label_set_text(process_status_label, "MAINTAINING");
      break;
  }
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
  ProcessState previousProcessState = currentProcessState;

  if (!isHeaterEnabled) {
    currentProcessState = IDLE;
  } else {
    // If enabled, decide whether to start or continue a process
    if (currentProcessState == IDLE) {
      // When starting, reset stall detection timer
      lastStallCheckTime = millis();
      humidityAtLastStallCheck = currentHumidity;
      effectiveSetpointHumidity = setpointHumidity; // Reset to the user-defined target
      currentProcessState = DRYING;
    }
    
    if (currentControlMode == HUMIDITY_MODE) {
      if (currentProcessState == DRYING) {
        // Check for normal completion
        if (currentHumidity <= effectiveSetpointHumidity && currentHumidity > 0) {
          update_message_box("Humidity setpoint reached.");
          currentProcessState = MAINTAINING;
        // Check for stall condition
        } else if (millis() - lastStallCheckTime > stallCheckInterval) {
          if ((humidityAtLastStallCheck - currentHumidity) < stallHumidityDelta) {
            update_message_box("Stall detected, switching to maintain.");
            currentProcessState = MAINTAINING;
            effectiveSetpointHumidity = currentHumidity; // The stall point becomes the new effective setpoint
          } else {
            // Not stalled, reset timer and snapshot
            lastStallCheckTime = millis();
            humidityAtLastStallCheck = currentHumidity;
          }
        }
      } else if (currentProcessState == MAINTAINING && currentHumidity > (effectiveSetpointHumidity + humidityHysteresis)) {
        update_message_box("Humidity rose, re-engaging drying.");
        currentProcessState = DRYING;
      }
    }
  }

  // Update the display if the state changed
  if (previousProcessState != currentProcessState) {
    // If we just started, give a clear message
    if (previousProcessState == IDLE && currentProcessState == DRYING) {
      update_message_box("Drying process started.");
    }
    update_process_status_display();
  }

  // --- Thermostat Logic based on State ---
  bool newHeaterState = isHeaterOn;
  const float hysteresis = 1.0; // Hysteresis of 1 degree to prevent rapid cycling
  float targetTemp = 0.0;
  bool heatingRequired = false;

  if (currentProcessState == DRYING) { // This state is used by both modes
    targetTemp = setpointTemperature;
    if (currentControlMode == HUMIDITY_MODE) {
      heatingRequired = (currentHumidity > effectiveSetpointHumidity);
    } else { // TEMP_ONLY_MODE
      heatingRequired = true;
    }
  } else if (currentProcessState == MAINTAINING) { // This state is only used by HUMIDITY_MODE
    targetTemp = maintenanceTemperature;
    heatingRequired = true; // In maintenance, we always want to hold the temp.
  } else { // State is IDLE
    heatingRequired = false;
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