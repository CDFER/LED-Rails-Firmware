#include "statusLeds.h"

StatusLedManager statusLeds;

void StatusLedManager::begin() {
	xTaskCreate(task, "Status LED Manager", 2048, this, 5, &taskHandle);
}

void StatusLedManager::setState(
    uint8_t firstPin, StatusLedCommand firstCommand, uint8_t secondPin, StatusLedCommand secondCommand) {
	if (taskHandle != nullptr) {
		uint32_t notification = (firstPin << 24) | (firstCommand << 16) | (secondPin << 8) | secondCommand;
		xTaskNotify(taskHandle, notification, eSetValueWithOverwrite);
	}
}

void StatusLedManager::setState(uint8_t pin, StatusLedCommand command) {
	setState(pin, command, 0, LED_OFF);
}

void StatusLedManager::setCharlieplexedLed(uint8_t pin, StatusLedCommand command) {
	switch (command) {
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

void StatusLedManager::task(void* pvParameters) {
// Default LEDs as originally configured
// Using conditionally defined pins since they come from build_flags
#ifndef WIFI_LED_PIN
#define WIFI_LED_PIN 255
#endif
#ifndef SERVER_LED_PIN
#define SERVER_LED_PIN 255
#endif

	StatusLed ledStates[] = { { WIFI_LED_PIN, LED_OFF, false, 0 }, { SERVER_LED_PIN, LED_OFF, false, 0 } };
	const int ledCount = sizeof(ledStates) / sizeof(ledStates[0]);

	while (true) {
		// Check for notifications
		uint32_t notification;
		if (xTaskNotifyWait(0, ULONG_MAX, &notification, 0) == pdTRUE) {
			// Process up to two commands
			for (int commandIndex = 0; commandIndex < 2; commandIndex++) {
				uint8_t commandPin = (notification >> (24 - (commandIndex * 16))) & 0xFF;
				StatusLedCommand command =
				    static_cast<StatusLedCommand>((notification >> (16 - (commandIndex * 16))) & 0xFF);

				// Skip invalid pins (0 means no command)
				if (commandPin == 0 || commandPin == 255)
					continue;

				for (int ledIndex = 0; ledIndex < ledCount; ledIndex++) {
					if (ledStates[ledIndex].pin == commandPin) {
						ledStates[ledIndex].command = command;
						if (command == LED_ON_GREEN || command == LED_ON_RED || command == LED_OFF) {
							setCharlieplexedLed(commandPin, command);
						}
						break;
					}
				}
			}
		}

		// Handle blinking
		unsigned long currentTime = millis();
		for (int ledIndex = 0; ledIndex < ledCount; ledIndex++) {
			if (ledStates[ledIndex].pin == 255)
				continue;  // Skip unconfigured pins

			if (ledStates[ledIndex].command >= LED_BLINK_GREEN_SLOW) {
				const bool isGreen = (ledStates[ledIndex].command == LED_BLINK_GREEN_SLOW
				                      || ledStates[ledIndex].command == LED_BLINK_GREEN_FAST);
				const bool isRed = (ledStates[ledIndex].command == LED_BLINK_RED_SLOW
				                    || ledStates[ledIndex].command == LED_BLINK_RED_FAST);
				const bool isSlow = (ledStates[ledIndex].command == LED_BLINK_GREEN_SLOW
				                     || ledStates[ledIndex].command == LED_BLINK_RED_SLOW);

				if (isGreen || isRed) {
					const int blinkInterval = isSlow ? 500 : 100;
					const StatusLedCommand solidColor = isGreen ? LED_ON_GREEN : LED_ON_RED;

					if (currentTime - ledStates[ledIndex].lastToggle >= blinkInterval) {
						ledStates[ledIndex].currentState = !ledStates[ledIndex].currentState;
						setCharlieplexedLed(
						    ledStates[ledIndex].pin, ledStates[ledIndex].currentState ? solidColor : LED_OFF);
						ledStates[ledIndex].lastToggle = currentTime;
					}
				}
			}
		}

		vTaskDelay(pdMS_TO_TICKS(25));
	}
}
