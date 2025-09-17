#include <Arduino.h>
#include <FastLED.h>
#include <Preferences.h>
#include <buttons.h>

extern Preferences preferences;

extern ButtonManager buttons;

extern CRGB leds1[];
#if defined(LED_2_PIN)
extern CRGB leds2[];
#endif

bool passed;

void onPowerFactory() {
	passed = true;
	preferences.putBool("passed", passed);	// Toggle factory test mode pass/fail state
	Serial.println("Factory test mode saved as passed");
	preferences.end();
}

void factorySetColor(CRGB color) {

	fill_solid(leds1, LED_1_PIXELS, color);
#if defined(LED_2_PIN)
	fill_solid(leds2, LED_2_PIXELS, color);
#endif
	FastLED.show();
}

void waitForPowerButton(int timeout) {
	unsigned long startTime = millis();
	while (!passed && (millis() - startTime < timeout)) {
		vTaskDelay(pdMS_TO_TICKS(100));
	}
}

void factoryTestMode() {
	preferences.begin("factory_test");
	passed = preferences.getBool("passed", false);
	if (passed == false) {
		buttons.setCallback(POWER_BUTTON, onPowerFactory);
		Serial.println("Factory test mode enabled");
		uint8_t colorIndex = 0;
		const CRGB testColors[] = { CRGB(128, 0, 0), CRGB(0, 128, 0), CRGB(0, 0, 128) };

		while (!passed) {
			factorySetColor(testColors[colorIndex]);
			waitForPowerButton(1000);
			colorIndex = (colorIndex + 1) % (sizeof(testColors) / sizeof(testColors[0]));
		}

	} else {
		Serial.println("Factory test passed, skipping.");
		preferences.end();
	}
}