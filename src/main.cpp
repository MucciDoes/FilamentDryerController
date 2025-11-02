#include <Arduino.h>
#include <Wire.h>
#include "Adafruit_SHT31.h"
#include <TFT_eSPI.h>

// Define the custom I2C pins for the CYD board
#define I2C_SDA 27
#define I2C_SCL 22

// Create an SHT31 sensor object
Adafruit_SHT31 sht31 = Adafruit_SHT31();

// Create a TFT_eSPI object
TFT_eSPI tft = TFT_eSPI();

void setup() {
  // Start serial communication at 115200 bits per second (baud).
  Serial.begin(115200);

  // Wait a moment for the serial monitor to connect.
  while (!Serial) {
    delay(10); // wait for serial port to connect. Needed for native USB
  }

  Serial.println("SHT31 Sensor Test");

  // Initialize the I2C bus with our custom pins before initializing the sensor
  Wire.begin(I2C_SDA, I2C_SCL);
  if (!sht31.begin(0x44)) { // 0x44 is the default I2C address
    Serial.println("Couldn't find SHT31 sensor! Check your wiring.");
    while (1) delay(1); // Halt forever if sensor not found
  }

  // Turn on the backlight
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  // Initialize the TFT screen
  tft.init();
  tft.setRotation(1); // 1 for landscape, 0 for portrait
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.println("Sensor Readings:");
}

void loop() {
  float temp = sht31.readTemperature();
  float hum = sht31.readHumidity();

  if (! isnan(temp)) {  // check if 'is not a number'
    Serial.print("Temp *C = "); Serial.print(temp, 1); Serial.print("\t\t");
    tft.setCursor(0, 40); // Set cursor position (x, y)
    tft.setTextSize(3);
    tft.printf("Temp: %.1f C", temp);
  } else { 
    Serial.println("Failed to read temperature");
    tft.println("Failed to read temp");
  }
  
  if (! isnan(hum)) {  // check if 'is not a number'
    Serial.print("Humidity % = "); Serial.println(hum, 1);
    tft.setCursor(0, 80);
    tft.setTextSize(3);
    tft.printf("Humi: %.1f %%", hum); // Use %% to print a single % sign
  } else { 
    Serial.println("Failed to read humidity");
    tft.println("Failed to read humi");
  }

  delay(2000); // Wait 2 seconds between readings
}