#include "dailySchedule.h"
#include "brightness.h"
#include "ledStorageSync.h"

DailyScheduleManager dailyScheduleManager;

void DailyScheduleManager::begin() {
	std::lock_guard<std::mutex> lock(scheduleMutex);
	Preferences localPreferences;
	xSemaphoreTake(fastLEDPreferencesMutex, portMAX_DELAY);
	localPreferences.begin("dailySchedule", true);
	for (uint8_t entryIndex = 0; entryIndex < DAILY_SCHEDULE_ENTRY_COUNT; entryIndex++) {
		const String prefix = "entry" + String(entryIndex);
		entries[entryIndex].enabled = localPreferences.getBool((prefix + "Enabled").c_str(), false);
		entries[entryIndex].minuteOfDay = localPreferences.getUShort((prefix + "Minute").c_str(), 0);
		entries[entryIndex].turnOn = localPreferences.getBool((prefix + "TurnOn").c_str(), true);
		const uint8_t modeValue = localPreferences.getUChar((prefix + "Mode").c_str(), REALTIME_MODE);
		entries[entryIndex].mode = modeValue < NUM_MODES ? static_cast<Mode>(modeValue) : REALTIME_MODE;
	}
	localPreferences.end();
	xSemaphoreGive(fastLEDPreferencesMutex);

	if (!isValidSchedule(entries)) {
		Serial.println("Invalid daily schedule in Preferences; using an empty schedule");
		entries = {};
	}
}

bool DailyScheduleManager::requestSchedule(
    const std::array<DailyScheduleEntry, DAILY_SCHEDULE_ENTRY_COUNT>& requestedEntries) {
	if (!isValidSchedule(requestedEntries)) {
		return false;
	}

	std::lock_guard<std::mutex> lock(scheduleMutex);
	entries = requestedEntries;
	saveLocked();
	return true;
}

void DailyScheduleManager::getSchedule(
    std::array<DailyScheduleEntry, DAILY_SCHEDULE_ENTRY_COUNT>& requestedEntries) const {
	std::lock_guard<std::mutex> lock(scheduleMutex);
	requestedEntries = entries;
}

void DailyScheduleManager::update(time_t epoch) {
	// Unix time near zero means NTP has not supplied a reliable wall clock yet.
	if (epoch < 100000) {
		return;
	}

	struct tm localTime;
	if (localtime_r(&epoch, &localTime) == nullptr) {
		return;
	}

	const time_t currentMinute = epoch / 60;
	std::array<DailyScheduleEntry, DAILY_SCHEDULE_ENTRY_COUNT> currentEntries;
	{
		std::lock_guard<std::mutex> lock(scheduleMutex);
		if (currentMinute == lastProcessedMinute) {
			return;
		}
		lastProcessedMinute = currentMinute;
		currentEntries = entries;
	}

	const uint16_t minuteOfDay = localTime.tm_hour * 60 + localTime.tm_min;
	for (const DailyScheduleEntry& entry : currentEntries) {
		if (!entry.enabled || entry.minuteOfDay != minuteOfDay) {
			continue;
		}

		if (entry.turnOn) {
			// Set the mode before power is enabled so the first visible frame uses it.
			modeManager.requestMode(entry.mode);
		}
		brightnessManager.setPower(entry.turnOn);
		Serial.printf("Daily schedule: turning %s at %02u:%02u\n",
		              entry.turnOn ? "on" : "off",
		              entry.minuteOfDay / 60,
		              entry.minuteOfDay % 60);
	}
}

bool DailyScheduleManager::isValidSchedule(
    const std::array<DailyScheduleEntry, DAILY_SCHEDULE_ENTRY_COUNT>& requestedEntries) const {
	bool hasPreviousEnabledEntry = false;
	uint16_t previousMinute = 0;
	for (const DailyScheduleEntry& entry : requestedEntries) {
		if (entry.minuteOfDay >= 24 * 60 || entry.mode < REALTIME_MODE || entry.mode >= NUM_MODES) {
			return false;
		}
		if (!entry.enabled) {
			continue;
		}
		if (hasPreviousEnabledEntry && entry.minuteOfDay <= previousMinute) {
			return false;
		}
		previousMinute = entry.minuteOfDay;
		hasPreviousEnabledEntry = true;
	}
	return true;
}

void DailyScheduleManager::saveLocked() {
	Preferences localPreferences;
	xSemaphoreTake(fastLEDPreferencesMutex, portMAX_DELAY);
	localPreferences.begin("dailySchedule", false);
	for (uint8_t entryIndex = 0; entryIndex < DAILY_SCHEDULE_ENTRY_COUNT; entryIndex++) {
		const String prefix = "entry" + String(entryIndex);
		const DailyScheduleEntry& entry = entries[entryIndex];
		localPreferences.putBool((prefix + "Enabled").c_str(), entry.enabled);
		localPreferences.putUShort((prefix + "Minute").c_str(), entry.minuteOfDay);
		localPreferences.putBool((prefix + "TurnOn").c_str(), entry.turnOn);
		localPreferences.putUChar((prefix + "Mode").c_str(), static_cast<uint8_t>(entry.mode));
	}
	localPreferences.end();
	xSemaphoreGive(fastLEDPreferencesMutex);
}
