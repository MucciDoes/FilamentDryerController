#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include "Adafruit_SHT31.h"

Adafruit_SHT31 sht31 = Adafruit_SHT31();

// Create a TFT_eSPI object
TFT_eSPI tft = TFT_eSPI();

void setup() {
  // Initialize Serial for debugging
  Serial.begin(115200);
  delay(1000); // Wait a moment for serial monitor to connect
  Serial.println("--- Setup Started ---");

  // 1. Initialize the TFT screen controller
  tft.init();
  tft.setRotation(1); // Set landscape mode

  // Backlight workaround
  const int backlightPin = 21; // Pin from platformio.ini
  const int pwmChannel = 0;    // Use channel 0 for PWM
  const int pwmFrequency = 5000; // 5kHz frequency
  const int pwmResolution = 8;   // 8-bit resolution (0-255)
  ledcSetup(pwmChannel, pwmFrequency, pwmResolution); // Configure the PWM channel
  ledcAttachPin(backlightPin, pwmChannel);            // Attach the backlight pin to the channel
  ledcWrite(pwmChannel, 255);                         // Set duty cycle to max (full brightness)

  // Initialize the I2C bus using the pins available on your board's ports.
  Wire.begin(27, 22); // SDA=27, SCL=22

  // Initialize the SHT31 Sensor
  if (!sht31.begin(0x44)) { // Address from I2C scanner
    Serial.println("Could not find SHT31? Check wiring");
    tft.fillScreen(TFT_RED);
    tft.setTextColor(TFT_WHITE);
    tft.drawCentreString("SHT31 Error!", 160, 110, 4);
    while (1) delay(10);
  }

  Serial.println("--- Setup Complete. Starting loop. ---");

  // --- Draw Static UI Elements ---
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawCentreString("Filament Dryer", 160, 10, 4); // Draw title in Font 4
  tft.drawFastHLine(0, 40, 320, TFT_DARKGREY);
  
  // Labels for our data
  tft.drawString("Temp:", 20, 80, 4);
  tft.drawString("Humidity:", 20, 140, 4);
}

void loop() {
  float t = sht31.readTemperature();
  float h = sht31.readHumidity();

  // --- Add serial prints for debugging ---
  Serial.print("Temperature: ");
  Serial.print(t);
  Serial.print(" *C\tHumidity: ");
  Serial.print(h);
  Serial.println(" %");

  // Set text parameters for sensor data
  tft.setTextDatum(MC_DATUM); // Middle-Center datum for text alignment

  if (isnan(h) || isnan(t)) {
    Serial.println("Failed to read from SHT31 sensor!");
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("Sensor Error!", 230, 120, 4); // Font 4
    return;
  }

  // --- Display the updated values ---
  tft.setTextColor(TFT_WHITE, TFT_BLACK); // Set text color to white with a black background

  char tempStr[8];
  dtostrf(t, 4, 1, tempStr); // Format float to string: 4 width, 1 decimal
  char humStr[8];
  dtostrf(h, 4, 1, humStr); // Format float to string: 4 width, 1 decimal

  // Draw the strings using memory-efficient char buffers.
  // The black background color will erase the old values.
  tft.drawString(tempStr, 230, 90, 6);  // Use large Font 6
  tft.drawString(humStr, 230, 150, 6); // Use large Font 6

  // Wait a bit before the next loop.
  delay(2000); // Wait 2s between sensor reads
}