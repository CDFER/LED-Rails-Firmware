/**
 * @file statusLeds.h
 * @brief Asynchronous state and blink control for diagnostic status LEDs.
 *
 * Boards either drive a single GPIO charlieplexed LED (legacy) or a dual-colour
 * LED with separate red and green pins (SYD, which may use expander pins).
 */

#pragma once

#include <Arduino.h>
#include <memory>

#include "ioPins.h"

// Callers use WIFI_LED_PIN and SERVER_LED_PIN as LED IDs. Dual-colour boards use
// the red pin token as the ID, which keeps IDs unique and avoids the 255 sentinel.
#if defined(WIFI_LED_R_PIN)
#define WIFI_LED_PIN WIFI_LED_R_PIN
#elif !defined(WIFI_LED_PIN)
#define WIFI_LED_PIN 255
#endif

#if defined(SERVER_LED_R_PIN)
#define SERVER_LED_PIN SERVER_LED_R_PIN
#elif !defined(SERVER_LED_PIN)
#define SERVER_LED_PIN 255
#endif

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
 * @brief Hardware driver for one diagnostic LED. Only the status task calls apply().
 */
class StatusLedOutput {
  public:
	virtual ~StatusLedOutput() = default;

	/**
	 * @brief Drive the LED to a solid state.
	 * @param command LED_OFF, LED_ON_GREEN, or LED_ON_RED. Other values are ignored.
	 */
	virtual void apply(StatusLedCommand command) = 0;
};

/**
 * @brief Legacy single-pin LED that is charlieplexed with another LED.
 */
class CharlieplexedLedOutput : public StatusLedOutput {
  public:
	/**
	 * @param pin GPIO pin to drive.
	 */
	explicit CharlieplexedLedOutput(uint8_t pin);

	void apply(StatusLedCommand command) override;

  private:
	uint8_t pin;  ///< GPIO pin to drive.
};

/**
 * @brief Dual-colour LED with independent red and green pins.
 */
class DualColourLedOutput : public StatusLedOutput {
  public:
	/**
	 * @param redPin Red LED pin (native GPIO or expander token).
	 * @param greenPin Green LED pin (native GPIO or expander token).
	 * @param redDim LED current dim value used when the red LED is on.
	 * @param greenDim LED current dim value used when the green LED is on.
	 */
	DualColourLedOutput(uint8_t redPin, uint8_t greenPin, uint8_t redDim, uint8_t greenDim);

	void apply(StatusLedCommand command) override;

  private:
	uint8_t redPin;    ///< Red LED pin.
	uint8_t greenPin;  ///< Green LED pin.
	uint8_t redDim;    ///< Red LED current dim value.
	uint8_t greenDim;  ///< Green LED current dim value.
};

/**
 * @brief Command and runtime state for one diagnostic LED.
 */
struct StatusLed {
	uint8_t id = 255;                         ///< Caller-facing LED ID; 255 means unconfigured.
	StatusLedCommand command = LED_OFF;       ///< Desired display command.
	bool currentState = false;                ///< Current on/off phase used by blinking.
	unsigned long lastToggle = 0;             ///< millis() timestamp of the last blink transition.
	std::unique_ptr<StatusLedOutput> output;  ///< Driver; null when the LED is not configured.
};

/**
 * @brief Manages diagnostic LEDs from a background task.
 *
 * State requests are packed into a task queue, so setState() returns
 * without directly changing GPIOs from the calling task. All GPIO access
 * happens in the status task after begin().
 */
class StatusLedManager {
  public:
	/**
	 * @brief Create the LED drivers and start the background task.
	 */
	void begin();

	/**
	 * @brief Queue commands for two status LEDs.
	 * @param firstPin First LED ID.
	 * @param firstCommand First LED command.
	 * @param secondPin Second LED ID.
	 * @param secondCommand Second LED command.
	 */
	void setState(uint8_t firstPin, StatusLedCommand firstCommand, uint8_t secondPin, StatusLedCommand secondCommand);

	/**
	 * @brief Queue a command for one status LED.
	 * @param pin LED ID.
	 * @param command LED command.
	 */
	void setState(uint8_t pin, StatusLedCommand command);

  private:
	static constexpr size_t LedCount = 2;

	TaskHandle_t taskHandle = nullptr;     ///< Handle for the background task.
	QueueHandle_t commandQueue = nullptr;  ///< Queue of packed LED state requests.
	StatusLed leds[LedCount];              ///< LED state; written in begin() and then only by task().

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
