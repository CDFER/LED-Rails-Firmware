#include "modeManager.h"
#include "brightness.h"
#include "mapRenderer.h"
#include "network.h"

#if defined(TIMETABLE_SPEED)
#include "timetable.h"
#endif

ModeManager modeManager;

namespace {
const char* modeName(Mode mode) {
	switch (mode) {
		case REALTIME_MODE: return "Live";
#if defined(TIMETABLE_SPEED)
		case ONE_X_TIMETABLE_MODE: return "Timetable";
		case HIGH_SPEED_TIMETABLE_MODE: return "Fast Forward";
#endif
#if defined(OUT_OF_SERVICE_TRAINS)
		case HIDE_OUT_OF_SERVICE_TRAINS_MODE: return "Live without not-in-service";
#endif
		default: return "Unknown";
	}
}
}

void ModeManager::begin() {
	modeStartTime = millis();
}

void ModeManager::nextMode() {
	currentMode = Mode((currentMode + 1) % NUM_MODES);
	modeStartTime = millis();
	lastMapDrawTime = 0;               // Force immediate redraw
	brightnessManager.setPower(true);  // Ensure brightnessManager is on when changing modes
	Serial.printf("Mode #%d: %s\n", currentMode, modeName(currentMode));
	Serial.flush();
}

void ModeManager::resetTimer() {
	modeStartTime = millis();
	lastMapDrawTime = 0;
}

void ModeManager::setMode(Mode targetMode) {
	currentMode = targetMode;
	resetTimer();
}

NetworkMode ModeManager::getTargetNetworkMode() const {
	NetworkMode targetNetMode = NetworkMode::REALTIME;
#if defined(TIMETABLE_SPEED)
	if (currentMode == ONE_X_TIMETABLE_MODE) {
		targetNetMode = NetworkMode::TIME_ONLY;
	} else if (currentMode == HIGH_SPEED_TIMETABLE_MODE) {
		targetNetMode = NetworkMode::OFF;
	}
#endif
#if defined(OUT_OF_SERVICE_TRAINS)
	if (currentMode == HIDE_OUT_OF_SERVICE_TRAINS_MODE) {
		targetNetMode = NetworkMode::REALTIME;
	}
#endif
	return targetNetMode;
}

bool ModeManager::shouldDrawFrame(unsigned long currentTime) {
	if (lastMapDrawTime + MapRedrawIntervalMilliseconds < currentTime) {
		lastMapDrawTime = currentTime;
		return true;
	}
	return false;
}

void ModeManager::drawCurrentMode(time_t epoch) {
	switch (currentMode) {
		case REALTIME_MODE: renderer.drawRealtimeMap(epoch); break;

#if defined(TIMETABLE_SPEED)
		case ONE_X_TIMETABLE_MODE:
			{
				struct tm timeinfo;
				localtime_r(&epoch, &timeinfo);
				uint32_t secondsSinceMidnight = timeinfo.tm_hour * 3600 + timeinfo.tm_min * 60 + timeinfo.tm_sec;
				renderer.drawTimetableMap(secondsSinceMidnight, getAllRoutes());
			}
			break;

		case HIGH_SPEED_TIMETABLE_MODE:
			{
				renderer.drawFastForwardTimetable(getAllRoutes(), modeStartTime, MapRenderer::FastForwardSpeedScale);
			}
			break;
#endif

#if defined(OUT_OF_SERVICE_TRAINS)
		case HIDE_OUT_OF_SERVICE_TRAINS_MODE: renderer.drawRealtimeMap(epoch, true); break;
#endif

		default:
			Serial.println("Unknown mode, reverting to REALTIME");
			currentMode = REALTIME_MODE;
			break;
	}
}