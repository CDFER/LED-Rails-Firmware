#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <atomic>
#include <buttons.h>
#include <mapLEDs.h>

extern Preferences preferences;

extern ButtonManager buttons;

extern LedManager mapLEDs;

inline std::atomic<bool> factoryTestPassed{ false };

/**
 * @brief Power button callback for factory test mode
 * 
 * Marks the test as passed and saves the result to NVS.
 */
inline void onPowerFactory() {
	factoryTestPassed.store(true);
	Preferences localPrefs;
	localPrefs.begin("factory_test", false);
	localPrefs.putBool("passed", true);	 // Toggle factory test mode pass/fail state
	Serial.println("Factory test mode saved as passed");
	localPrefs.end();
}

/**
 * @brief Set all LEDs to a specific color with a brightness ramp for factory testing
 * 
 * @param color RGB color to display
 */
inline void factorySetColor(CRGB color) {
	for (int b = 0; b <= 255; b += 10) {
		if (factoryTestPassed.load())
			break;	// Exit early if button is pressed during fade
		CRGB fadedColor = color;
		fadedColor.nscale8_video(b);

		mapLEDs.beginFrame();
		mapLEDs.setAllLedsColor(fadedColor);
		mapLEDs.show();
		Serial.printf("Factory test color: R=%d, G=%d, B=%d, Brightness=%d\n", fadedColor.r, fadedColor.g, fadedColor.b, b);
		vTaskDelay(pdMS_TO_TICKS(30));	// Short delay to allow the LEDs to update
	}
}

/**
 * @brief Wait for the power button to be pressed or timeout
 * 
 * @param timeout Maximum wait time in milliseconds
 */
inline void waitForPowerButton(int timeout) {
	unsigned long startTime = millis();
	while (!factoryTestPassed.load() && (millis() - startTime < timeout)) {
		vTaskDelay(pdMS_TO_TICKS(10));
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
inline bool factoryTestMode() {
	{
		Preferences localPrefs;
		localPrefs.begin("factory_test", true);
		factoryTestPassed.store(localPrefs.getBool("passed", false));
		localPrefs.end();
	}

	if (!factoryTestPassed.load()) {
		buttons.setCallback(POWER_BUTTON, onPowerFactory);
		Serial.println("Factory test mode enabled");
		uint8_t colorIndex = 0;
		const CRGB testColors[] = {
			CRGB(255, 0, 0), CRGB(0, 255, 0), CRGB(0, 0, 255), CRGB(255, 255, 255), CRGB(0, 0, 0),
		};

		while (!factoryTestPassed.load()) {
			// Ramp up to full brightness for the current test color
			factorySetColor(testColors[colorIndex]);
#if defined(LED_8_PIXELS)  // If there are >=8 LED channels, show each color for 2 seconds
			waitForPowerButton(2000);
#else
			waitForPowerButton(1000);
#endif
			colorIndex = (colorIndex + 1) % (sizeof(testColors) / sizeof(testColors[0]));
		}

		mapLEDs.beginFrame();
		mapLEDs.setAllLedsColor(CRGB::Black);
		mapLEDs.show();

		return true;

	} else {
		Serial.println("Factory test passed, skipping.");
		return false;
	}
}