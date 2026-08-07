#include <Arduino.h>
#include <FastLED.h>
#include <esp_freertos_hooks.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <time.h>

#if defined(FACTORY_TEST)
#include "factory.h"
#endif

#if defined(TIMETABLE_SPEED)
#include "timetable.h"
#endif

#include "brightness.h"
#include "buttons.h"
#include "dailySchedule.h"
#include "mapLeds.h"
#include "mapRenderer.h"
#include "modeManager.h"
#include "network.h"
#include "statusLeds.h"
#include "systemInfo.h"

constexpr uint32_t LoopDelayMilliseconds = 30;

void onBrightnessDown() {
	brightnessManager.decrease();
}

void onBrightnessUp() {
	brightnessManager.increase();
}

void onPower() {
	brightnessManager.toggle();
	if (brightnessManager.isOn()) {
		modeManager.requestTimerReset();
	}
	Serial.printf("Power %s\n", brightnessManager.isOn() ? "ON" : "OFF");
}

void onMode() {
	modeManager.requestNextMode();
}

void setup() {
	// Hardware Serial
	// Serial0.begin(115200);
	// Serial0.setDebugOutput(true);

	// USB Serial
	Serial.begin();
	Serial.setDebugOutput(true);
	Serial.setTxBufferSize(1024);  // Increase TX buffer size to reduce dropped characters

	statusLeds.begin();

	mapLeds.begin();
	dailyScheduleManager.begin();

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

	network.begin();

#if defined(TIMETABLE_SPEED) && defined(BETA_BUILD)
	const auto& routes = getAllRoutes();
	printTimetableSize(routes);
#endif
}

void loop() {
	unsigned long currentTime = millis();
	time_t epoch = time(nullptr);  // Get current time

	dailyScheduleManager.update(epoch);
	modeManager.processPendingModeRequest();
	network.setSystemState(modeManager.getTargetNetworkMode(), brightnessManager.isOn());

	if (modeManager.shouldDrawFrame(currentTime)) {
		modeManager.drawCurrentMode(epoch);
	}

	vTaskDelay(pdMS_TO_TICKS(LoopDelayMilliseconds));
}