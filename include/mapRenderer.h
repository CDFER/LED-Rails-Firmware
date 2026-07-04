/**
 * @file mapRenderer.h
 * @brief Logic for rendering map data (realtime and timetable) to LEDs
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
 * @brief Handles the logical mapping of train positions to LED patterns
 */
class MapRenderer {
  public:
	static constexpr size_t MaxRealtimeBlocks = 2000;			///< Maximum number of blocks in the system
	static constexpr uint32_t SecondsPerDay = 24U * 60U * 60U;	///< Seconds in a 24-hour day
	static constexpr float FastForwardSpeedScale = 1000.0f;		///< Simulation speed for fast-forward mode
	static constexpr uint32_t SecondsPerHour = 3600U;			///< Seconds in one hour

	/**
	 * @brief Render current realtime map state to LEDs
	 * @param epoch Current Unix timestamp
	 * @param skipColorId0 If true, block ID 0 updates are ignored
	 */
	void drawRealtimeMap(time_t epoch, bool skipColorId0 = false);

#if defined(TIMETABLE_SPEED)
	/**
	 * @brief Render map state for a specific time of day from timetables
	 * @param second Seconds since midnight
	 * @param routes Collection of train routes to render
	 */
	void drawTimetableMap(uint32_t second, RouteSpan<const TrainRoute*> routes);

	/**
	 * @brief Render an accelerated timetable simulation
	 * @param routes Collection of train routes to render
	 * @param start_time Realtime start timestamp
	 * @param xSpeed Simulation speed factor
	 */
	void drawFastForwardTimetable(RouteSpan<const TrainRoute*> routes, uint32_t start_time, float xSpeed = FastForwardSpeedScale);
#endif

  private:
	/**
	 * @brief Set the color ID for a specific block in the rendering buffer
	 */
	void setBlockColorId(
		std::array<uint8_t, MaxRealtimeBlocks>& blockColorIds, uint16_t block, int colorId, const std::vector<CRGB>& colorTable);

#if defined(TIMETABLE_SPEED)
	bool timetableSetup = false;			   ///< True if timetable bounds have been calculated
	uint32_t firstRouteStart = SecondsPerDay;  ///< Time of the first departure in the timetable
	uint32_t lastRouteStart = 0;			   ///< Time of the last departure in the timetable
#endif
};

/**
 * @brief Global map renderer instance
 */
extern MapRenderer renderer;