/**
 * @file brightness.h
 * @brief LED brightness management system
 */

#pragma once

#include <Arduino.h>
#include <FastLED.h>
#include <Preferences.h>

#if defined(LIGHT_SENSOR)
	#include <LTR303.h>

extern LTR303 lightSensor;

/**
 * @brief Lux range and corresponding brightness level
 */
struct BrightnessBucket {
	float luxMax;		  ///< Maximum lux for this bucket
	float brightnessMax;  ///< Brightness at maximum lux (0-1)
};

#endif

/**
 * @brief Task notification bits for thread-safe brightness control
 */
#define EVT_INCREASE (1 << 0)
#define EVT_DECREASE (1 << 1)
#define EVT_UPDATE_BRIGHTNESS (1 << 2)

/**
 * @brief Manages LED brightness using either an ambient light sensor or manual controls
 */
class BrightnessManager {
  public:
	/**
	 * @brief Construct a new Brightness Manager object
	 */
	BrightnessManager();

	/**
	 * @brief Initialize the brightness manager
	 */
	void begin();

	/**
	 * @brief Increase manual brightness level
	 */
	void increase();

	/**
	 * @brief Decrease manual brightness level
	 */
	void decrease();

	/**
	 * @brief Toggle between automatic and manual brightness modes
	 */
	void toggle();

	/**
	 * @brief Set the power state of the LEDs
	 * @param on True to turn on, false to turn off
	 */
	void setPower(bool on);

	/**
	 * @brief Set the LED brightness based on current mode and sensor data
	 */
	void setBrightness();

	/**
	 * @brief Check if power is currently on
	 * @return true if power is on
	 */
	bool isOn();

	/**
	 * @brief Save brightness settings to non-volatile storage
	 */
	void save();

	/**
	 * @brief Load brightness settings from non-volatile storage
	 */
	void load();

	/**
	 * @brief Periodically update brightness (called from main loop)
	 */
	void update();

  private:
	TickType_t lastUpdate = xTaskGetTickCount();  ///< Last time the brightness was updated
	float brightness;							  ///< Current brightness level (0.0-1.0)
	bool powerOn;								  ///< Current power state
	bool savePending = false;					  ///< Flag to indicate settings need saving
	bool buttonPressed = false;					  ///< Flag to track button state for debouncing
#if defined(LIGHT_SENSOR)
	float ambientLux;							 ///< Smoothed ambient light level in lux
	int bucketIndex;							 ///< Current brightness bucket index
	BrightnessBucket buckets[4];				 ///< Configuration for light-to-brightness mapping
	const int numBuckets = 4;					 ///< Number of brightness buckets
	const float luxUpSmoothingFactor = 0.05f;	 ///< Smoothing factor for increasing light
	const float luxDownSmoothingFactor = 0.01f;	 ///< Smoothing factor for decreasing light

	/**
	 * @brief Apply gamma correction to a brightness value
	 * @param _brightness Input linear brightness
	 * @return uint8_t Gamma-corrected 8-bit brightness value
	 */
	uint8_t gammaCorrectedBrightness(float _brightness);

	/**
	 * @brief Adjust all brightness buckets by a delta
	 * @param delta Amount to change brightness by
	 */
	void adjustBuckets(float delta);

	/**
	 * @brief Print bucket configuration to Serial for debugging
	 */
	void printBuckets();

	/**
	 * @brief Map a float value from one range to another
	 */
	float mapFloat(float value, float inMin, float inMax, float outMin, float outMax);

	/**
	 * @brief Get the maximum lux for a specific bucket
	 */
	float getLuxForBucket(int index);

	/**
	 * @brief Get the maximum brightness for a specific bucket
	 */
	float getBrightnessForBucket(int index);

	/**
	 * @brief Determine which bucket current lux falls into
	 */
	int getAmbientBucketIndex(float lux);

	/**
	 * @brief Calculate target brightness for current lux and bucket
	 */
	float calculateBrightnessForAmbient(float lux, int index);
#endif
};

/**
 * @brief Global brightness manager instance
 */
extern BrightnessManager brightness;
