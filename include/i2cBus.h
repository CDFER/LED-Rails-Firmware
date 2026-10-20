/**
 * @file i2cBus.h
 * @brief Shared I2C bus with a mutex so the LTR303 and AW9523B never overlap.
 */

#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

/**
 * @brief Owns the I2C bus used by the light sensor and GPIO expander.
 *
 * Call begin() once from setup() before any device access. Callers must hold an
 * I2cBusLock for every transaction.
 */
class I2cBus {
  public:
	I2cBus();

	/** @brief Initialise the Wire peripheral on SDA_PIN/SCL_PIN. */
	void begin();

	/** @brief Scan the I2C bus for devices and print their addresses. */
	void scan();

  private:
	friend class I2cBusLock;
	SemaphoreHandle_t mutex;  ///< Non-recursive mutex guarding all bus transactions.
};

/**
 * @brief RAII guard that holds the I2C bus mutex for its lifetime.
 *
 * The mutex is not recursive, so do not take a second lock while one is held.
 */
class I2cBusLock {
  public:
	explicit I2cBusLock(I2cBus& bus) : mutex(bus.mutex) {
		xSemaphoreTake(mutex, portMAX_DELAY);
	}

	~I2cBusLock() {
		xSemaphoreGive(mutex);
	}

	I2cBusLock(const I2cBusLock&) = delete;
	I2cBusLock& operator=(const I2cBusLock&) = delete;

  private:
	SemaphoreHandle_t mutex;
};

/// Global I2C bus shared by all I2C devices.
extern I2cBus i2cBus;
