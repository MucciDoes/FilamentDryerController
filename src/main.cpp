#include <Arduino.h>
#include <TFT_eSPI.h>
#include "User_Setup.h" // Make display settings available to main.cpp

// Create a TFT_eSPI object
TFT_eSPI tft = TFT_eSPI();

void setup() {
  // 1. Initialize the TFT screen controller
  tft.init();
  
  // 2. Turn on the backlight
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
}

void loop() {
  // Cycle through a rainbow of colors
  tft.fillScreen(TFT_RED);
  delay(500);
  tft.fillScreen(TFT_GREEN);
  delay(500);
  tft.fillScreen(TFT_BLUE);
  delay(500);
  tft.fillScreen(TFT_BLACK);
  delay(500);
}