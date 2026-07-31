/**
 * @file modeManager.h
 * @brief Application mode selection and mode-specific rendering dispatch.
 */

#pragma once

#include <Arduino.h>
#include <time.h>
#include "network.h"

/**
 * @brief Display modes available in the current build.
 *
 * Optional modes are included only when their corresponding build flags are
 * defined. NUM_MODES is a sentinel and is not a display mode.
 */
enum Mode {
	REALTIME_MODE,  ///< Render the live map feed.
#if defined(TIMETABLE_SPEED)
	ONE_X_TIMETABLE_MODE,       ///< Render the timetable at real-time speed.
	HIGH_SPEED_TIMETABLE_MODE,  ///< Render an accelerated timetable simulation.
#endif
#if defined(OUT_OF_SERVICE_TRAINS)
	HIDE_OUT_OF_SERVICE_TRAINS_MODE,  ///< Render live data without out-of-service trains.
#endif
	NUM_MODES  ///< Sentinel containing the number of available display modes.
};

/**
 * @brief Owns the current display mode and dispatches its renderer.
 */
class ModeManager {
  public:
	/** @brief Initialize mode timing. */
	void begin();
	/** @brief Advance to the next compiled-in mode and reset its timers. */
	void nextMode();
	/** @brief Restart the timer used by the current mode. */
	void resetTimer();
	/**
	 * @brief Select a mode directly and reset its timers.
	 * @param targetMode Mode to select.
	 */
	void setMode(Mode targetMode);

	/**
	 * @brief Return the network behavior required by the current display mode.
	 */
	NetworkMode getTargetNetworkMode() const;
	/**
	 * @brief Check whether enough time has passed to render another frame.
	 * @param currentTime Current millisecond timestamp from millis().
	 */
	bool shouldDrawFrame(unsigned long currentTime);
	/**
	 * @brief Render the current mode for the supplied Unix timestamp.
	 * @param epoch Current Unix timestamp used by realtime and timetable views.
	 */
	void drawCurrentMode(time_t epoch);

	/** @brief Return the currently selected display mode. */
	Mode getCurrentMode() const {
		return currentMode;
	}

  private:
	Mode currentMode = REALTIME_MODE;   ///< Mode selected for the next render.
	unsigned long lastMapDrawTime = 0;  ///< Timestamp of the last accepted render request.
	uint32_t modeStartTime = 0;         ///< millis() timestamp when the current mode started.
	static constexpr unsigned long MapRedrawIntervalMilliseconds = 25;  ///< Minimum interval between map redraws.
};

/** @brief Global display mode manager instance. */
extern ModeManager modeManager;