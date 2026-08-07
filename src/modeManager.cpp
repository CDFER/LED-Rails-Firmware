#include "modeManager.h"
#include "brightness.h"
#include "mapRenderer.h"
#include "network.h"

#if defined(TIMETABLE_SPEED)
#include "timetable.h"
#endif

ModeManager modeManager;

namespace {
constexpr int NoModeRequest = -1;
constexpr int NextModeRequest = -2;
}

const char* ModeManager::getModeName(Mode mode) {
	switch (mode) {
		case REALTIME_MODE: return "Live";
#if defined(TIMETABLE_SPEED)
		case ONE_X_TIMETABLE_MODE: return "Timetable";
		case HIGH_SPEED_TIMETABLE_MODE: return "Fast Forward";
#endif
#if defined(OUT_OF_SERVICE_TRAINS)
		case HIDE_OUT_OF_SERVICE_TRAINS_MODE: return "In Service Live";
#endif
		default: return "Unknown";
	}
}

void ModeManager::begin() {
	modeStartTime = millis();
}

void ModeManager::nextMode() {
	setMode(Mode((currentMode + 1) % NUM_MODES));
	brightnessManager.setPower(true);  // Ensure brightnessManager is on when changing modes
	Serial.printf("Mode #%d: %s\n", currentMode, getModeName(currentMode));
	Serial.flush();
}

void ModeManager::resetTimer() {
	modeStartTime = millis();
	lastMapDrawTime = 0;
}

void ModeManager::setMode(Mode targetMode) {
	currentMode = targetMode;
	reportedMode.store(targetMode);
	resetTimer();
}

void ModeManager::requestMode(Mode targetMode) {
	if (targetMode >= REALTIME_MODE && targetMode < NUM_MODES) {
		pendingModeRequest.store(targetMode);
	}
}

void ModeManager::requestNextMode() {
	pendingModeRequest.store(NextModeRequest);
}

void ModeManager::requestTimerReset() {
	timerResetRequested.store(true);
}

void ModeManager::processPendingModeRequest() {
	const int requestedMode = pendingModeRequest.exchange(NoModeRequest);
	if (requestedMode == NextModeRequest) {
		nextMode();
	} else if (requestedMode >= REALTIME_MODE && requestedMode < NUM_MODES) {
		setMode(static_cast<Mode>(requestedMode));
	}

	if (timerResetRequested.exchange(false)) {
		resetTimer();
	}
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