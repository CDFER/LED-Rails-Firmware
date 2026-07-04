
/**
 * @file timetable.h
 * @brief Core structures and classes for static timetable-based rendering
 */

#pragma once

#include <Arduino.h>
#include <FastLED.h>

/**
 * @brief Structure representing a timetable entry
 * 
 * Each entry defines when a train enters a specific block along its route.
 */
struct TimetableEntry {
	int16_t offsetSeconds;	///< Offset in seconds from route start time (-32768 to 32767)
	int16_t blockNumber;	///< Block number (0-32767, with -1 reserved for "no block")

	/**
	 * @brief Construct a new TimetableEntry object
	 * 
	 * @param seconds Offset in seconds from route start time
	 * @param block Block number for this entry
	 */
	constexpr TimetableEntry(int16_t seconds, int16_t block) : offsetSeconds(seconds), blockNumber(block) {}
};

/**
 * @brief Non-owning view of a collection of objects (similar to std::span)
 * @tparam T Type of elements in the span
 */
template<typename T> struct RouteSpan {
	const T* _data;	 ///< Pointer to the underlying array
	size_t _size;	 ///< Number of elements in the span

	/**
     * @brief Construct from a pointer and size
     */
	constexpr RouteSpan(const T* d, size_t s) : _data(d), _size(s) {}

	/**
     * @brief Construct from a fixed-size array
     */
	template<size_t N> constexpr RouteSpan(const T (&arr)[N]) : _data(arr), _size(N) {}

	/// Get iterator to start
	const T* begin() const {
		return _data;
	}
	/// Get iterator to end
	const T* end() const {
		return _data + _size;
	}
	/// Access element by index
	const T& operator[](size_t index) const {
		return _data[index];
	}
	/// Get number of elements
	size_t size() const {
		return _size;
	}
	/// Check if span is empty
	bool empty() const {
		return _size == 0;
	}
	/// Get first element
	const T& front() const {
		return _data[0];
	}
	/// Get last element
	const T& back() const {
		return _data[_size - 1];
	}
};

/**
 * @brief Abstract base class for train routes
 * 
 * Defines the interface for train route implementations including timetable
 * entries, color coding, and start times.
 */
class TrainRoute {
  public:
	virtual ~TrainRoute() = default;

	/**
	 * @brief Get the timetable entries for this route
	 * 
	 * @return RouteSpan of timetable entries
	 */
	virtual RouteSpan<TimetableEntry> getEntries() const = 0;

	/**
	 * @brief Get the color used to display this route
	 * 
	 * @return CRGB color value for LED visualization
	 */
	virtual CRGB getColor() const = 0;

	/**
	 * @brief Get the start times for trains on this route
	 * 
	 * @return RouteSpan of start times (seconds since midnight)
	 */
	virtual RouteSpan<uint32_t> getStartTimes() const = 0;

	/**
	 * @brief Get the current block number based on elapsed time
	 * 
	 * @param elapsedSeconds Seconds elapsed since route start time
	 * @return uint16_t Current block number
	 */
	uint16_t getCurrentBlock(int32_t elapsedSeconds) const {
		const auto& entries = getEntries();
		if (entries.empty())
			return 0;

		// Find the last entry whose offsetSeconds <= elapsedSeconds
		for (int i = static_cast<int>(entries.size()) - 1; i >= 0; --i) {
			if (entries[i].offsetSeconds <= elapsedSeconds) {
				return entries[i].blockNumber;
			}
		}

		// If we're before the first entry, return the first entry's block
		return entries[0].blockNumber;
	}

	/**
	 * @brief Calculate approximate memory usage of this route
	 * 
	 * @return uint16_t Total size in bytes
	 */
	uint16_t getSize() const {
		return sizeof(*this) + sizeof(TimetableEntry) * getEntries().size() + sizeof(uint32_t) * getStartTimes().size();
	}
};

/**
 * @brief Class representing an individual train instance
 * 
 * Tracks a specific train's position along its route based on current time.
 */
class TrainInstance {
  private:
	const TrainRoute* route;	// Route this train follows
	uint32_t startTimeSeconds;	// Start time in seconds since midnight

  public:
	/**
	 * @brief Construct a new TrainInstance object
	 * 
	 * @param r Pointer to the route this train follows
	 * @param startTime Start time in seconds since midnight
	 */
	TrainInstance(const TrainRoute* r, uint32_t startTime) : route(r), startTimeSeconds(startTime) {}

	/**
	 * @brief Get the current block number for this train
	 * 
	 * @param currentSecondsSinceMidnight Current time in seconds since midnight
	 * @return uint16_t Current block number
	 */
	uint16_t getCurrentBlock(uint32_t currentSecondsSinceMidnight) const {
		// Calculate elapsed time since train start as signed seconds.
		// This allows timetable offsets to be negative (entries before start)
		int32_t elapsedSeconds;

		if (currentSecondsSinceMidnight >= startTimeSeconds) {
			elapsedSeconds = static_cast<int32_t>(currentSecondsSinceMidnight - startTimeSeconds);
		} else {
			// Handle midnight crossing (24 hours = 86400 seconds)
			elapsedSeconds = static_cast<int32_t>((86400 - startTimeSeconds) + currentSecondsSinceMidnight);
		}

		// Use elapsed seconds to find current block
		return route->getCurrentBlock(elapsedSeconds);
	}

	/**
	 * @brief Check if this train is currently visible
	 * 
	 * A train is visible when the current time is between its first and last
	 * timetable entries (exclusive of endpoints).
	 * 
	 * @param currentSecondsSinceMidnight Current time in seconds since midnight
	 * @return true if train is visible
	 * @return false if train is not visible
	 */
	bool isVisible(uint32_t currentSecondsSinceMidnight) const {
		// Get the first and last entry offsets
		const auto& entries = route->getEntries();
		if (entries.empty())
			return false;

		int32_t firstOffset = entries.front().offsetSeconds;
		int32_t lastOffset = entries.back().offsetSeconds;

		// Compute elapsed seconds as signed value (wrap across midnight)
		int32_t elapsedSeconds;
		if (currentSecondsSinceMidnight >= startTimeSeconds) {
			elapsedSeconds = static_cast<int32_t>(currentSecondsSinceMidnight - startTimeSeconds);
		} else {
			elapsedSeconds = static_cast<int32_t>((86400 - startTimeSeconds) + currentSecondsSinceMidnight);
		}

		// Visible if elapsedSeconds is between firstOffset and lastOffset (exclusive endpoints)
		return (elapsedSeconds > firstOffset && elapsedSeconds < lastOffset);
	}

	/**
	 * @brief Get the color used to display this train
	 * 
	 * @return CRGB color value for LED visualization
	 */
	CRGB getColor() const {
		return route->getColor();
	}

	/**
	 * @brief Get the start time for this train
	 * 
	 * @return uint32_t Start time in seconds since midnight
	 */
	uint32_t getStartTimeSeconds() const {
		return startTimeSeconds;
	}

	/**
	 * @brief Get the route this train follows
	 * 
	 * @return const TrainRoute* Pointer to route object
	 */
	const TrainRoute* getRoute() const {
		return route;
	}
};

/**
 * @brief Print information about loaded routes and memory usage
 * 
 * @param routes Vector of pointers to loaded routes
 */
inline void printTimetableSize(RouteSpan<const TrainRoute*> routes) {
	uint32_t bytes = 0;
	for (const auto& route : routes) {
		bytes += route->getSize();
	}
	bytes += sizeof(TrainRoute*) * routes.size();  // Account for vector of pointers
	Serial.printf("Loaded %d routes, ~%0.2f KiB\n", routes.size(), bytes / 1024.0);
}

#if defined(MEL_V1_0_0)
	#include "timetables/MEL_V1_0_0_Timetable.h"
#elif defined(WLG_V1_0_0)
	#include "timetables/WLG_V1_0_0_Timetable.h"
#elif defined(AKL_V1_0_0)
	#include "timetables/AKL_V1_0_0_Timetable.h"
#elif defined(AKL_V1_1_0)
	#include "timetables/AKL_V1_1_0_Timetable.h"
#endif
