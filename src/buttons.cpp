#include "buttons.h"

// Define the global instances
QueueHandle_t buttonQueue;
ButtonManager buttons;

void ButtonManager::add(
    uint8_t pin, ButtonCallback callback, ButtonCallback longPressCallback, uint16_t longPressDuration) {
	buttons.push_back({ pin, callback, longPressCallback, longPressDuration, HIGH, 0, 0 });
}

void ButtonManager::setCallback(uint8_t pin, ButtonCallback callback) {
	for (auto& button : buttons) {
		if (button.pin == pin) {
			button.callback = callback;
			return;
		}
	}
	Serial.printf("Button on pin %d not found!\n", pin);
}

void ButtonManager::setLongPressCallback(uint8_t pin, ButtonCallback longPressCallback) {
	for (auto& button : buttons) {
		if (button.pin == pin) {
			button.longPressCallback = longPressCallback;
			return;
		}
	}
	Serial.printf("Button on pin %d not found!\n", pin);
}

void ButtonManager::begin() {
	buttonQueue = xQueueCreate(20, sizeof(ButtonEvent));
	if (buttonQueue == NULL) {
		Serial.println("Failed to create button queue!");
		return;
	}

	for (auto& button : buttons) {
		pinMode(button.pin, INPUT_PULLUP);
		attachInterruptArg(digitalPinToInterrupt(button.pin),
		                   isrWrapper,
		                   &button,
		                   CHANGE  // Trigger on both rising and falling edges
		);
	}

	xTaskCreate(buttonTask, "ButtonTask", 4096, this, 2, NULL);
}

void IRAM_ATTR ButtonManager::isrWrapper(void* buttonArgument) {
	Button* button = static_cast<Button*>(buttonArgument);
	TickType_t currentTick = xTaskGetTickCountFromISR();

	bool newState = digitalRead(button->pin);

	if (newState != button->state) {
		button->state = newState;

		if (newState == LOW) {
			// Button pressed
			button->fallingTick = currentTick;
			button->pressStartTick = currentTick;
			button->longPressTriggered = false;  // Reset long press flag
		} else {
			// Button released
			button->risingTick = currentTick;
			// Only send short press if it's debounced and not already handled as long press
			if ((button->risingTick - button->fallingTick) > pdMS_TO_TICKS(DEBOUNCE_MS)
			    && !button->longPressTriggered) {
				ButtonEvent event = { button->pin, false };
				BaseType_t xHigherPriorityTaskWoken = pdFALSE;
				xQueueSendFromISR(buttonQueue, &event, &xHigherPriorityTaskWoken);
				if (xHigherPriorityTaskWoken) {
					portYIELD_FROM_ISR();
				}
			}
		}
	}
}

void ButtonManager::buttonTask(void* pvParameters) {
	ButtonManager* manager = static_cast<ButtonManager*>(pvParameters);
	ButtonEvent event;

	while (true) {
		// Check for long press events
		TickType_t currentTime = xTaskGetTickCount();

		for (auto& button : manager->buttons) {
			// If button is currently pressed, has long press callback, and long press not yet triggered
			if (button.state == LOW && button.longPressCallback != nullptr && !button.longPressTriggered) {
				// Check if enough time has passed for a long press
				TickType_t pressDuration = currentTime - button.pressStartTick;
				if (pressDuration >= pdMS_TO_TICKS(button.longPressDuration)) {
					// Send long press event
					ButtonEvent longPressEvent = { button.pin, true };
					xQueueSend(buttonQueue, &longPressEvent, 0);
					button.longPressTriggered = true;  // Mark long press as triggered
				}
			}
		}

		// Process button events from queue
		if (xQueueReceive(buttonQueue, &event, pdMS_TO_TICKS(buttonTaskPollingInterval))) {
			for (auto& button : manager->buttons) {
				if (button.pin == event.pin) {
					if (event.isLongPress) {
						if (button.longPressCallback) {
							button.longPressCallback();
						}
					} else {
						button.callback();
					}
					break;
				}
			}
		}
	}
}
