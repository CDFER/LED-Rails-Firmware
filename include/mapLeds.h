/**
 * @file mapLeds.h
 * @brief Queued LED frame transactions, fading, and power management.
 */

#pragma once

#include <FastLED.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <vector>
#include "brightness.h"

/**
 * @brief Manages configured LED strips from a dedicated rendering task.
 *
 * Callers build a frame with beginFrame(), append commands, and submit it
 * with show(). The completed frame is queued for the LED task, which owns and
 * processes it after submission.
 */
class LedManager {
  public:
	/**
	 * @brief Initialize configured strips, power pins, brightnessManager, and the LED task.
	 */
	void begin();

	/**
	 * @brief Discard any unfinished frame and start a new frame transaction.
	 */
	void beginFrame();

	/**
	 * @brief Queue the current frame for rendering without blocking.
	 *
	 * If the queue is full, the frame is dropped and its memory is released.
	 */
	void show();

	/**
	 * @brief Add a command to set one map block's target color.
	 *
	 * The command is stored in the frame created by beginFrame() and does not
	 * affect the LED buffers until show() submits that frame.
	 * @param block Map block number to set; block zero is ignored by the backend.
	 * @param color Linear RGB color before backend gamma correction.
	 */
	void setBlockColorRGB(uint16_t block, CRGB color);

	/**
	 * @brief Add a command that sets every target pixel to black.
	 *
	 * The command is stored in the current frame and is applied asynchronously
	 * by the LED task after show().
	 */
	void clear();

	/**
	 * @brief Add a command that sets every target pixel to one color.
	 *
	 * The command is stored in the current frame and is applied asynchronously
	 * by the LED task after show().
	 * @param color RGB color value.
	 */
	void setAllLedsColor(CRGB color);

	/**
	 * @brief Commands that can be batched in one frame transaction.
	 */
	enum CommandType {
		CMD_SET_BLOCK,     ///< Set the target color of one map block.
		CMD_CLEAR_ALL,     ///< Set all target pixels to black.
		CMD_SET_ALL_COLOR  ///< Set all target pixels to one color.
	};

	/**
	 * @brief One operation stored in a frame transaction.
	 */
	struct LedCommand {
		CommandType type;  ///< Operation to perform.
		uint16_t block;    ///< Target map block when type is CMD_SET_BLOCK.
		CRGB color;        ///< Target color when the command carries a color.
	};

  private:
	QueueHandle_t frameQueue;                         ///< FreeRTOS queue for passing frames to the task.
	std::vector<LedCommand>* currentFrame = nullptr;  ///< Buffer for the frame currently being built.

	/**
	 * @brief Consume queued frames and update the target pixel buffers.
	 */
	void processFrames();

	/**
	 * @brief Move displayed pixels toward targets or black during power-off.
	 */
	void updateFade();

	/**
	 * @brief Run the LED state machine, fade pixels, and call FastLED.show().
	 */
	void task();
};

/**
 * @brief Global LED manager instance.
 */
extern LedManager mapLeds;
