#pragma once

#include "brightness.h"
#include <FastLED.h>
#include <vector>

class LedManager {
  public:
	/**
	 * @brief Initialize all LED strips and start the dithering task
	 */
	void begin();

	/**
	 * @brief Set the color of a specific track block
	 * 
	 * @param block Block number to set
	 * @param color RGB color value
	 */
	void setBlockColorRGB(uint16_t block, CRGB color);

	/**
	 * @brief Turn off all LEDs across all strips
	 */
	void clear();

	/**
	 * @brief Set all LEDs to the same color
	 * 
	 * @param color RGB color value
	 */
	void setAllLedsColor(CRGB color);

	/**
	 * @brief Suspend the FastLED dithering task
	 */
	void suspendDithering();

	/**
	 * @brief Resume the FastLED dithering task
	 */
	void resumeDithering();
};

extern LedManager mapLEDs;
