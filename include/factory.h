#pragma once

#include <Arduino.h>
#include <FastLED.h>
#include <Preferences.h>
#include <buttons.h>
#include <ledManager.h>

extern Preferences preferences;

extern ButtonManager buttons;

extern LedManager mapLEDs;

bool passed;

/**
 * @brief Power button callback for factory test mode
 * 
 * Marks the test as passed and saves the result to NVS.
 */
void onPowerFactory() {
	passed = true;
	preferences.putBool("passed", passed);	// Toggle factory test mode pass/fail state
	Serial.println("Factory test mode saved as passed");
	preferences.end();
}

/**
 * @brief Set all LEDs to a specific color with a brightness ramp for factory testing
 * 
 * @param color RGB color to display
 */
void factorySetColor(CRGB color) {
	for (int b = 0; b <= 255; b += 5) {
		mapLEDs.setAllLedsColor(color);
		FastLED.setBrightness(b);
		FastLED.delay(10);
	}
}

/**
 * @brief Wait for the power button to be pressed or timeout
 * 
 * @param timeout Maximum wait time in milliseconds
 */
void waitForPowerButton(int timeout) {
	unsigned long startTime = millis();
	while (!passed && (millis() - startTime < timeout)) {
		FastLED.delay(10);
	}
}

/**
 * @brief Run the factory test sequence
 * 
 * Cycles through red, green, and blue test colors until the power button is
 * pressed to confirm all LEDs are working.
 * 
 * @return true if the test was run and passed
 * @return false if the test was already passed previously
 */
bool factoryTestMode() {
	preferences.begin("factory_test");
	passed = preferences.getBool("passed", false);
	if (passed == false) {
		buttons.setCallback(POWER_BUTTON, onPowerFactory);
		Serial.println("Factory test mode enabled");
		uint8_t colorIndex = 0;
		const CRGB testColors[] = {
			CRGB(128, 0, 0), CRGB(0, 128, 0), CRGB(0, 0, 128), CRGB(32, 32, 32),
			CRGB(32, 0, 0),	 CRGB(0, 32, 0),  CRGB(0, 0, 32),  CRGB(0, 0, 0),
		};

		while (!passed) {
			// Ramp up to full brightness for the current test color
			factorySetColor(testColors[colorIndex]);
#if defined(LED_8_PIXELS)  // If there are >=8 LED channels, show each color for 2 seconds
			waitForPowerButton(2000);
#else
			waitForPowerButton(1000);
#endif
			colorIndex = (colorIndex + 1) % (sizeof(testColors) / sizeof(testColors[0]));
		}

		factorySetColor(CRGB::Black);

		return true;

	} else {
		Serial.println("Factory test passed, skipping.");
		preferences.end();
		return false;
	}
}