/**
 * @file modeManager.h
 * @brief Application mode management and rendering dispatch logic
 */

#pragma once

#include "network.h"
#include <Arduino.h>
#include <time.h>

enum Mode {
	REALTIME_MODE,
#if defined(TIMETABLE_SPEED)
	ONE_X_TIMETABLE_MODE,
	HIGH_SPEED_TIMETABLE_MODE,
#endif
#if defined(OUT_OF_SERVICE_TRAINS)
	HIDE_OUT_OF_SERVICE_TRAINS_MODE,
#endif
	NUM_MODES  // Sentinel value for the number of modes
};

class ModeManager {
  public:
	void begin();
	void nextMode();
	void resetTimer();
	void setMode(Mode m);

	NetworkMode getTargetNetworkMode() const;
	bool shouldDrawFrame(unsigned long now);
	void drawCurrentMode(time_t epoch);

	Mode getCurrentMode() const {
		return currentMode;
	}

  private:
	Mode currentMode = REALTIME_MODE;
	unsigned long lastMapDrawTime = 0;
	uint32_t modeStartTime = 0;
	static constexpr unsigned long MapRedrawIntervalMs = 25;
};

extern ModeManager modeManager;