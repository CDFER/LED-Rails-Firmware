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
	uint8_t pin;  //< GPIO pin number of the button that was pressed
};

// FreeRTOS queue handle for button events
QueueHandle_t buttonQueue;

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
		uint8_t pin;			  // GPIO pin number for this button
		ButtonCallback callback;  // Function to call when button is pressed

		// For debouncing
		bool state;				 // Current state of the button (assumes idle state is HIGH with INPUT_PULLUP)
		TickType_t fallingTick;	 // Tick count when button last transitioned to LOW
		TickType_t risingTick;	 // Tick count when button last transitioned to HIGH

		/**
		 * @brief Construct a new Button object
		 * 
		 * @param p GPIO pin number
		 * @param cb Callback function to execute on button press
		 * @param st Initial state of the button (default: HIGH)
		 * @param ft Initial falling tick time (default: 0)
		 * @param rt Initial rising tick time (default: 0)
		 * @param it Initial ISR time (default: 0)
		 */
		Button(uint8_t p, ButtonCallback cb, bool st = HIGH, TickType_t ft = 0, TickType_t rt = 0)
			: pin(p), callback(cb), state(st), fallingTick(ft), risingTick(rt) {}
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
	 */
	void add(uint8_t pin, ButtonCallback cb) {
		buttons.push_back({ pin, cb, HIGH, 0, 0 });
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
		buttonQueue = xQueueCreate(10, sizeof(ButtonEvent));
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

		xTaskCreate(buttonTask, "ButtonTask", 2048, this, 1, NULL);
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
				button->fallingTick = now;
			} else {
				button->risingTick = now;
				if ((button->risingTick - button->fallingTick) > pdMS_TO_TICKS(DEBOUNCE_MS)) {
					ButtonEvent event = { button->pin };
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
	 * 
	 * @param pvParameters Pointer to the ButtonManager instance
	 */
	static void buttonTask(void* pvParameters) {
		ButtonManager* manager = static_cast<ButtonManager*>(pvParameters);
		ButtonEvent event;

		while (true) {
			if (xQueueReceive(buttonQueue, &event, portMAX_DELAY)) {
				for (auto& btn : manager->buttons) {
					if (btn.pin == event.pin) {
						btn.callback();
						break;
					}
				}
			}
			vTaskDelay(pdMS_TO_TICKS(buttonTaskPollingInterval));
		}
	}
};