#include "statusLeds.h"

#include "ioPins.h"

StatusLedManager statusLeds;

namespace {
// Charlieplexed LEDs share pins, so their ID 0 or 255 means "no LED".
std::unique_ptr<StatusLedOutput> createCharlieplexedOutput(uint8_t pin) {
	if (pin == 0 || pin == 255) {
		return nullptr;
	}
	return std::unique_ptr<StatusLedOutput>(new CharlieplexedLedOutput(pin));
}
}

CharlieplexedLedOutput::CharlieplexedLedOutput(uint8_t pin) : pin(pin) {
	apply(LED_OFF);
}

void CharlieplexedLedOutput::apply(StatusLedCommand command) {
	switch (command) {
		case LED_ON_GREEN:
			ioPinMode(pin, OUTPUT);
			ioDigitalWrite(pin, HIGH);
			break;

		case LED_ON_RED:
			ioPinMode(pin, OUTPUT);
			ioDigitalWrite(pin, LOW);
			break;

		case LED_OFF:
			// Input (high impedance) disables the output driver.
			ioPinMode(pin, INPUT);
			break;

		default: break;
	}
}

DualColourLedOutput::DualColourLedOutput(uint8_t redPin, uint8_t greenPin, uint8_t redDim, uint8_t greenDim)
	: redPin(redPin), greenPin(greenPin), redDim(redDim), greenDim(greenDim) {
	// LED mode uses the expander's constant-current sink, so no series resistor is needed.
	ioPinModeLedCurrent(redPin);
	ioPinModeLedCurrent(greenPin);
	apply(LED_OFF);
}

void DualColourLedOutput::apply(StatusLedCommand command) {
	// Turn the other colour off first so the LED never shows both colours.
	switch (command) {
		case LED_ON_GREEN:
			ioSetLedCurrent(redPin, 0);
			ioSetLedCurrent(greenPin, greenDim);
			break;

		case LED_ON_RED:
			ioSetLedCurrent(greenPin, 0);
			ioSetLedCurrent(redPin, redDim);
			break;

		case LED_OFF:
			ioSetLedCurrent(redPin, 0);
			ioSetLedCurrent(greenPin, 0);
			break;

		default: break;
	}
}

void StatusLedManager::begin() {
	commandQueue = xQueueCreate(10, sizeof(uint32_t));

#if defined(POWER_LED_R_PIN)
	// Red-only power LED (green is tied to 3.3 V). Hold it off from boot.
	ioPinModeLedCurrent(POWER_LED_R_PIN);
	ioSetLedCurrent(POWER_LED_R_PIN, 0);
#endif

	leds[0].id = WIFI_LED_PIN;
	leds[1].id = SERVER_LED_PIN;
#if defined(WIFI_LED_R_PIN)
	leds[0].output.reset(new DualColourLedOutput(WIFI_LED_R_PIN, WIFI_LED_G_PIN, STATUS_LED_RED_DIM, STATUS_LED_GREEN_DIM));
#else
	leds[0].output = createCharlieplexedOutput(WIFI_LED_PIN);
#endif
#if defined(SERVER_LED_R_PIN)
	leds[1].output.reset(new DualColourLedOutput(SERVER_LED_R_PIN, SERVER_LED_G_PIN, STATUS_LED_RED_DIM, STATUS_LED_GREEN_DIM));
#else
	leds[1].output = createCharlieplexedOutput(SERVER_LED_PIN);
#endif

	xTaskCreate(task, "Status LED Manager", 3072, this, 5, &taskHandle);
}

void StatusLedManager::setState(
    uint8_t firstPin, StatusLedCommand firstCommand, uint8_t secondPin, StatusLedCommand secondCommand) {
	if (commandQueue != nullptr) {
		uint32_t notification = (firstPin << 24) | (firstCommand << 16) | (secondPin << 8) | secondCommand;
		xQueueSend(commandQueue, &notification, 0);
	}
}

void StatusLedManager::setState(uint8_t pin, StatusLedCommand command) {
	setState(pin, command, 0, LED_OFF);
}

void StatusLedManager::task(void* pvParameters) {
	StatusLedManager* manager = static_cast<StatusLedManager*>(pvParameters);

	while (true) {
		// Check for queued commands
		uint32_t notification;
		if (xQueueReceive(manager->commandQueue, &notification, 0) == pdTRUE) {
			// Process up to two commands
			for (int commandIndex = 0; commandIndex < 2; commandIndex++) {
				uint8_t commandPin = (notification >> (24 - (commandIndex * 16))) & 0xFF;
				StatusLedCommand command =
				    static_cast<StatusLedCommand>((notification >> (16 - (commandIndex * 16))) & 0xFF);

				// Skip invalid pins (0 means no command)
				if (commandPin == 0 || commandPin == 255)
					continue;

				for (auto& led : manager->leds) {
					if (led.id == commandPin) {
						led.command = command;
						if (led.output
						    && (command == LED_ON_GREEN || command == LED_ON_RED || command == LED_OFF)) {
							led.output->apply(command);
						}
						break;
					}
				}
			}
		}

		// Handle blinking
		unsigned long currentTime = millis();
		for (auto& led : manager->leds) {
			if (!led.output)
				continue;  // Skip unconfigured LEDs

			if (led.command >= LED_BLINK_GREEN_SLOW) {
				const bool isGreen = (led.command == LED_BLINK_GREEN_SLOW || led.command == LED_BLINK_GREEN_FAST);
				const bool isRed = (led.command == LED_BLINK_RED_SLOW || led.command == LED_BLINK_RED_FAST);
				const bool isSlow = (led.command == LED_BLINK_GREEN_SLOW || led.command == LED_BLINK_RED_SLOW);

				if (isGreen || isRed) {
					const int blinkInterval = isSlow ? 500 : 100;
					const StatusLedCommand solidColor = isGreen ? LED_ON_GREEN : LED_ON_RED;

					if (currentTime - led.lastToggle >= blinkInterval) {
						led.currentState = !led.currentState;
						led.output->apply(led.currentState ? solidColor : LED_OFF);
						led.lastToggle = currentTime;
					}
				}
			}
		}

		vTaskDelay(pdMS_TO_TICKS(25));
	}
}
