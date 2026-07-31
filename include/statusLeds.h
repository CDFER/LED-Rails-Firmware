/**
 * @file statusLeds.h
 * @brief Asynchronous state and blink control for diagnostic status LEDs.
 */

#pragma once

#include <Arduino.h>

/**
 * @brief Display commands supported by the diagnostic LEDs.
 */
enum StatusLedCommand {
	LED_OFF = 0,               ///< LED is disabled.
	LED_ON_GREEN = 1,          ///< LED is continuously green.
	LED_ON_RED = 2,            ///< LED is continuously red.
	LED_BLINK_GREEN_SLOW = 3,  ///< Green blink with a 1 Hz full cycle.
	LED_BLINK_GREEN_FAST = 4,  ///< Green blink with a 5 Hz full cycle.
	LED_BLINK_RED_SLOW = 5,    ///< Red blink with a 1 Hz full cycle.
	LED_BLINK_RED_FAST = 6     ///< Red blink with a 5 Hz full cycle.
};

/**
 * @brief Command and runtime state for one diagnostic LED.
 */
struct StatusLed {
	uint8_t pin;               ///< GPIO pin number; 255 means the pin is unconfigured.
	StatusLedCommand command;  ///< Desired display command.
	bool currentState;         ///< Current on/off phase used by blinking.
	unsigned long lastToggle;  ///< millis() timestamp of the last blink transition.
};

/**
 * @brief Manages charlieplexed diagnostic LEDs from a background task.
 *
 * State requests are packed into a task notification, so setState() returns
 * without directly changing GPIOs from the calling task.
 */
class StatusLedManager {
  public:
	/**
	 * @brief Start the background task that applies LED commands and blink timing.
	 */
	void begin();

	/**
	 * @brief Queue commands for two status LEDs.
	 * @param firstPin First LED GPIO pin.
	 * @param firstCommand First LED command.
	 * @param secondPin Second LED GPIO pin.
	 * @param secondCommand Second LED command.
	 */
	void setState(uint8_t firstPin, StatusLedCommand firstCommand, uint8_t secondPin, StatusLedCommand secondCommand);

	/**
	 * @brief Queue a command for one status LED.
	 * @param pin LED GPIO pin.
	 * @param command LED command.
	 */
	void setState(uint8_t pin, StatusLedCommand command);

  private:
	TaskHandle_t taskHandle = nullptr;  ///< Handle for the background task.

	/**
	 * @brief Apply an immediate GPIO state for one charlieplexed LED.
	 * @param pin GPIO pin to drive.
	 * @param command Solid LED command to apply; blinking is handled by task().
	 */
	static void setCharlieplexedLed(uint8_t pin, StatusLedCommand command);

	/**
	 * @brief Consume queued commands and update solid or blinking LED states.
	 * @param pvParameters Pointer to the StatusLedManager instance.
	 */
	static void task(void* pvParameters);
};

/**
 * @brief Global status LED manager instance
 */
extern StatusLedManager statusLeds;
