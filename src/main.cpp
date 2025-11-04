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

/* Network Globals */
// IMPORTANT: Replace with your network credentials
const char* ssid = "meshMucci2427";
const char* password = "9275cabfed";
AsyncWebServer server(80);


/* Settings & State */
float setpointTemperature = 50.0;
float setpointHumidity = 30.0;
bool isHeaterEnabled = false; // Master switch for the heating process, OFF by default for safety

/* UI Object Globals */
lv_obj_t * temp_label_value;
lv_obj_t * hum_label_value;
lv_obj_t * message_label;
lv_obj_t * setpoint_label_value;
lv_obj_t * hum_setpoint_label_value;
static lv_style_t style_error;

/* Forward Declarations */
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p);
void setupWiFi();
void setupWebServer();
void ui_init();
void update_humidity_setpoint_display();
void update_setpoint_display();
void update_message_box(const char* message);
void setupSensor();
void update_sensor_task(lv_timer_t * timer);

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

  // --- Sensor Initialization ---
  setupSensor();

  // --- Network Initialization ---
  setupWiFi();
  setupWebServer();

  // --- Create a task to update sensor data ---
  lv_timer_create(update_sensor_task, 2000, NULL);

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
  if (!sht31.begin(0x44)) {
    Serial.println("Could not find SHT31? Check wiring");
    while (1) delay(10);
  }
}

void setupWiFi() {
  Serial.print("Connecting to ");
  Serial.println(ssid);
  update_message_box("Connecting to WiFi...");

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected.");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  char msgBuffer[50];
  sprintf(msgBuffer, "IP: %s", WiFi.localIP().toString().c_str());
  update_message_box(msgBuffer);
}

void setupWebServer() {
  // Route for the main web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    String html = R"rawliteral(<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'><title>Filament Dryer</title><style>body{font-family:Arial,sans-serif;text-align:center;margin:20px;}h1{color:#333;}.grid-container{display:grid;grid-template-columns:1fr 1fr;gap:10px 20px;max-width:500px;margin:auto;}.grid-item{background-color:#f2f2f2;padding:15px;border-radius:8px;}.data{font-size:1.8em;font-weight:bold;}.label{font-size:0.9em;color:#555;}.edit-btn{cursor:pointer;font-size:1em;margin-left:10px;}</style><script>let currentData={};function fetchData(){var x=new XMLHttpRequest();x.onreadystatechange=function(){if(this.readyState==4&&this.status==200){currentData=JSON.parse(this.responseText);document.getElementById('temp_val').innerHTML=currentData.temperature+' &deg;C';document.getElementById('hum_val').innerHTML=currentData.humidity+' %';document.getElementById('temp_set_val').innerHTML=currentData.setpoint+' &deg;C';document.getElementById('hum_set_val').innerHTML=currentData.setpoint_hum+' %';}};x.open('GET','/readings',true);x.send();}
function editTemp(){var v=prompt('Enter new temperature setpoint:',currentData.setpoint);if(v!=null&&v!==''){var p='value='+v;var x=new XMLHttpRequest();x.open('POST','/setpoint',true);x.setRequestHeader('Content-type','application/x-www-form-urlencoded');x.onreadystatechange=function(){if(this.readyState==4&&this.status==200){console.log('Temp setpoint updated');fetchData();}};x.send(p);}}
function editHum(){var v=prompt('Enter new humidity setpoint:',currentData.setpoint_hum);if(v!=null&&v!==''){var p='value='+v;var x=new XMLHttpRequest();x.open('POST','/setpointhum',true);x.setRequestHeader('Content-type','application/x-www-form-urlencoded');x.onreadystatechange=function(){if(this.readyState==4&&this.status==200){console.log('Hum setpoint updated');fetchData();}};x.send(p);}}
setInterval(fetchData,2000);window.onload=fetchData;</script></head><body><h1>Filament Dryer Control</h1><div class='grid-container'><div class='grid-item'><div class='label'>Temperature</div><div id='temp_val' class='data'>--.- &deg;C</div></div><div class='grid-item'><div class='label'>Temp Setpoint</div><div id='temp_set_val' class='data'>--.- &deg;C</div><span class='edit-btn' onclick='editTemp()'>&#9998;</span></div><div class='grid-item'><div class='label'>Humidity</div><div id='hum_val' class='data'>--.- %</div></div><div class='grid-item'><div class='label'>Hum Setpoint</div><div id='hum_set_val' class='data'>--.- %</div><span class='edit-btn' onclick='editHum()'>&#9998;</span></div></div></body></html>)rawliteral";
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
    json += "}";
    request->send(200, "application/json", json);
  });

  // Route to set the temperature setpoint
  server.on("/setpoint", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("value", true)) { // "true" means it's a POST parameter
      String value = request->getParam("value", true)->value();
      setpointTemperature = value.toFloat();
      Serial.printf("Setpoint updated to: %.1f\n", setpointTemperature);
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
      Serial.printf("Humidity setpoint updated to: %.1f\n", setpointHumidity);
      update_humidity_setpoint_display(); // Update the LVGL display immediately
      request->send(200, "text/plain", "OK");
    } else {
      request->send(400, "text/plain", "Bad Request");
    }
  });

  server.begin();
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
  // Column 1: Labels, Column 2: Values, Column 3: Setpoints
  const int col1_x = 10;
  const int col2_x = 120;
  const int col3_x = 230;

  lv_obj_t * title_label = lv_label_create(lv_scr_act());
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

  setpoint_label_value = lv_label_create(lv_scr_act());
  lv_obj_add_style(setpoint_label_value, &style_setpoint, 0);
  lv_obj_align(setpoint_label_value, LV_ALIGN_TOP_LEFT, col3_x, 55);

  // --- Humidity Row ---
  lv_obj_t * hum_label_static = lv_label_create(lv_scr_act());
  lv_label_set_text(hum_label_static, "Humidity:");
  lv_obj_add_style(hum_label_static, &style_label, 0);
  lv_obj_align(hum_label_static, LV_ALIGN_TOP_LEFT, col1_x, 120);

  hum_label_value = lv_label_create(lv_scr_act());
  lv_obj_add_style(hum_label_value, &style_value, 0);
  lv_obj_align(hum_label_value, LV_ALIGN_TOP_LEFT, col2_x, 115);

  hum_setpoint_label_value = lv_label_create(lv_scr_act());
  lv_obj_add_style(hum_setpoint_label_value, &style_setpoint_hum, 0);
  lv_obj_align(hum_setpoint_label_value, LV_ALIGN_TOP_LEFT, col3_x, 115);

  // --- Message Box ---
  message_label = lv_label_create(lv_scr_act());
  lv_label_set_text(message_label, "Initializing...");
  lv_obj_add_style(message_label, &style_message, 0);
  lv_obj_set_width(message_label, screenWidth - 20);
  lv_label_set_long_mode(message_label, LV_LABEL_LONG_WRAP);
  lv_obj_align(message_label, LV_ALIGN_BOTTOM_LEFT, 10, -5);

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