/**
 * @file dailySchedule.h
 * @brief Thread-safe daily on/off schedule for the LED map.
 */

#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <array>
#include <mutex>
#include <time.h>
#include "modeManager.h"

/** @brief Maximum number of time-based actions stored for one day. */
constexpr uint8_t DAILY_SCHEDULE_ENTRY_COUNT = 8;

/**
 * @brief One action in the map's daily schedule.
 *
 * Times use local time and are stored as minutes after midnight. A turn-on
 * action may also select the display mode that should start with the map.
 */
struct DailyScheduleEntry {
	bool enabled = false;
	uint16_t minuteOfDay = 0;
	bool turnOn = true;
	Mode mode = REALTIME_MODE;
};

/**
 * @brief Owns the persisted daily schedule and applies due actions.
 *
 * HTTP callbacks may replace the schedule from the network task. The main
 * loop calls update(), which is the only place that executes scheduled
 * actions, so FastLED and mode timing remain on their owning tasks.
 */
class DailyScheduleManager {
  public:
	/** @brief Load the saved daily schedule from NVS. */
	void begin();

	/**
	 * @brief Replace the complete schedule after validating every entry.
	 * @param entries New schedule entries, including disabled slots.
	 * @return False when the entry count, times, modes, or ordering is invalid.
	 */
	bool requestSchedule(const std::array<DailyScheduleEntry, DAILY_SCHEDULE_ENTRY_COUNT>& entries);

	/** @brief Copy the current schedule into a caller-owned snapshot. */
	void getSchedule(std::array<DailyScheduleEntry, DAILY_SCHEDULE_ENTRY_COUNT>& entries) const;

	/**
	 * @brief Apply actions matching the current local minute.
	 *
	 * Each wall-clock minute is evaluated once. This prevents a schedule entry
	 * from repeatedly toggling the map during the many loop passes in that
	 * minute, while still applying an entry immediately after a reboot.
	 */
	void update(time_t epoch);

  private:
	std::array<DailyScheduleEntry, DAILY_SCHEDULE_ENTRY_COUNT> entries{};
	mutable std::mutex scheduleMutex;
	time_t lastProcessedMinute = -1;

	/** @brief Validate times, ordering, and mode values before persistence. */
	bool isValidSchedule(const std::array<DailyScheduleEntry, DAILY_SCHEDULE_ENTRY_COUNT>& entries) const;

	/** @brief Persist a validated schedule while holding the schedule mutex. */
	void saveLocked();
};

/** @brief Global daily schedule manager instance. */
extern DailyScheduleManager dailyScheduleManager;
