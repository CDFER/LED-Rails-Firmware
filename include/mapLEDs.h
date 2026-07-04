/**
 * @file mapLEDs.h
 * @brief Thread-safe LED management and transaction system
 */

#pragma once

#include "brightness.h"
#include <FastLED.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <vector>

/**
 * @brief Manages LED strips with thread-safe transactional updates
 * 
 * This class uses a background task to process LED updates, allowing
 * multiple threads to submit frame transactions without flicker.
 */
class LedManager {
  public:
	/**
	 * @brief Initialize all LED strips and start the dithering task
	 */
	void begin();

	/**
	 * @brief Start building a new frame transaction
	 */
	void beginFrame();

	/**
	 * @brief Submit the built frame to the renderer
	 */
	void show();

	/**
	 * @brief Adds a set color command to the current frame transaction
	 * 
	 * @param block Block number to set
	 * @param color RGB color value
	 */
	void setBlockColorRGB(uint16_t block, CRGB color);

	/**
	 * @brief Adds a clear all command to the current frame transaction
	 */
	void clear();

	/**
	 * @brief Adds a set all color command to the current frame transaction
	 * 
	 * @param color RGB color value
	 */
	void setAllLedsColor(CRGB color);

	/**
	 * @brief Types of LED commands that can be batched in a frame
	 */
	enum CommandType {
		CMD_SET_BLOCK,	   ///< Set color of a specific block
		CMD_CLEAR_ALL,	   ///< Clear all LEDs
		CMD_SET_ALL_COLOR  ///< Set all LEDs to a single color
	};

	/**
	 * @brief Individual LED command structure
	 */
	struct LedCommand {
		CommandType type;  ///< Type of command
		uint16_t block;	   ///< Target block (if applicable)
		CRGB color;		   ///< Target color (if applicable)
	};

  private:
	QueueHandle_t frameQueue;						  ///< FreeRTOS queue for passing frames to the task
	std::vector<LedCommand>* currentFrame = nullptr;  ///< Buffer for the frame currently being built

	/**
	 * @brief Process queued frames and update LED strips
	 */
	void processFrames();

	/**
	 * @brief Task wrapper for FreeRTOS
	 * @param pvParameters Pointer to the LedManager instance
	 */
	static void task_wrapper(void* pvParameters);

	/**
	 * @brief Main task loop for LED rendering
	 */
	void task();
};

/**
 * @brief Global LED manager instance
 */
extern LedManager mapLEDs;
