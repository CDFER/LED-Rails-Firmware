#include "mapRenderer.h"
#include "mapLEDs.h"
#include "network.h"
#include <sys/time.h>

MapRenderer renderer;

void MapRenderer::setBlockColorId(
	std::array<uint8_t, MaxRealtimeBlocks>& blockColorIds, uint16_t block, int colorId, const std::vector<CRGB>& colorTable) {
	if (colorId < blockColorIds[block]) {
		return;	 // Do not update if the new color is lower priority
	}

	blockColorIds[block] = colorId;	 // Update the color ID for the block

	// Get the actual color from the color table, defaulting to black if out of range
	static const CRGB fallbackBlack = CRGB::Black;
	CRGB color = (colorId >= 0 && colorId < static_cast<int>(colorTable.size())) ? colorTable[colorId] : fallbackBlack;

	mapLEDs.setBlockColorRGB(block, color);
}

void MapRenderer::drawRealtimeMap(time_t epoch, bool skipColorId0) {
	auto mapData = network.getMapData();
	if (!mapData)
		return;

	std::array<uint8_t, MaxRealtimeBlocks> blockColorIds{};

	struct timeval tv;
	gettimeofday(&tv, NULL);
	uint16_t msInSecond = tv.tv_usec / 1000;

	mapLEDs.beginFrame();
	mapLEDs.clear();

	// Draw the map based on the current LED update schedule
	for (const auto& update : mapData->ledUpdateSchedule) {
		if (skipColorId0 && update.colorId == 0)
			continue;

		bool isPost = (epoch > update.timestamp) || (epoch == update.timestamp && msInSecond >= update.msOffset);
		setBlockColorId(blockColorIds, isPost ? update.postBlock : update.preBlock, update.colorId, mapData->colorTable);
	}
	mapLEDs.show();
}

#if defined(TIMETABLE_SPEED)
void MapRenderer::drawTimetableMap(uint32_t second, RouteSpan<const TrainRoute*> routes) {
	mapLEDs.beginFrame();
	mapLEDs.clear();

	for (size_t routeIndex = 0; routeIndex < routes.size(); routeIndex++) {
		const TrainRoute* route = routes[routeIndex];
		for (uint32_t startTime : route->getStartTimes()) {
			TrainInstance train(route, startTime);
			if (train.isVisible(second)) {
				uint16_t block = train.getCurrentBlock(second);
				mapLEDs.setBlockColorRGB(block, route->getColor());
			}
		}
	}
	mapLEDs.show();
}

void MapRenderer::drawFastForwardTimetable(RouteSpan<const TrainRoute*> routes, uint32_t start_time, float xSpeed) {
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

	uint32_t seconds = ((millis() - start_time) / 1000.0f * xSpeed);
	seconds = seconds % ((lastRouteStart - firstRouteStart) + SecondsPerHour);	// Wrap around
	seconds += firstRouteStart;													// Offset to start from the first train
	drawTimetableMap(seconds, routes);
}
#endif