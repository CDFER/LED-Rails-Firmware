/**
 * @file gpioExpander.h
 * @brief Thread-safe wrapper around the TcMenu IoAbstraction AW9523B driver.
 */

#pragma once

#if defined(GPIO_EXPANDER)

#include <Arduino.h>
#include <IoAbstractionWire.h>

#include "i2cBus.h"

#ifndef GPIO_EXPANDER_ADDRESS
#define GPIO_EXPANDER_ADDRESS 0x5B
#endif

#ifdef I2C_INT_PIN
#define GPIO_EXPANDER_INT_PIN I2C_INT_PIN
#else
#define GPIO_EXPANDER_INT_PIN IO_PIN_NOT_DEFINED
#endif

/**
 * @brief Owns the AW9523B expander and serialises every access through the I2C bus lock.
 *
 * Pin numbers here are the device pins 0-15 (P0_0 = 0 ... P1_7 = 15).
 * Use the ioPins.h functions in application code rather than calling this directly.
 */
class GpioExpander {
  public:
	GpioExpander();

	/** @brief Check the device responds, initialise it and log its ID. Call after i2cBus.begin(). */
	void begin();

	void pinMode(uint8_t pin, uint8_t mode);
	void digitalWrite(uint8_t pin, uint8_t value);

	/** @brief Returns the cached input value. Call refreshInputs() first to sample the hardware. */
	uint8_t digitalRead(uint8_t pin);

	/** @brief Read the input port over I2C and update the cache. Also clears a pending INT. */
	void refreshInputs();

	/** @brief Attach a handler to a pin change. Does nothing if no INT line is configured. */
	void attachInterrupt(uint8_t pin, RawIntHandler handler, uint8_t mode);

	/**
	 * @brief Set the constant-current dimming value of a pin in LED current mode (pin must be configured with AW9523_LED_OUTPUT).
	 * @param dim 0 turns the output off, 255 is the full scale set in begin().
	 */
	void setLedCurrent(uint8_t pin, uint8_t dim);

	bool hasInterrupt() const { return interruptAvailable; }

  private:
	/** @brief Print the key AW9523 registers on a failure. Caller must hold the bus lock. */
	void dumpRegisters(const char* stage);

	AW9523IoAbstraction expander;
	bool interruptAvailable;
};

/// Global GPIO expander instance.
extern GpioExpander gpioExpander;

#endif