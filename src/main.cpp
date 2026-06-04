#include <Arduino.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <time.h>
#include <vector>

#if defined(FACTORY_TEST)
	#include "factory.h"
#endif

#if defined(TIMETABLE_SPEED)
	#include "timetable.h"
#endif

#include "brightness.h"
#include "buttons.h"
#include "mapLEDs.h"
#include "network.h"
#include "statusLeds.h"

// Array of server URLs for failover
String serverURLs[] = {
	String("http://api.keastudios.co.nz/") + CITY_CODE + "-ltm/" + BACKEND_VERSION + ".json",
#if defined(BETA_BUILD)
	String("http://192.168.86.31:3000/") + CITY_CODE + "-ltm/" + BACKEND_VERSION + ".json",	 // For testing with a local server
#endif
	String("http://keastudios.co.nz/") + CITY_CODE + "-ltm/" + BACKEND_VERSION + ".json",
	String("http://dirksonline.net/") + CITY_CODE + "-ltm/" + BACKEND_VERSION + ".json",
};
const int numServers = sizeof(serverURLs) / sizeof(serverURLs[0]);
int currentServerIndex = 0;
int failedFetchCount = 0;

unsigned long lastMapDrawTime = 0;	// Tracks the last time the map was drawn (ms since boot)
time_t nextFetchTime = 0;			// Tracks when the next update should occur
uint32_t modeStartTime = 0;			// Tracks when the current mode started (for fast forward mode timing)
uint8_t fetchOffset = 0;			// Random time ms to fetch (reduces server load)
uint8_t updateInterval = 30;		// Default update interval in seconds

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

Mode trainMapMode = REALTIME_MODE;

#if defined(TIMETABLE_SPEED)
const auto& routes = getAllRoutes();
#endif

CRGB black = CRGB::Black;
std::vector<CRGB> colorTable;

// --- Data structure for scheduled LED updates ---
struct LedUpdate {
	uint16_t preBlock;
	uint16_t postBlock;
	int colorId;
	time_t timestamp;	// Timestamp for when the update should occur
	uint16_t msOffset;	// Millisecond offset for precise timing within the second
};

std::vector<LedUpdate> ledUpdateSchedule;

const char* getLocalTime(time_t epoch) {
	struct tm timeinfo;
	static char buffer[64];

	// Convert epoch to local time
	if (!localtime_r(&epoch, &timeinfo)) {
		return "No time available";
	}
	if (strftime(buffer, sizeof(buffer), "%H:%M:%S", &timeinfo)) {
		return buffer;
	}
	return "Format error";
}

String getSystemInfo() {
	FlashMode_t mode = (FlashMode_t)ESP.getFlashChipMode();
	String flashMode;

	// Convert flash mode to human-readable string
	switch (mode) {
		case FM_QIO: flashMode = "Quad I/O (QIO)"; break;
		case FM_QOUT: flashMode = "Quad Output (QOUT)"; break;
		case FM_DIO: flashMode = "Dual I/O (DIO)"; break;
		case FM_DOUT: flashMode = "Dual Output (DOUT)"; break;
		case FM_FAST_READ: flashMode = "Fast Read"; break;
		case FM_SLOW_READ: flashMode = "Slow Read"; break;
		default: flashMode = "Unknown"; break;
	}

	String info = "\n";
	info += String(ARDUINO_BOARD) + "\n";
	info += String(FIRMWARE) + " V" + FIRMWARE_VERSION + "\n";
	info += "Built: " + String(__DATE__) + " " + __TIME__ + "\n";
	info += String(ESP.getChipModel()) + " Rev:" + ESP.getChipRevision() + "\n";
	info += String(ESP.getChipCores()) + " Core @ " + ESP.getCpuFreqMHz() + "MHz\n";
	info += String(ESP.getFlashChipSize() / (1024 * 1024)) + "MiB Flash @ " + (ESP.getFlashChipSpeed() / (1000 * 1000))
			+ "MHz in " + flashMode + " Mode\n";
	info += "RAM Heap: " + String(ESP.getHeapSize() / 1024) + "kiB\n";
	info += "IDF SDK: " + String(ESP.getSdkVersion()) + "\n";

	return info;
}

String downloadJSON() {
	HTTPClient http;
	const String& url = serverURLs[currentServerIndex];

	http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
	http.begin(url);

	int httpCode = http.GET();
	if (httpCode == HTTP_CODE_OK) {
		String payload = http.getString();
		http.end();
		if (payload.length() > 0) {
			failedFetchCount = 0;
			return payload;
		}
		Serial.printf("Fetch from %s: empty payload\n", url.c_str());
	} else {
		Serial.printf("Fetch from %s error: %i\n", url.c_str(), httpCode);
		http.end();
	}

	if (++failedFetchCount > 3) {
		currentServerIndex = (currentServerIndex + 1) % numServers;
	}
	return "";
}

void setBlockColorId(uint8_t* blockColorIds, uint16_t block, int colorId) {
	if (colorId < blockColorIds[block]) {
		return;	 // Do not update if the new color is lower priority
	}

	blockColorIds[block] = colorId;	 // Update the color ID for the block

	// Get the actual color from the color table, defaulting to black if out of range
	CRGB color = (colorId >= 0 && colorId < static_cast<int>(colorTable.size())) ? colorTable[colorId] : black;

	mapLEDs.setBlockColorRGB(block, color);
}

void drawRealtimeMap(time_t epoch, bool skipColorId0 = false) {
	uint8_t blockColorIds[2000] = { 0 };  // Initialize all elements to 0

	mapLEDs.suspendDithering();

	struct timeval tv;
	gettimeofday(&tv, NULL);
	uint16_t msInSecond = tv.tv_usec / 1000;

	mapLEDs.clear();

	// Draw the map based on the current LED update schedule
	for (const auto& update : ledUpdateSchedule) {
		if (skipColorId0 && update.colorId == 0)
			continue;

		bool isPost = (epoch > update.timestamp) || (epoch == update.timestamp && msInSecond >= update.msOffset);
		setBlockColorId(blockColorIds, isPost ? update.postBlock : update.preBlock, update.colorId);
	}

	mapLEDs.resumeDithering();
}

#if defined(TIMETABLE_SPEED)
void drawTimetableMap(uint32_t second, RouteSpan<const TrainRoute*> routes) {
	mapLEDs.suspendDithering();
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

	mapLEDs.resumeDithering();
}

bool timetableSetup = false;
uint32_t first_route_start = 24 * 60 * 60;	// Start with max seconds in a day
uint32_t last_route_start = 0;

void drawFastForwardTimetable(RouteSpan<const TrainRoute*> routes, uint32_t start_time, float xSpeed = 1000.0f) {
	// Calculate the current simulated time in seconds since midnight

	const uint32_t night_time = 1 * 60 * 60;  // Shrink night time to 1 hour

	if (!timetableSetup) {
		for (const auto& route : routes) {
			for (uint32_t startTime : route->getStartTimes()) {
				if (startTime < first_route_start) {
					first_route_start = startTime;
				} else if (startTime > last_route_start) {
					last_route_start = startTime;
				}
			}
		}
		timetableSetup = true;
	}

	uint32_t seconds = ((millis() - start_time) / 1000.0f * xSpeed);
	seconds = seconds % ((last_route_start - first_route_start) + night_time);	// Wrap around
	seconds += first_route_start;												// Offset to start from the first train
	drawTimetableMap(seconds, routes);
}
#endif

time_t parseLEDMap(const String& downloadedJson) {
	JsonDocument doc;
	if (DeserializationError error = deserializeJson(doc, downloadedJson)) {
		Serial.printf("JSON parse error: %s\n", error.c_str());
		return 0;
	}

	time_t baseTimestamp = doc["timestamp"] | 0;
	updateInterval = doc["update"] | updateInterval;

	if (baseTimestamp + updateInterval <= nextFetchTime) {
		Serial.println("Fetched the same data twice");
		return baseTimestamp;
	}
	nextFetchTime = baseTimestamp + updateInterval;

	const char* version = doc["version"] | "";
	if (strcmp(BACKEND_VERSION, version) != 0) {
		Serial.printf("Backend version mismatch: expected %s, got %s\n", BACKEND_VERSION, version);
	}

	JsonObject colors = doc["colors"];
	colorTable.clear();
	colorTable.reserve(colors.size());
	for (JsonPair kv : colors) {
		JsonArray rgb = kv.value();
		colorTable.push_back(CRGB(rgb[0] | 0, rgb[1] | 0, rgb[2] | 0));
	}

	JsonArray updates = doc["updates"];
	ledUpdateSchedule.clear();
	ledUpdateSchedule.reserve(updates.size());
	for (JsonObject update : updates) {
		int offset = update["t"];

		time_t timestamp = 0;
		uint16_t msOffset = 0;
		if (offset > 0) {
			timestamp = baseTimestamp + offset;
			// Random Seed based on block for consistency to spread out updates within the second
			randomSeed(update["b"][0]);
			msOffset = random(0, 1000);
		}

		ledUpdateSchedule.push_back({ update["b"][0], update["b"][1], update["c"], timestamp, msOffset });
	}

	return baseTimestamp;
}

void onBrightnessDown() {
	brightness.decrease();
}

void onBrightnessUp() {
	brightness.increase();
}

void onPower() {
	brightness.toggle();
	if (brightness.isOn()) {
		modeStartTime = millis();  // Reset start time for fast forward mode
	} else {
		statusLEDs.setState(WIFI_LED_PIN, LED_OFF, SERVER_LED_PIN, LED_OFF);
	}
	lastMapDrawTime = 0;  // Force redraw
	Serial.printf("Power %s\n", brightness.isOn() ? "ON" : "OFF");
}

void onMode() {
	// Cycle through modes
	trainMapMode = Mode((trainMapMode + 1) % NUM_MODES);
	modeStartTime = millis();	// Reset start time for fast forward mode
	lastMapDrawTime = 0;		// Force immediate redraw
	brightness.setPower(true);	// Ensure brightness is on when changing modes
	Serial.printf("Mode #%d\n", trainMapMode);
}

void realtimeMode(time_t epoch, bool wiFiConnected, bool hideOutOfServiceTrains = false) {
	if (wiFiConnected) {
		// --- Fetch new data periodically ---
		if (epoch > nextFetchTime && millis() % 1000 > fetchOffset) {
			if (epoch > nextFetchTime + updateInterval && brightness.isOn()) {
				statusLEDs.setState(WIFI_LED_PIN, LED_ON_GREEN, SERVER_LED_PIN, LED_BLINK_GREEN_FAST);
			}

			time_t timeOffset = 0;
			String downloadedJson = downloadJSON();
			if (downloadedJson.length() > 0) {
				if (brightness.isOn()) {
					statusLEDs.setState(WIFI_LED_PIN, LED_ON_GREEN, SERVER_LED_PIN, LED_ON_GREEN);
				}
				timeOffset = epoch - parseLEDMap(downloadedJson);
			} else {
				if (failedFetchCount > 3 + numServers) {
					Serial.println("All servers failed to provide data.");
					if (brightness.isOn()) {
						statusLEDs.setState(SERVER_LED_PIN, LED_ON_RED);
					}
				}
			}

			nextFetchTime = constrain(nextFetchTime, epoch + 2, epoch + updateInterval);

			Serial.printf("%s fetchDelay:%2is size:%1.1fkiB MCU:%2.0f°C WiFi:%2idBm\n",
						  getLocalTime(epoch),
						  timeOffset,
						  downloadedJson.length() / 1024.0,
						  temperatureRead(),
						  WiFi.RSSI());
			Serial.flush();
		}

		// --- Push updates to the LED strips only if changes were made ---
		if (lastMapDrawTime + 50 < millis()) {
			drawRealtimeMap(epoch, hideOutOfServiceTrains);	 // Draw the map with the current updates
			lastMapDrawTime = millis();
		}

	} else {
		if (brightness.isOn()) {
			if (millis() > 60 * 1000) {
				statusLEDs.setState(WIFI_LED_PIN, LED_ON_RED, SERVER_LED_PIN, LED_OFF);
			}
		}
	}
}

void setup() {
	// Hardware Serial
	// Serial0.begin(115200);

	// USB Serial
	Serial.begin();
	Serial.setDebugOutput(true);

	mapLEDs.begin();

	// --- Setup Buttons ---
	buttons.add(BRIGHTNESS_DOWN_BUTTON, onBrightnessDown);
	buttons.add(BRIGHTNESS_UP_BUTTON, onBrightnessUp);
#if defined(MODE_BUTTON)
	buttons.add(POWER_BUTTON, onPower);
	buttons.add(MODE_BUTTON, onMode);
#else
	buttons.add(POWER_BUTTON, onPower, onMode);
#endif
	buttons.begin();

	Serial.println(getSystemInfo());

#if defined(FACTORY_TEST)
	#if defined(TIMETABLE_SPEED)
	if (factoryTestMode()) {
		trainMapMode = HIGH_SPEED_TIMETABLE_MODE;  // Default to fast forward mode after factory test
		modeStartTime = millis();				   // Reset start time for fast forward mode
	}
	#endif
	buttons.setCallback(POWER_BUTTON, onPower);
#endif

	statusLEDs.begin();

#if defined(BETA_BUILD)
	fetchOffset = 0;  // No need for random offset in beta builds
#else
	fetchOffset = random(0, 999);  // Random delay between 0 and 999 ms to reduce server load
#endif

	// --- WiFi Setup ---
	if (network.begin()) {
		Serial.println("WiFi credentials found...");
		statusLEDs.setState(WIFI_LED_PIN, LED_BLINK_GREEN_FAST);
	} else {
		Serial.println("No WiFi credentials found...");
		statusLEDs.setState(WIFI_LED_PIN, LED_ON_RED);
	}

#if defined(TIMETABLE_SPEED) && defined(BETA_BUILD)
	printTimetableSize(routes);
#endif
}

void loop() {
	time_t epoch = time(nullptr);  // Get current time
	bool wiFiConnected = (WiFi.status() == WL_CONNECTED);

	switch (trainMapMode) {
		// Run the realtime mode using the LED-Rails backend server (default)
		case REALTIME_MODE: realtimeMode(epoch, wiFiConnected); break;

#if defined(TIMETABLE_SPEED)
		// Run the timetable mode at 1x speed (uses wiFi for time sync if available)
		case ONE_X_TIMETABLE_MODE:
			if (brightness.isOn()) {
				if (millis() > lastMapDrawTime) {
					struct tm timeinfo;
					localtime_r(&epoch, &timeinfo);
					uint32_t secondsSinceMidnight = timeinfo.tm_hour * 3600 + timeinfo.tm_min * 60 + timeinfo.tm_sec;
					drawTimetableMap(secondsSinceMidnight, routes);
					lastMapDrawTime = millis();
				}

				if (wiFiConnected) {
					statusLEDs.setState(WIFI_LED_PIN, LED_ON_GREEN, SERVER_LED_PIN, LED_OFF);
				} else if (millis() > 60 * 1000) {
					statusLEDs.setState(WIFI_LED_PIN, LED_ON_RED, SERVER_LED_PIN, LED_OFF);
				}
			}
			break;

		// Run the timetable mode at TIMETABLE_SPEED times normal speed (no wiFi required)
		case HIGH_SPEED_TIMETABLE_MODE:
			if (brightness.isOn()) {
				drawFastForwardTimetable(routes, modeStartTime, TIMETABLE_SPEED);
				statusLEDs.setState(WIFI_LED_PIN, LED_OFF, SERVER_LED_PIN, LED_OFF);
				nextFetchTime = 0;
			}
			break;
#endif

#if defined(OUT_OF_SERVICE_TRAINS)
			// Hide out-of-service trains
		case HIDE_OUT_OF_SERVICE_TRAINS_MODE: realtimeMode(epoch, wiFiConnected, true); break;
#endif

		default:
			Serial.println("Unknown mode, reverting to REALTIME");
			trainMapMode = REALTIME_MODE;
			break;
	}

	vTaskDelay(pdMS_TO_TICKS(30));
}