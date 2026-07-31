/**
 * @file factory.h
 * @brief Factory LED test flow and persistent pass-state handling.
 */

#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <atomic>
#include "buttons.h"
#include "ledStorageSync.h"
#include "mapLeds.h"
#include "statusLeds.h"

/// Shared NVS preferences handle used by the firmware.
extern Preferences preferences;

/// Global button manager used by the factory test callbacks.
extern ButtonManager buttons;

/// Global LED manager used to submit factory-test frames.
extern LedManager mapLeds;

/** @brief True once the factory test has been completed successfully. */
inline std::atomic<bool> factoryTestPassed{ false };

/**
 * @brief Mark the factory test as passed when the power button is pressed.
 *
 * The result is stored in NVS so subsequent boots can skip the test.
 */
inline void onPowerFactory() {
	factoryTestPassed.store(true);
	xSemaphoreTake(fastLEDPreferencesMutex, portMAX_DELAY);
	Preferences localPrefs;
	localPrefs.begin("factory_test", false);
	localPrefs.putBool("passed", true);  // Toggle factory test mode pass/fail state
	Serial.println("Factory test mode saved as passed");
	localPrefs.end();
	xSemaphoreGive(fastLEDPreferencesMutex);
}

/**
 * @brief Display a color while ramping brightness for factory testing.
 * @param color RGB color to display.
 */
inline void factorySetColor(CRGB color) {
	if (color == CRGB(255, 0, 0)) {
		statusLeds.setState(WIFI_LED_PIN, LED_ON_RED, SERVER_LED_PIN, LED_ON_RED);
	} else if (color == CRGB(0, 255, 0)) {
		statusLeds.setState(WIFI_LED_PIN, LED_ON_GREEN, SERVER_LED_PIN, LED_ON_GREEN);
	} else {
		statusLeds.setState(WIFI_LED_PIN, LED_OFF, SERVER_LED_PIN, LED_OFF);
	}

	for (int brightnessLevel = 0; brightnessLevel <= 255; brightnessLevel += 64) {
		if (factoryTestPassed.load())
			break;  // Exit early if button is pressed during fade
		CRGB fadedColor = color;
		fadedColor.nscale8_video(brightnessLevel);

		mapLeds.beginFrame();
		mapLeds.setAllLedsColor(fadedColor);
		mapLeds.show();
		// Serial.printf("Factory test color: R=%d, G=%d, B=%d, Brightness=%d\n", fadedColor.r, fadedColor.g, fadedColor.b, b);
		vTaskDelay(pdMS_TO_TICKS(100));  // Delay to allow the LEDs to update
	}
}

/**
 * @brief Wait until the factory button callback runs or a timeout expires.
 * @param timeout Maximum wait time in milliseconds.
 */
inline void waitForPowerButton(int timeout) {
	unsigned long startTime = millis();
	while (!factoryTestPassed.load() && (millis() - startTime < timeout)) {
		vTaskDelay(pdMS_TO_TICKS(10));
	}
}

/**
 * @brief Run the factory color sequence unless it has already passed.
 *
 * Red, green, blue, and low-level white test colors are displayed until the
 * power button confirms that the LEDs are working.
 *
 * @return true if the test ran and was passed during this call.
 * @return false if a previous pass was loaded from NVS.
 */
inline bool factoryTestMode() {
	{
		xSemaphoreTake(fastLEDPreferencesMutex, portMAX_DELAY);
		Preferences localPrefs;
		localPrefs.begin("factory_test", true);
		factoryTestPassed.store(localPrefs.getBool("passed", false));
		localPrefs.end();
		xSemaphoreGive(fastLEDPreferencesMutex);
	}

	if (!factoryTestPassed.load()) {
		buttons.setCallback(POWER_BUTTON, onPowerFactory);
		Serial.println("Factory test mode enabled");
		uint8_t colorIndex = 0;
		const CRGB testColors[] = {
			CRGB(255, 0, 0),
			CRGB(0, 255, 0),
			CRGB(0, 0, 255),
			CRGB(128, 128, 128),
		};

		while (!factoryTestPassed.load()) {
			// Ramp up to full brightnessManager for the current test color
			factorySetColor(testColors[colorIndex]);
#if defined(LED_8_PIXELS)  // If there are >=8 LED channels, show each color for longer
			waitForPowerButton(1500);
#else
			waitForPowerButton(500);
#endif
			colorIndex = (colorIndex + 1) % (sizeof(testColors) / sizeof(testColors[0]));
		}

		mapLeds.beginFrame();
		mapLeds.setAllLedsColor(CRGB::Black);
		mapLeds.show();

		return true;

	} else {
		Serial.println("Factory test passed, skipping.");
		return false;
	}
}