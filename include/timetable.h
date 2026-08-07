/**
 * @file timetable.h
 * @brief Shared route, train, and timetable data types for timetable rendering.
 */

#pragma once

#include <Arduino.h>
#include <FastLED.h>

/**
 * @brief One position change in a train's route timetable.
 *
 * The offset is measured from the train's scheduled start time. A negative
 * offset represents a position before that start time, and block -1 is used
 * as the route-end sentinel by generated timetable data.
 */
struct TimetableEntry {
	int16_t offsetSeconds;  ///< Offset in seconds from the route start time.
	int16_t blockNumber;    ///< LED map block number; -1 marks the end of a route.

	/**
	 * @brief Construct a new TimetableEntry object
	 * 
	 * @param seconds Offset in seconds from route start time
	 * @param block Block number for this entry
	 */
	constexpr TimetableEntry(int16_t seconds, int16_t block) : offsetSeconds(seconds), blockNumber(block) {}
};

/**
 * @brief Non-owning, read-only view of a contiguous collection.
 *
 * The referenced storage must outlive this view. RouteSpan does not allocate,
 * copy, or perform bounds checking.
 * @tparam T Type of elements in the span
 */
template<typename T> struct RouteSpan {
	const T* dataPointer;  ///< Pointer to the first element; may be null when size() is zero.
	size_t elementCount;   ///< Number of elements in the referenced collection.

	/**
	 * @brief Construct a view from a pointer and element count.
	 * @param dataPointer Pointer to the first element.
	 * @param elementCount Number of elements in the referenced collection.
	 */
	constexpr RouteSpan(const T* dataPointer, size_t elementCount)
	    : dataPointer(dataPointer), elementCount(elementCount) {}

	/**
	 * @brief Construct a view from a fixed-size array.
	 * @param routeArray Array whose storage will be referenced without copying.
	 */
	template<size_t ElementCount>
	constexpr RouteSpan(const T (&routeArray)[ElementCount]) : dataPointer(routeArray), elementCount(ElementCount) {}

	/// Return an iterator to the first element.
	const T* begin() const {
		return dataPointer;
	}
	/// Return an iterator one past the last element.
	const T* end() const {
		return dataPointer + elementCount;
	}
	/// Access an element without bounds checking.
	const T& operator[](size_t index) const {
		return dataPointer[index];
	}
	/// Return the number of referenced elements.
	size_t size() const {
		return elementCount;
	}
	/// Return true when no elements are referenced.
	bool empty() const {
		return elementCount == 0;
	}
	/// Return the first element; the view must not be empty.
	const T& front() const {
		return dataPointer[0];
	}
	/// Return the last element; the view must not be empty.
	const T& back() const {
		return dataPointer[elementCount - 1];
	}
};

/**
 * @brief Abstract interface for a train route and its scheduled departures.
 *
 * Concrete generated routes provide static timetable entries, a display color,
 * and one or more departure times. Returned RouteSpan objects refer to data
 * owned by the route implementation and remain valid for the route lifetime.
 */
class TrainRoute {
  public:
	virtual ~TrainRoute() = default;

	/**
	 * @brief Return the ordered timetable entries for this route.
	 * @return Non-owning view of route entries.
	 */
	virtual RouteSpan<TimetableEntry> getEntries() const = 0;

	/**
	 * @brief Return the LED color used to display this route.
	 * @return RGB color value for LED visualization.
	 */
	virtual CRGB getColor() const = 0;

	/**
	 * @brief Return scheduled departure times for this route.
	 * @return Non-owning view of seconds-since-midnight start times.
	 */
	virtual RouteSpan<uint32_t> getStartTimes() const = 0;

	/**
	 * @brief Resolve the route block at an elapsed time.
	 *
	 * The last entry at or before elapsedSeconds is selected. Before the first
	 * entry, the first route block is returned.
	 *
	 * @param elapsedSeconds Seconds elapsed since route start time.
	 * @return Current block number, or zero when the route has no entries.
	 */
	uint16_t getCurrentBlock(int32_t elapsedSeconds) const {
		const auto& entries = getEntries();
		if (entries.empty())
			return 0;

		// Find the last entry whose offsetSeconds <= elapsedSeconds
		for (int entryIndex = static_cast<int>(entries.size()) - 1; entryIndex >= 0; --entryIndex) {
			if (entries[entryIndex].offsetSeconds <= elapsedSeconds) {
				return entries[entryIndex].blockNumber;
			}
		}

		// If we're before the first entry, return the first entry's block
		return entries[0].blockNumber;
	}

	/**
	 * @brief Estimate the memory used by this route's object and static data.
	 * @return Approximate size in bytes.
	 */
	uint16_t getSize() const {
		return sizeof(*this) + sizeof(TimetableEntry) * getEntries().size() + sizeof(uint32_t) * getStartTimes().size();
	}
};

/**
 * @brief Runtime instance of one scheduled train departure.
 *
 * The instance references a route without taking ownership and calculates its
 * position from seconds since midnight.
 */
class TrainInstance {
  private:
	const TrainRoute* route;    ///< Route this train follows; not owned.
	uint32_t startTimeSeconds;  ///< Scheduled start time in seconds since midnight.

  public:
	/**
	 * @brief Construct a train instance for one route departure.
	 * @param route Pointer to the route this train follows; must remain valid.
	 * @param startTime Start time in seconds since midnight.
	 */
	TrainInstance(const TrainRoute* route, uint32_t startTime) : route(route), startTimeSeconds(startTime) {}

	/**
	 * @brief Return this train's current route block.
	 * @param currentSecondsSinceMidnight Current time in seconds since midnight.
	 * @return Current block number from the referenced route.
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
	 * @brief Check whether the train is between its route endpoints.
	 *
	 * Visibility is true strictly between the first and last timetable offsets.
	 *
	 * @param currentSecondsSinceMidnight Current time in seconds since midnight.
	 * @return true when the train should be rendered, otherwise false.
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
	 * @brief Return the route color used to display this train.
	 * @return RGB color value for LED visualization.
	 */
	CRGB getColor() const {
		return route->getColor();
	}

	/**
	 * @brief Return this train's scheduled start time.
	 * @return Start time in seconds since midnight.
	 */
	uint32_t getStartTimeSeconds() const {
		return startTimeSeconds;
	}

	/**
	 * @brief Return the referenced route without transferring ownership.
	 * @return Pointer to the route object.
	 */
	const TrainRoute* getRoute() const {
		return route;
	}
};

/**
 * @brief Print the number of loaded routes and an approximate memory total.
 * @param routes Non-owning collection of route pointers.
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
// Generated route data for the Melbourne V1 board.
#include "timetables/MEL_V1_0_0_Timetable.h"
#elif defined(WLG_V1_0_0)
// Generated route data for the Wellington V1 board.
#include "timetables/WLG_V1_0_0_Timetable.h"
#elif defined(AKL_V1_0_0)
// Generated route data for the Auckland V1 board.
#include "timetables/AKL_V1_0_0_Timetable.h"
#elif defined(AKL_V1_1_0)
// Generated route data for the Auckland V1.1 board.
#include "timetables/AKL_V1_1_0_Timetable.h"
#endif
