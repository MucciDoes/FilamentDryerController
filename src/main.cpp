#include <Arduino.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include "Adafruit_SHT31.h"

/* LVGL Globals */
TFT_eSPI tft = TFT_eSPI();
static const uint16_t screenWidth  = 320;
static const uint16_t screenHeight = 240;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[screenWidth * 10];

/* Sensor Globals */
Adafruit_SHT31 sht31 = Adafruit_SHT31();

/* UI Object Globals */
lv_obj_t * temp_label_value;
lv_obj_t * hum_label_value;
lv_obj_t * message_label;
static lv_style_t style_error; // Moved to global scope

/* Forward Declarations */
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p);
void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data);
void ui_init();
void update_message_box(const char* message);
void update_sensor_task(lv_timer_t * timer);

void setup() {
  Serial.begin(115200); // LVGL can use this for logging

  // --- LVGL and Display Initialization ---
  lv_init();
  tft.begin();
  tft.setRotation(1);

  // Backlight workaround
  ledcSetup(0, 5000, 8);
  ledcAttachPin(21, 0);
  ledcWrite(0, 255);

  lv_disp_draw_buf_init(&draw_buf, buf, NULL, screenWidth * 10);

  /*Initialize the display*/
  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = screenWidth;
  disp_drv.ver_res = screenHeight;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  /*Initialize the input device (touchpad)*/
  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read;
  lv_indev_drv_register(&indev_drv);

  // --- Sensor Initialization ---
  Wire.begin(27, 22); // SDA=27, SCL=22
  if (!sht31.begin(0x44)) {
    Serial.println("Could not find SHT31? Check wiring");
    // We can create an error screen in LVGL later
    while (1) delay(10);
  }

  // --- Create the UI ---
  ui_init();

  // --- Create a task to update sensor data ---
  lv_timer_create(update_sensor_task, 2000, NULL); // 2000ms = 2 seconds

  update_message_box("Setup complete.");
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

/*Read the touchpad*/
void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
  uint16_t touchX, touchY;
  bool touched = tft.getTouch(&touchX, &touchY);
  if (!touched) {
    data->state = LV_INDEV_STATE_REL;
  } else {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = touchX;
    data->point.y = touchY;
  }
}

void ui_init() {
  // --- Create Styles ---
  // Style for the title
  static lv_style_t style_title;
  lv_style_init(&style_title);
  lv_style_set_text_font(&style_title, &lv_font_montserrat_24);

  // Style for the static labels
  static lv_style_t style_label;
  lv_style_init(&style_label);
  lv_style_set_text_font(&style_label, &lv_font_montserrat_16);

  // Style for the large sensor values
  static lv_style_t style_value;
  lv_style_init(&style_value);
  lv_style_set_text_font(&style_value, &lv_font_montserrat_28);

  // Style for error text (yellow, same font size as values)
  lv_style_init(&style_error);
  lv_style_set_text_font(&style_error, &lv_font_montserrat_28);
  lv_style_set_text_color(&style_error, lv_color_hex(0xFFFF00)); // Yellow

  // Style for the message box
  static lv_style_t style_message;
  lv_style_init(&style_message);
  lv_style_set_text_color(&style_message, lv_color_hex(0xFFFF00)); // Yellow

  // Create a title label
  lv_obj_t * title_label = lv_label_create(lv_scr_act());
  lv_label_set_text(title_label, "Filament Dryer");
  lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 10);
  lv_obj_add_style(title_label, &style_title, 0);

  // Create a static label for Temperature
  lv_obj_t * temp_label_static = lv_label_create(lv_scr_act());
  lv_label_set_text(temp_label_static, "Temperature:");
  lv_obj_add_style(temp_label_static, &style_label, 0);
  lv_obj_align(temp_label_static, LV_ALIGN_TOP_LEFT, 20, 60);

  // Create a dynamic label for the Temperature value
  temp_label_value = lv_label_create(lv_scr_act());
  lv_label_set_text(temp_label_value, "--.- C");
  lv_obj_add_style(temp_label_value, &style_value, 0);
  // Align the value in a column to the right of the static labels
  lv_obj_align(temp_label_value, LV_ALIGN_TOP_LEFT, 160, 55);

  // Create a static label for Humidity
  lv_obj_t * hum_label_static = lv_label_create(lv_scr_act());
  lv_label_set_text(hum_label_static, "Humidity:");
  lv_obj_add_style(hum_label_static, &style_label, 0);
  lv_obj_align(hum_label_static, LV_ALIGN_TOP_LEFT, 20, 120);

  // Create a dynamic label for the Humidity value
  hum_label_value = lv_label_create(lv_scr_act());
  lv_label_set_text(hum_label_value, "--.- %");
  lv_obj_add_style(hum_label_value, &style_value, 0);
  lv_obj_align(hum_label_value, LV_ALIGN_TOP_LEFT, 160, 115);

  // Create the message label at the bottom
  message_label = lv_label_create(lv_scr_act());
  lv_label_set_text(message_label, "Initializing...");
  lv_obj_add_style(message_label, &style_message, 0);
  lv_obj_set_width(message_label, screenWidth - 20); // Set width to fill screen with padding
  lv_label_set_long_mode(message_label, LV_LABEL_LONG_WRAP); // Allow text to wrap
  lv_obj_align(message_label, LV_ALIGN_BOTTOM_LEFT, 10, -10);
}

void update_message_box(const char* message) {
  lv_label_set_text(message_label, message);
}

void update_sensor_task(lv_timer_t * timer) {
  float t = sht31.readTemperature();
  float h = sht31.readHumidity();

  char msgBuffer[50];

  if (isnan(t) || isnan(h)) {
    // Apply the error style and set text
    lv_obj_add_style(temp_label_value, &style_error, 0);
    lv_obj_add_style(hum_label_value, &style_error, 0);
    lv_label_set_text(temp_label_value, "Error");
    lv_label_set_text(hum_label_value, "Error");
    update_message_box("Sensor read error!");
  } else {
    // On recovery, remove the error style so the default (white) is used
    lv_obj_remove_style(temp_label_value, &style_error, 0);
    lv_obj_remove_style(hum_label_value, &style_error, 0);

    char tempStr[8];
    dtostrf(t, 4, 1, tempStr); // Format to 1 decimal place
    lv_label_set_text_fmt(temp_label_value, "%s C", tempStr);

    char humStr[8];
    dtostrf(h, 4, 1, humStr);
    lv_label_set_text_fmt(hum_label_value, "%s %%", humStr);

    // Update the message box with the latest readings
    sprintf(msgBuffer, "Temp: %.1f C, Humidity: %.1f %%", t, h);
    update_message_box(msgBuffer);
  }
}