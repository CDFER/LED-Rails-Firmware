#include "buttons.h"

#include "ioPins.h"

#if defined(GPIO_EXPANDER)
#include "gpioExpander.h"
#endif

ButtonManager buttons;

namespace {
TaskHandle_t buttonTaskHandle = nullptr;
constexpr uint32_t ButtonPollIntervalMs = 10;

/** @brief Wakes the button task on any button edge. Does no debouncing itself. */
void IRAM_ATTR buttonInterruptHandler() {
	if (buttonTaskHandle == nullptr) {
		return;
	}
	BaseType_t higherPriorityTaskWoken = pdFALSE;
	vTaskNotifyGiveFromISR(buttonTaskHandle, &higherPriorityTaskWoken);
	if (higherPriorityTaskWoken) {
		portYIELD_FROM_ISR();
	}
}
}

void ButtonManager::add(
    uint8_t pin, ButtonCallback callback, ButtonCallback longPressCallback, uint16_t longPressDuration) {
	if (started) {
		Serial.printf("Button on pin %d added after begin(); ignored\n", pin);
		return;
	}
	buttons.push_back({ pin, std::move(callback), std::move(longPressCallback), longPressDuration });
}

ButtonManager::Button* ButtonManager::findButton(uint8_t pin) {
	for (auto& button : buttons) {
		if (button.pin == pin) {
			return &button;
		}
	}
	return nullptr;
}

ButtonManager::ButtonCallback ButtonManager::snapshot(const ButtonCallback& callback) {
	std::lock_guard<std::mutex> lock(callbackMutex);
	return callback;
}

void ButtonManager::setCallback(uint8_t pin, ButtonCallback callback) {
	std::lock_guard<std::mutex> lock(callbackMutex);
	if (Button* button = findButton(pin)) {
		button->callback = std::move(callback);
		return;
	}
	Serial.printf("Button on pin %d not found!\n", pin);
}

void ButtonManager::setLongPressCallback(uint8_t pin, ButtonCallback longPressCallback) {
	std::lock_guard<std::mutex> lock(callbackMutex);
	if (Button* button = findButton(pin)) {
		button->longPressCallback = std::move(longPressCallback);
		return;
	}
	Serial.printf("Button on pin %d not found!\n", pin);
}

void ButtonManager::begin() {
	const TickType_t now = xTaskGetTickCount();

	for (auto& button : buttons) {
		if (isExpanderPin(button.pin)) {
			hasExpanderButtons = true;
			ioPinMode(button.pin, INPUT);
		} else {
			ioPinMode(button.pin, INPUT_PULLUP);
		}
	}

	if (hasExpanderButtons) {
		ioRefreshInputs();
	}

	for (auto& button : buttons) {
		button.rawPressed = ioDigitalRead(button.pin) == LOW;
		button.pressed = button.rawPressed;
		button.rawChangeTick = now;
		button.pressStartTick = now;
		// A button held during boot is ignored until it is released, as before.
		button.longPressTriggered = button.pressed;
	}

	started = true;
	xTaskCreate(buttonTask, "ButtonTask", 4096, this, 2, &buttonTaskHandle);

	for (auto& button : buttons) {
		// Expander pins without an INT line are handled by polling alone.
		ioAttachInterrupt(button.pin, buttonInterruptHandler, CHANGE);
	}
}

void ButtonManager::scan(TickType_t now) {
	const TickType_t debounceTicks = pdMS_TO_TICKS(DEBOUNCE_MS);

	for (auto& button : buttons) {
		const bool sampledPressed = ioDigitalRead(button.pin) == LOW;
		if (sampledPressed != button.rawPressed) {
			button.rawPressed = sampledPressed;
			button.rawChangeTick = now;
		}

		if (button.rawPressed != button.pressed && (now - button.rawChangeTick) >= debounceTicks) {
			button.pressed = button.rawPressed;
			if (button.pressed) {
				button.pressStartTick = now;
				button.longPressTriggered = false;
			} else if (!button.longPressTriggered) {
				if (ButtonCallback callback = snapshot(button.callback)) {
					callback();
				}
			}
		}

		if (button.pressed && !button.longPressTriggered
		    && (now - button.pressStartTick) >= pdMS_TO_TICKS(button.longPressDuration)) {
			if (ButtonCallback longPressCallback = snapshot(button.longPressCallback)) {
				button.longPressTriggered = true;
				longPressCallback();
			}
		}
	}
}

void ButtonManager::buttonTask(void* pvParameters) {
	ButtonManager* manager = static_cast<ButtonManager*>(pvParameters);

	while (true) {
		ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ButtonPollIntervalMs));
		if (manager->hasExpanderButtons) {
			ioRefreshInputs();
		}
		manager->scan(xTaskGetTickCount());
	}
}
