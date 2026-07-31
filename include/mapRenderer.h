/**
 * @file mapRenderer.h
 * @brief Convert realtime and timetable data into LED frame transactions.
 */

#pragma once

#include <Arduino.h>
#include <FastLED.h>
#include <array>
#include <time.h>
#include <vector>

#if defined(TIMETABLE_SPEED)
#include "timetable.h"
#endif

/**
 * @brief Maps train positions and scheduled updates to LED colors.
 */
class MapRenderer {
  public:
	static constexpr size_t MaxRealtimeBlocks = 2000;  ///< Number of block-color slots used by realtime rendering.
	static constexpr uint32_t SecondsPerDay = 24U * 60U * 60U;  ///< Number of seconds in a 24-hour day.
	static constexpr float FastForwardSpeedScale = 1000.0f;     ///< Default accelerated timetable multiplier.
	static constexpr uint32_t SecondsPerHour = 3600U;           ///< Number of seconds in one hour.

	/**
	 * @brief Render the current realtime map snapshot to an LED frame.
	 * @param epoch Current Unix timestamp used to choose pre/post block states.
	 * @param skipColorId0 If true, scheduled updates with color ID 0 are ignored.
	 */
	void drawRealtimeMap(time_t epoch, bool skipColorId0 = false);

#if defined(TIMETABLE_SPEED)
	/**
	 * @brief Render timetable train positions for a time of day.
	 * @param secondsSinceMidnight Seconds since midnight.
	 * @param routes Non-owning collection of routes to evaluate.
	 */
	void drawTimetableMap(uint32_t secondsSinceMidnight, RouteSpan<const TrainRoute*> routes);

	/**
	 * @brief Render an accelerated, looping timetable simulation.
	 * @param routes Non-owning collection of routes to evaluate.
	 * @param startTime millis() timestamp at which the simulation started.
	 * @param speedMultiplier Simulation speed multiplier.
	 */
	void drawFastForwardTimetable(
	    RouteSpan<const TrainRoute*> routes, uint32_t startTime, float speedMultiplier = FastForwardSpeedScale);
#endif

  private:
	/**
	 * @brief Set a block's color when the new color has sufficient priority.
	 *
	 * Higher color IDs replace lower IDs for the same block. The resolved color
	 * is submitted to the current LED frame.
	 * @param blockColorIds Highest color ID currently assigned to each block.
	 * @param block LED map block to update.
	 * @param colorId Candidate color-table index.
	 * @param colorTable Colors indexed by colorId.
	 */
	void setBlockColorId(std::array<uint8_t, MaxRealtimeBlocks>& blockColorIds,
	                     uint16_t block,
	                     int colorId,
	                     const std::vector<CRGB>& colorTable);

#if defined(TIMETABLE_SPEED)
	bool timetableSetup = false;               ///< True if timetable bounds have been calculated
	uint32_t firstRouteStart = SecondsPerDay;  ///< Time of the first departure in the timetable
	uint32_t lastRouteStart = 0;               ///< Time of the last departure in the timetable
#endif
};

/**
 * @brief Global map renderer instance
 */
extern MapRenderer renderer;