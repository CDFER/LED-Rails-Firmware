#include <Arduino.h>
#include <FastLED.h>
#include <time.h>

#if defined(FACTORY_TEST)
	#include "factory.h"
#endif

#if defined(TIMETABLE_SPEED)
	#include "timetable.h"
#endif

#include "brightness.h"
#include "buttons.h"
#include "mapLEDs.h"
#include "mapRenderer.h"
#include "modeManager.h"
#include "network.h"
#include "statusLeds.h"
#include "systemInfo.h"

constexpr uint32_t LoopDelayMs = 30;

void onBrightnessDown() {
	brightness.decrease();
}

void onBrightnessUp() {
	brightness.increase();
}

void onPower() {
	brightness.toggle();
	if (brightness.isOn()) {
		modeManager.resetTimer();
	}
	Serial.printf("Power %s\n", brightness.isOn() ? "ON" : "OFF");
}

void onMode() {
	modeManager.nextMode();
}

void setup() {
	// Hardware Serial
	// Serial0.begin(115200);

	// USB Serial
	Serial.begin();
	Serial.setDebugOutput(true);

	mapLEDs.begin();
	modeManager.begin();

	// --- Setup Buttons ---
	buttons.add(BRIGHTNESS_DOWN_BUTTON, onBrightnessDown);
	buttons.add(BRIGHTNESS_UP_BUTTON, onBrightnessUp);
#if defined(MODE_BUTTON)
	buttons.add(POWER_BUTTON, onPower);
	buttons.add(MODE_BUTTON, onMode);
#else
	buttons.add(POWER_BUTTON, onPower, onMode);
#endif
	buttons.begin();

	Serial.println(getSystemInfo());

#if defined(FACTORY_TEST)
	#if defined(TIMETABLE_SPEED)
	if (factoryTestMode()) {
		modeManager.setMode(HIGH_SPEED_TIMETABLE_MODE);
	}
	#endif
	buttons.setCallback(POWER_BUTTON, onPower);
#endif

	statusLEDs.begin();

	network.begin();

#if defined(TIMETABLE_SPEED) && defined(BETA_BUILD)
	const auto& routes = getAllRoutes();
	printTimetableSize(routes);
#endif
}

void loop() {
	unsigned long now = millis();
	time_t epoch = time(nullptr);  // Get current time

	network.setSystemState(modeManager.getTargetNetworkMode(), brightness.isOn());

	if (modeManager.shouldDrawFrame(now)) {
		modeManager.drawCurrentMode(epoch);
	}

	vTaskDelay(pdMS_TO_TICKS(LoopDelayMs));
}