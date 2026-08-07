/**
 * @file buttons.h
 * @brief Interrupt-driven button input, debouncing, and callback dispatch.
 */

#pragma once

#include <Arduino.h>
#include <functional>
#include <vector>

/**
 * @brief Button event passed from the interrupt handler to the button task.
 *
 * The event contains only the pin and press type so the interrupt service
 * routine can remain short and defer callbacks to task context.
 */
struct ButtonEvent {
	uint8_t pin;       ///< GPIO pin number that generated the event.
	bool isLongPress;  ///< True for a long-press event, false for a debounced release.
};

/// FreeRTOS queue used to transfer button events out of interrupt context.
extern QueueHandle_t buttonQueue;

/// Default duration in milliseconds before a held button becomes a long press.
const uint16_t defaultLongPressMs = 500;

/**
 * @brief Manages multiple active-low buttons with debouncing and callbacks.
 *
 * Buttons are configured with INPUT_PULLUP and monitored on both signal edges.
 * Short-press callbacks run from the button task after release; long-press
 * callbacks run from the same task once their configured duration elapses.
 */
class ButtonManager {
  public:
	/// Callback invoked from button-task context after a press is recognized.
	using ButtonCallback = std::function<void()>;

	/**
	 * @brief Configuration and runtime state for one registered button.
	 */
	struct Button {
		uint8_t pin;                       ///< GPIO pin number for this button
		ButtonCallback callback;           ///< Function to call when button is pressed
		ButtonCallback longPressCallback;  ///< Function to call when button is long pressed
		uint16_t longPressDuration;        ///< Duration in ms to consider a long press

		bool state;                 ///< Current GPIO state; HIGH is idle and LOW is pressed.
		TickType_t fallingTick;     ///< Tick count when the button last transitioned to LOW.
		TickType_t risingTick;      ///< Tick count when the button last transitioned to HIGH.
		TickType_t pressStartTick;  ///< Tick count when the current press began.
		bool longPressTriggered;    ///< True after the long-press callback has been queued.

		/**
		 * @brief Construct a button configuration with its initial idle state.
		 *
		 * @param pin GPIO pin number
		 * @param callback Callback function to execute on button press
		 * @param longPressCallback Callback function to execute on long press
		 * @param longPressDuration Duration in ms to consider a long press
		 * @param initialState Initial state of the button (default: HIGH)
		 * @param initialFallingTick Initial falling tick time (default: 0)
		 * @param initialRisingTick Initial rising tick time (default: 0)
		 */
		Button(uint8_t pin,
		       ButtonCallback callback,
		       ButtonCallback longPressCallback,
		       uint16_t longPressDuration,
		       bool initialState = HIGH,
		       TickType_t initialFallingTick = 0,
		       TickType_t initialRisingTick = 0)
		    : pin(pin), callback(callback), longPressCallback(longPressCallback), longPressDuration(longPressDuration),
		      state(initialState), fallingTick(initialFallingTick), risingTick(initialRisingTick), pressStartTick(0),
		      longPressTriggered(false) {}
	};

	/// Container for all registered buttons
	std::vector<Button> buttons;

	/**
	 * @brief Register a button and its short- and long-press callbacks.
	 *
	 * The pin is configured with INPUT_PULLUP when begin() is called.
	 *
	 * @param pin GPIO pin number for the button.
	 * @param callback Callback function to execute for a short press.
	 * @param longPressCallback Optional callback function for a long press.
	 * @param longPressDuration Hold duration in milliseconds for a long press.
	 */
	void add(uint8_t pin,
	         ButtonCallback callback,
	         ButtonCallback longPressCallback = nullptr,
	         uint16_t longPressDuration = defaultLongPressMs);

	/**
	 * @brief Replace the short-press callback for a registered pin.
	 *
	 * Changes the callback function associated with a button on the specified pin.
	 * If no button is found on that pin, an error message is printed to Serial.
	 * 
	 * @param pin GPIO pin number of the button to update.
	 * @param callback New callback function to execute for a short press.
	 */
	void setCallback(uint8_t pin, ButtonCallback callback);

	/**
	 * @brief Replace the long-press callback for a registered pin.
	 *
	 * Changes the long press callback function associated with a button on the specified pin.
	 * If no button is found on that pin, an error message is printed to Serial.
	 * 
	 * @param pin GPIO pin number of the button to update.
	 * @param longPressCallback New callback function to execute for a long press.
	 */
	void setLongPressCallback(uint8_t pin, ButtonCallback longPressCallback);

	/**
	 * @brief Create the event queue, configure pins, attach interrupts, and start the task.
	 */
	void begin();

  private:
	static const int buttonTaskPollingInterval = 50;  ///< Maximum queue wait in milliseconds.

	/**
	 * @brief Handle a GPIO edge and queue a debounced press event when released.
	 *
	 * This function runs in ISR context and must not invoke user callbacks.
	 *
	 * @param buttonArgument Pointer to the Button object that triggered the interrupt.
	 */
	static void IRAM_ATTR isrWrapper(void* buttonArgument);

	/**
	 * @brief Process queued events and detect long presses in task context.
	 * @param pvParameters Pointer to the ButtonManager instance.
	 */
	static void buttonTask(void* pvParameters);
};

extern ButtonManager buttons;