/**
 * @file statusLeds.h
 * @brief Background task for managing diagnostic status LEDs
 */

#pragma once

#include <Arduino.h>

/**
 * @brief Command enum for the status LEDs
 */
enum statusLedCommand {
	LED_OFF = 0,			   ///< LED is fully off
	LED_ON_GREEN = 1,		   ///< LED is solid green
	LED_ON_RED = 2,			   ///< LED is solid red
	LED_BLINK_GREEN_SLOW = 3,  ///< LED blinks green at 1Hz
	LED_BLINK_GREEN_FAST = 4,  ///< LED blinks green at 5Hz
	LED_BLINK_RED_SLOW = 5,	   ///< LED blinks red at 1Hz
	LED_BLINK_RED_FAST = 6	   ///< LED blinks red at 5Hz
};

/**
 * @brief State structure for a single status LED
 */
struct statusLed {
	uint8_t pin;			   ///< GPIO pin number (for dual-color or standard)
	statusLedCommand command;  ///< Desired display command
	bool currentState;		   ///< Current physical state (on/off)
	unsigned long lastToggle;  ///< Timestamp of the last toggle
};

/**
 * @brief Manager class for handling charlieplexed status LEDs
 * 
 * This class runs a background task to handle blinking and state management
 * of bidirectional LEDs (charlieplexed or standard dual-color).
 */
class StatusLEDManager {
  public:
	/**
	 * @brief Initialize the status LED manager and start its background task
	 */
	void begin();

	/**
	 * @brief Set the command state for two status LEDs simultaneously
	 * @param pin1 First LED GPIO pin
	 * @param cmd1 First LED command
	 * @param pin2 Second LED GPIO pin
	 * @param cmd2 Second LED command
	 */
	void setState(uint8_t pin1, statusLedCommand cmd1, uint8_t pin2, statusLedCommand cmd2);

	/**
	 * @brief Set the command state for a single status LED
	 * @param pin LED GPIO pin
	 * @param command LED command
	 */
	void setState(uint8_t pin, statusLedCommand command);

  private:
	TaskHandle_t taskHandle = nullptr;	///< Handle for the background task

	/**
	 * @brief Hardware-level control for charlieplexed/bidirectional LEDs
	 */
	static void setCharlieplexedLED(uint8_t pin, statusLedCommand state);

	/**
	 * @brief Main task loop for status LED timing and updates
	 */
	static void taskMethod(void* pvParameters);
};

/**
 * @brief Global status LED manager instance
 */
extern StatusLEDManager statusLEDs;
