#include "mapRenderer.h"
#include <sys/time.h>
#include "mapLeds.h"
#include "network.h"

MapRenderer renderer;

void MapRenderer::setBlockColorId(
    std::array<uint8_t, MaxRealtimeBlocks>& blockColorIds,
    uint16_t block,
    int colorId,
    const std::vector<CRGB>& colorTable) {
	if (colorId < blockColorIds[block]) {
		return;  // Do not update if the new color is lower priority
	}

	blockColorIds[block] = colorId;  // Update the color ID for the block

	// Get the actual color from the color table, defaulting to black if out of range
	static const CRGB fallbackBlack = CRGB::Black;
	CRGB color = (colorId >= 0 && colorId < static_cast<int>(colorTable.size())) ? colorTable[colorId] : fallbackBlack;

	mapLeds.setBlockColorRGB(block, color);
}

void MapRenderer::drawRealtimeMap(time_t epoch, bool skipColorId0) {
	auto mapData = network.getMapData();
	if (!mapData)
		return;

	std::array<uint8_t, MaxRealtimeBlocks> blockColorIds{};

	struct timeval timeValue;
	gettimeofday(&timeValue, NULL);
	uint16_t millisecondsInSecond = timeValue.tv_usec / 1000;

	mapLeds.beginFrame();
	mapLeds.clear();

	// Draw the map based on the current LED update schedule
	for (const auto& update : mapData->ledUpdateSchedule) {
		if (skipColorId0 && update.colorId == 0)
			continue;

		bool isPostTransition =
		    (epoch > update.timestamp) || (epoch == update.timestamp && millisecondsInSecond >= update.msOffset);
		setBlockColorId(
		    blockColorIds, isPostTransition ? update.postBlock : update.preBlock, update.colorId, mapData->colorTable);
	}
	mapLeds.show();
}

#if defined(TIMETABLE_SPEED)
void MapRenderer::drawTimetableMap(uint32_t secondsSinceMidnight, RouteSpan<const TrainRoute*> routes) {
	mapLeds.beginFrame();
	mapLeds.clear();

	for (size_t routeIndex = 0; routeIndex < routes.size(); routeIndex++) {
		const TrainRoute* route = routes[routeIndex];
		for (uint32_t startTime : route->getStartTimes()) {
			TrainInstance train(route, startTime);
			if (train.isVisible(secondsSinceMidnight)) {
				uint16_t block = train.getCurrentBlock(secondsSinceMidnight);
				mapLeds.setBlockColorRGB(block, route->getColor());
			}
		}
	}
	mapLeds.show();
}

void MapRenderer::drawFastForwardTimetable(
    RouteSpan<const TrainRoute*> routes, uint32_t startTime, float speedMultiplier) {
	if (!timetableSetup) {
		for (const auto& route : routes) {
			for (uint32_t startTime : route->getStartTimes()) {
				if (startTime < firstRouteStart) {
					firstRouteStart = startTime;
				} else if (startTime > lastRouteStart) {
					lastRouteStart = startTime;
				}
			}
		}
		timetableSetup = true;
	}

	uint32_t simulatedSeconds = ((millis() - startTime) / 1000.0f * speedMultiplier);
	simulatedSeconds = simulatedSeconds % ((lastRouteStart - firstRouteStart) + SecondsPerHour);  // Wrap around
	simulatedSeconds += firstRouteStart;  // Offset to start from the first train
	drawTimetableMap(simulatedSeconds, routes);
}
#endif