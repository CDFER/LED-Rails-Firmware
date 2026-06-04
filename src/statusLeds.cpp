#include "statusLEDs.h"

StatusLEDManager statusLEDs;

void StatusLEDManager::begin() {
	xTaskCreate(taskMethod, "Status LED Manager", 1024, this, 2, &taskHandle);
}

void StatusLEDManager::setState(uint8_t pin1, statusLedCommand cmd1, uint8_t pin2, statusLedCommand cmd2) {
	if (taskHandle != nullptr) {
		uint32_t notification = (pin1 << 24) | (cmd1 << 16) | (pin2 << 8) | cmd2;
		xTaskNotify(taskHandle, notification, eSetValueWithOverwrite);
	}
}

void StatusLEDManager::setState(uint8_t pin, statusLedCommand command) {
	setState(pin, command, 0, LED_OFF);
}

void StatusLEDManager::setCharlieplexedLED(uint8_t pin, statusLedCommand state) {
	switch (state) {
		case LED_ON_GREEN:
			pinMode(pin, OUTPUT);
			digitalWrite(pin, HIGH);
			break;

		case LED_ON_RED:
			pinMode(pin, OUTPUT);
			digitalWrite(pin, LOW);
			break;

		case LED_OFF:
			// Set as input (High Resistance) to disable output driver
			pinMode(pin, INPUT);
			break;

		default: break;
	}
}

void StatusLEDManager::taskMethod(void* pvParameters) {
// Default LEDs as originally configured
// Using conditionally defined pins since they might come from build_flags
#ifndef WIFI_LED_PIN
	#define WIFI_LED_PIN 255
#endif
#ifndef SERVER_LED_PIN
	#define SERVER_LED_PIN 255
#endif

	statusLed leds[] = { { WIFI_LED_PIN, LED_OFF, false, 0 }, { SERVER_LED_PIN, LED_OFF, false, 0 } };
	const int numLeds = sizeof(leds) / sizeof(leds[0]);

	while (1) {
		// Check for notifications
		uint32_t notification;
		if (xTaskNotifyWait(0, ULONG_MAX, &notification, 0) == pdTRUE) {
			// Process up to two commands
			for (int cmdIdx = 0; cmdIdx < 2; cmdIdx++) {
				uint8_t pin = (notification >> (24 - (cmdIdx * 16))) & 0xFF;
				statusLedCommand cmd = static_cast<statusLedCommand>((notification >> (16 - (cmdIdx * 16))) & 0xFF);

				// Skip invalid pins (0 means no command)
				if (pin == 0 || pin == 255)
					continue;

				for (int i = 0; i < numLeds; i++) {
					if (leds[i].pin == pin) {
						leds[i].command = cmd;
						if (cmd == LED_ON_GREEN || cmd == LED_ON_RED || cmd == LED_OFF) {
							setCharlieplexedLED(pin, cmd);
						}
						break;
					}
				}
			}
		}

		// Handle blinking
		unsigned long now = millis();
		for (int i = 0; i < numLeds; i++) {
			if (leds[i].pin == 255)
				continue;  // Skip unconfigured pins

			if (leds[i].command >= LED_BLINK_GREEN_SLOW) {
				const bool isGreen = (leds[i].command == LED_BLINK_GREEN_SLOW || leds[i].command == LED_BLINK_GREEN_FAST);
				const bool isRed = (leds[i].command == LED_BLINK_RED_SLOW || leds[i].command == LED_BLINK_RED_FAST);
				const bool isSlow = (leds[i].command == LED_BLINK_GREEN_SLOW || leds[i].command == LED_BLINK_RED_SLOW);

				if (isGreen || isRed) {
					const int interval = isSlow ? 500 : 100;
					const statusLedCommand color = isGreen ? LED_ON_GREEN : LED_ON_RED;

					if (now - leds[i].lastToggle >= interval) {
						leds[i].currentState = !leds[i].currentState;
						setCharlieplexedLED(leds[i].pin, leds[i].currentState ? color : LED_OFF);
						leds[i].lastToggle = now;
					}
				}
			}
		}

		vTaskDelay(pdMS_TO_TICKS(25));
	}
}
