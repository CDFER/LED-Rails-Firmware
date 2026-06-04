#pragma once

#include <Arduino.h>

/**
 * @brief Command enum for the status LEDs
 */
enum statusLedCommand {
	LED_OFF = 0,
	LED_ON_GREEN = 1,
	LED_ON_RED = 2,
	LED_BLINK_GREEN_SLOW = 3,  // 1Hz
	LED_BLINK_GREEN_FAST = 4,  // 5Hz
	LED_BLINK_RED_SLOW = 5,	   // 1Hz
	LED_BLINK_RED_FAST = 6	   // 5Hz
};

/**
 * @brief State structure for a single status LED
 */
struct statusLed {
	uint8_t pin;
	statusLedCommand command;
	bool currentState;
	unsigned long lastToggle;
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
	 */
	void setState(uint8_t pin1, statusLedCommand cmd1, uint8_t pin2, statusLedCommand cmd2);

	/**
	 * @brief Set the command state for a single status LED
	 */
	void setState(uint8_t pin, statusLedCommand command);

  private:
	TaskHandle_t taskHandle = nullptr;

	static void setCharlieplexedLED(uint8_t pin, statusLedCommand state);
	static void taskMethod(void* pvParameters);
};

extern StatusLEDManager statusLEDs;
