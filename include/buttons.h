/**
 * @file buttons.h
 * @brief Button input with debouncing, long-press detection, and callback dispatch.
 *
 * Buttons may be native GPIO or AW9523B expander pins (see ioPins.h). A single
 * task owns all scanning. Interrupts only wake that task early, so polling still
 * works when an expander INT line is not connected.
 */

#pragma once

#include <Arduino.h>
#include <functional>
#include <mutex>
#include <vector>

/// Default duration in milliseconds before a held button becomes a long press.
const uint16_t defaultLongPressMs = 500;

/**
 * @brief Manages multiple active-low buttons with debouncing and callbacks.
 *
 * Callbacks run from the button task. Callback objects are copied under a mutex
 * and invoked outside it, so a callback can safely replace callbacks.
 * Buttons must be added before begin(); add() after begin() is rejected.
 */
class ButtonManager {
  public:
	/// Callback invoked from button-task context after a press is recognized.
	using ButtonCallback = std::function<void()>;

	/**
	 * @brief Register a button and its short- and long-press callbacks.
	 *
	 * Must be called before begin().
	 *
	 * @param pin Native GPIO or expander pin for the button.
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
	 * If no button is found on that pin, an error message is printed to Serial.
	 *
	 * @param pin GPIO pin number of the button to update.
	 * @param callback New callback function to execute for a short press.
	 */
	void setCallback(uint8_t pin, ButtonCallback callback);

	/**
	 * @brief Replace the long-press callback for a registered pin.
	 *
	 * If no button is found on that pin, an error message is printed to Serial.
	 *
	 * @param pin GPIO pin number of the button to update.
	 * @param longPressCallback New callback function to execute for a long press.
	 */
	void setLongPressCallback(uint8_t pin, ButtonCallback longPressCallback);

	/**
	 * @brief Configure pins, start the scan task, and attach interrupts.
	 */
	void begin();

  private:
	/**
	 * @brief Configuration and runtime state for one registered button.
	 *
	 * Callbacks are guarded by ButtonManager::callbackMutex. The scan fields are
	 * only used by the button task.
	 */
	struct Button {
		uint8_t pin;                       ///< Native or expander pin for this button.
		ButtonCallback callback;           ///< Short-press callback.
		ButtonCallback longPressCallback;  ///< Long-press callback; may be null.
		uint16_t longPressDuration;        ///< Hold duration in ms to consider a long press.

		bool rawPressed = false;          ///< Last sampled electrical state; true is pressed.
		TickType_t rawChangeTick = 0;     ///< Tick when rawPressed last changed.
		bool pressed = false;             ///< Debounced state.
		TickType_t pressStartTick = 0;    ///< Tick when the debounced press began.
		bool longPressTriggered = false;  ///< True once the long-press callback has run for this press.
	};

	std::vector<Button> buttons;      ///< Registered buttons; not modified after begin().
	std::mutex callbackMutex;         ///< Guards callback fields against concurrent replacement.
	bool started = false;             ///< True once begin() has run.
	bool hasExpanderButtons = false;  ///< True when any button is on the GPIO expander.

	/** @brief Find a registered button by pin, or nullptr. */
	Button* findButton(uint8_t pin);

	/** @brief Copy a callback under callbackMutex so it can be invoked outside the lock. */
	ButtonCallback snapshot(const ButtonCallback& callback);

	/**
	 * @brief Sample every button, apply debouncing, and fire callbacks.
	 * @param now Current tick count.
	 */
	void scan(TickType_t now);

	/**
	 * @brief Scan loop. Woken by button interrupts or after a polling interval.
	 * @param pvParameters Pointer to the ButtonManager instance.
	 */
	static void buttonTask(void* pvParameters);
};

extern ButtonManager buttons;
