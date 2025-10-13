#pragma once

#include <Arduino.h>
#include <functional>
#include <vector>

/**
 * @brief Structure representing a button press event
 * 
 * This struct is used to pass button events through the FreeRTOS queue
 * from the ISR context to the button handling task.
 */
struct ButtonEvent {
	uint8_t pin;	   //< GPIO pin number of the button that was pressed
	bool isLongPress;  // Flag indicating if this is a long press event
};

// FreeRTOS queue handle for button events
QueueHandle_t buttonQueue;

// Default long press duration in milliseconds
static const uint16_t DEFAULT_LONG_PRESS_MS = 500;

/**
 * @brief Button manager class for handling multiple button inputs with debouncing
 * 
 * This class provides a clean interface for managing multiple buttons with
 * interrupt-driven detection and debouncing. It uses FreeRTOS tasks and queues
 * to handle button events asynchronously.
 */
class ButtonManager {
  public:
	/// Function pointer type for button callback functions
	using ButtonCallback = std::function<void()>;

	/**
	 * @brief Structure representing a button configuration
	 * 
	 * Holds all the necessary information for a single button including
	 * its pin, callback function, and debouncing state variables.
	 */
	struct Button {
		uint8_t pin;					   // GPIO pin number for this button
		ButtonCallback callback;		   // Function to call when button is pressed
		ButtonCallback longPressCallback;  // Function to call when button is long pressed
		uint16_t longPressDuration;		   // Duration in ms to consider a long press

		// For debouncing
		bool state;					// Current state of the button (assumes idle state is HIGH with INPUT_PULLUP)
		TickType_t fallingTick;		// Tick count when button last transitioned to LOW
		TickType_t risingTick;		// Tick count when button last transitioned to HIGH
		TickType_t pressStartTick;	// Tick count when button press started (for long press detection)
		bool longPressTriggered;	// Flag to prevent multiple long press events

		/**
		 * @brief Construct a new Button object
		 * 
		 * @param p GPIO pin number
		 * @param cb Callback function to execute on button press
		 * @param lpcb Callback function to execute on long press
		 * @param lpd Duration in ms to consider a long press
		 * @param st Initial state of the button (default: HIGH)
		 * @param ft Initial falling tick time (default: 0)
		 * @param rt Initial rising tick time (default: 0)
		 */
		Button(
			uint8_t p, ButtonCallback cb, ButtonCallback lpcb, uint16_t lpd, bool st = HIGH, TickType_t ft = 0, TickType_t rt = 0)
			: pin(p), callback(cb), longPressCallback(lpcb), longPressDuration(lpd), state(st), fallingTick(ft), risingTick(rt),
			  pressStartTick(0), longPressTriggered(false) {}
	};

	/// Container for all registered buttons
	std::vector<Button> buttons;

	/**
	 * @brief Add a new button to the manager
	 * 
	 * Registers a new button with the specified pin and callback function.
	 * The button will be configured with INPUT_PULLUP mode.
	 * 
	 * @param pin GPIO pin number for the button
	 * @param cb Callback function to execute when button is pressed
	 * @param lpcb Callback function to execute when button is long pressed (optional)
	 * @param longPressDuration Duration in ms to consider a long press (default: 1000ms)
	 */
	void add(uint8_t pin, ButtonCallback cb, ButtonCallback lpcb = nullptr, uint16_t longPressDuration = DEFAULT_LONG_PRESS_MS) {
		buttons.push_back({ pin, cb, lpcb, longPressDuration, HIGH, 0, 0 });
	}

	/**
	 * @brief Update the callback function for an existing button
	 * 
	 * Changes the callback function associated with a button on the specified pin.
	 * If no button is found on that pin, an error message is printed to Serial.
	 * 
	 * @param pin GPIO pin number of the button to update
	 * @param cb New callback function to execute when button is pressed
	 */
	void setCallback(uint8_t pin, ButtonCallback cb) {
		for (auto& btn : buttons) {
			if (btn.pin == pin) {
				btn.callback = cb;
				return;
			}
		}
		Serial.printf("Button on pin %d not found!\n", pin);
	}

	/**
	 * @brief Update the long press callback function for an existing button
	 * 
	 * Changes the long press callback function associated with a button on the specified pin.
	 * If no button is found on that pin, an error message is printed to Serial.
	 * 
	 * @param pin GPIO pin number of the button to update
	 * @param lpcb New callback function to execute when button is long pressed
	 */
	void setLongPressCallback(uint8_t pin, ButtonCallback lpcb) {
		for (auto& btn : buttons) {
			if (btn.pin == pin) {
				btn.longPressCallback = lpcb;
				return;
			}
		}
		Serial.printf("Button on pin %d not found!\n", pin);
	}

	/**
	 * @brief Initialize the button manager
	 * 
	 * Performs initial setup including:
	 * 
	 * - Creating the FreeRTOS queue for button events
	 * 
	 * - Configuring all registered buttons with INPUT_PULLUP
	 * 
	 * - Attaching interrupt handlers for each button
	 * 
	 * - Starting the button handling task
	 */
	void begin() {
		buttonQueue = xQueueCreate(20, sizeof(ButtonEvent));  // Increased queue size
		if (buttonQueue == NULL) {
			Serial.println("Failed to create button queue!");
			return;
		}

		for (auto& btn : buttons) {
			pinMode(btn.pin, INPUT_PULLUP);
			attachInterruptArg(digitalPinToInterrupt(btn.pin),
							   isrWrapper,
							   &btn,
							   CHANGE  // Trigger on both rising and falling edges
			);
		}

		xTaskCreate(buttonTask, "ButtonTask", 2048, this, 2, NULL);
	}

  private:
	static const int buttonTaskPollingInterval = 50;  // ms

	/**
	 * @brief Interrupt service routine wrapper for button state changes
	 * 
	 * This static function is called when a button's state changes. It handles
	 * debouncing by checking the time between falling and rising edges.
	 * Valid button presses are sent to the button queue for processing by the task.
	 * 
	 * @param arg Pointer to the Button object that triggered the interrupt
	 */
	static void IRAM_ATTR isrWrapper(void* arg) {
		Button* button = static_cast<Button*>(arg);
		TickType_t now = xTaskGetTickCountFromISR();

		bool newState = digitalRead(button->pin);

		if (newState != button->state) {
			button->state = newState;

			if (newState == LOW) {
				// Button pressed
				button->fallingTick = now;
				button->pressStartTick = now;
				button->longPressTriggered = false;	 // Reset long press flag
			} else {
				// Button released
				button->risingTick = now;
				// Only send short press if it's debounced and not already handled as long press
				if ((button->risingTick - button->fallingTick) > pdMS_TO_TICKS(DEBOUNCE_MS) && !button->longPressTriggered) {
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

	/**
	 * @brief FreeRTOS task for processing button events
	 * 
	 * This task runs periodically, waiting for button events from the queue.
	 * When an event is received, it finds the corresponding button and executes its callback.
	 * It also handles long press detection.
	 * 
	 * @param pvParameters Pointer to the ButtonManager instance
	 */
	static void buttonTask(void* pvParameters) {
		ButtonManager* manager = static_cast<ButtonManager*>(pvParameters);
		ButtonEvent event;
		TickType_t lastCheckTime = xTaskGetTickCount();

		while (true) {
			// Check for long press events
			TickType_t currentTime = xTaskGetTickCount();
			TickType_t timeSinceLastCheck = currentTime - lastCheckTime;
			lastCheckTime = currentTime;

			for (auto& btn : manager->buttons) {
				// If button is currently pressed, has long press callback, and long press not yet triggered
				if (btn.state == LOW && btn.longPressCallback != nullptr && !btn.longPressTriggered) {
					// Check if enough time has passed for a long press
					TickType_t pressDuration = currentTime - btn.pressStartTick;
					if (pressDuration >= pdMS_TO_TICKS(btn.longPressDuration)) {
						// Send long press event
						ButtonEvent longPressEvent = { btn.pin, true };
						xQueueSend(buttonQueue, &longPressEvent, 0);
						btn.longPressTriggered = true;	// Mark long press as triggered
					}
				}
			}

			// Process button events from queue
			if (xQueueReceive(buttonQueue, &event, pdMS_TO_TICKS(buttonTaskPollingInterval))) {
				for (auto& btn : manager->buttons) {
					if (btn.pin == event.pin) {
						if (event.isLongPress) {
							if (btn.longPressCallback) {
								btn.longPressCallback();
							}
						} else {
							btn.callback();
						}
						break;
					}
				}
			}
		}
	}
};