#include <Arduino.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <time.h>
#include <vector>

#include "WiFiConfig.h"

#if defined(FACTORY_TEST)
	#include "factory.h"
#endif

#if defined(TIMETABLE_MODE)
	#include "timetable.h"
#endif

#if defined(LIGHT_SENSOR)
	#include "autoBrightness.h"
#else
	#include "manualBrightness.h"
#endif

#include "buttons.h"

Preferences preferences;
BrightnessManager brightness;
ButtonManager buttons;

// Array of server URLs for failover
String serverURLs[] = {
	String("http://keastudios.co.nz/") + CITY_CODE + "-ltm/" + BACKEND_VERSION + ".json",
	String("http://dirksonline.net/") + CITY_CODE + "-ltm/" + BACKEND_VERSION + ".json",
	// String("http://192.168.86.31:3000/") + CITY_CODE + "-ltm/" + BACKEND_VERSION + ".json",	 // For local server for testing
};
const int numServers = sizeof(serverURLs) / sizeof(serverURLs[0]);
int currentServerIndex = 0;

const char* ntpServers[] = { "nz.pool.ntp.org", "pool.msltime.measurement.govt.nz", "pool.ntp.org" };
const char* time_zone = "NZST-12NZDT,M9.5.0,M4.1.0/3";

time_t lastMapDrawTime = 0;	  // Tracks the last time the map was drawn
time_t nextFetchTime = 0;	  // Tracks when the next update should occur
uint32_t modeStartTime = 0;	  // Tracks when the current mode started (for fast forward mode timing)
uint8_t fetchOffset = 0;	  // Random time ms to fetch (reduces server load)
uint8_t updateInterval = 30;  // Default update interval in seconds

#if defined(TIMETABLE_MODE)
enum Mode { REALTIME, ONE_X_TIMETABLE, FAST_FORWARD_TIMETABLE };
Mode mode = REALTIME;
const auto& routes = getAllRoutes();
#else
enum Mode { REALTIME };
Mode mode = REALTIME;
#endif

// Pins and pixel counts defined in the board file (./boards/)
CRGB leds1[LED_1_PIXELS];
#if defined(LED_2_PIN)
CRGB leds2[LED_2_PIXELS];
#endif

CRGB black = CRGB::Black;
std::vector<CRGB> colorTable;

// --- Data structure for scheduled LED updates ---
struct LedUpdate {
	uint16_t preBlock;
	uint16_t postBlock;
	int colorId;
	time_t timestamp;  // Timestamp for when the update should occur
};

std::vector<LedUpdate> ledUpdateSchedule;

enum statusLedCommand {
	LED_OFF = 0,
	LED_ON_GREEN = 1,
	LED_ON_RED = 2,
	LED_BLINK_GREEN_SLOW = 3,  // 1Hz
	LED_BLINK_GREEN_FAST = 4,  // 5Hz
	LED_BLINK_RED_SLOW = 5,	   // 1Hz
	LED_BLINK_RED_FAST = 6	   // 5Hz
};

typedef struct {
	uint8_t pin;
	statusLedCommand command;
	bool currentState;
	unsigned long lastToggle;
} statusLed;

TaskHandle_t statusLedTaskHandle;
TaskHandle_t fastLEDDitheringTaskHandle;

void fastLEDDitheringTask(void* pvParameters) {
	const TickType_t delay = pdMS_TO_TICKS(20);	 // 50fps = 20ms interval
	while (true) {
		FastLED.show();
		vTaskDelay(delay);
	}
}

const char* getLocalTime(time_t epoch) {
	struct tm timeinfo;
	static char buffer[64];
	struct timeval tv;

	// Convert epoch to local time
	if (!localtime_r(&epoch, &timeinfo)) {
		return "No time available";
	}
	gettimeofday(&tv, nullptr);
	int ms = tv.tv_usec / 1000;
	if (strftime(buffer, sizeof(buffer), "%H:%M:%S", &timeinfo)) {
		snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), ".%03d", ms);
		return buffer;
	}
	return "Format error";
}

void timeavailable(struct timeval* t) {
	Serial.println("NTP Synced");
}

void setCharlieplexedLED(uint8_t pin, statusLedCommand state) {
	switch (state) {
		case LED_ON_GREEN:
			pinMode(pin, OUTPUT);
			digitalWrite(pin, HIGH);
			break;

		case LED_ON_RED:
			pinMode(pin, OUTPUT);
			digitalWrite(pin, LOW);
			break;

		case LED_OFF:
			// Set as input (High Resistance) to disable output driver
			pinMode(pin, INPUT);
			break;
	}
}

void statusLedManagerTask(void* pvParameters) {
	statusLed leds[] = { { WIFI_LED_PIN, LED_OFF, false, 0 }, { SERVER_LED_PIN, LED_OFF, false, 0 } };
	const int numLeds = sizeof(leds) / sizeof(leds[0]);

	while (1) {
		// Check for notifications
		uint32_t notification;
		if (xTaskNotifyWait(0, ULONG_MAX, &notification, 0) == pdTRUE) {
			// Process up to two commands
			for (int cmdIdx = 0; cmdIdx < 2; cmdIdx++) {
				uint8_t pin = (notification >> (24 - (cmdIdx * 16))) & 0xFF;
				statusLedCommand cmd = statusLedCommand((notification >> (16 - (cmdIdx * 16))) & 0xFF);

				// Skip invalid pins (0 means no command)
				if (pin == 0)
					continue;

				for (int i = 0; i < numLeds; i++) {
					if (leds[i].pin == pin) {
						leds[i].command = cmd;
						if (cmd == LED_ON_GREEN || cmd == LED_ON_RED || cmd == LED_OFF) {
							setCharlieplexedLED(pin, cmd);
						}
						break;
					}
				}
			}
		}

		// Handle blinking
		unsigned long now = millis();
		for (int i = 0; i < numLeds; i++) {
			if (leds[i].command >= LED_BLINK_GREEN_SLOW) {
				const bool isGreen = (leds[i].command == LED_BLINK_GREEN_SLOW || leds[i].command == LED_BLINK_GREEN_FAST);
				const bool isRed = (leds[i].command == LED_BLINK_RED_SLOW || leds[i].command == LED_BLINK_RED_FAST);
				const bool isSlow = (leds[i].command == LED_BLINK_GREEN_SLOW || leds[i].command == LED_BLINK_RED_SLOW);

				if (isGreen || isRed) {
					const int interval = isSlow ? 500 : 100;
					const statusLedCommand color = isGreen ? LED_ON_GREEN : LED_ON_RED;

					if (now - leds[i].lastToggle >= interval) {
						leds[i].currentState = !leds[i].currentState;
						setCharlieplexedLED(leds[i].pin, leds[i].currentState ? color : LED_OFF);
						leds[i].lastToggle = now;
					}
				}
			}
		}

		vTaskDelay(pdMS_TO_TICKS(25));
	}
}

void setStatusLedState(uint8_t pin1, statusLedCommand cmd1, uint8_t pin2, statusLedCommand cmd2) {
	uint32_t notification = (pin1 << 24) | (cmd1 << 16) | (pin2 << 8) | cmd2;
	xTaskNotify(statusLedTaskHandle, notification, eSetValueWithOverwrite);
}

void setStatusLedState(uint8_t pin, statusLedCommand command) {
	setStatusLedState(pin, command, 0, LED_OFF);
}

const char* getSystemInfo() {
	static char buffer[255];
	FlashMode_t mode = (FlashMode_t)ESP.getFlashChipMode();
	const char* flash_mode_str;

	// Convert flash mode to human-readable string
	switch (mode) {
		case FM_QIO: flash_mode_str = "Quad I/O (QIO)"; break;
		case FM_QOUT: flash_mode_str = "Quad Output (QOUT)"; break;
		case FM_DIO: flash_mode_str = "Dual I/O (DIO)"; break;
		case FM_DOUT: flash_mode_str = "Dual Output (DOUT)"; break;
		case FM_FAST_READ: flash_mode_str = "Fast Read"; break;
		case FM_SLOW_READ: flash_mode_str = "Slow Read"; break;
		case FM_UNKNOWN:
		default: flash_mode_str = "Unknown"; break;
	}

	snprintf(
		buffer,
		sizeof(buffer),
		"\n%s\n"
		"%s-Rev%d\n"
		"%d Core @ %dMHz\n"
		"%dMiB Flash @ %dMHz in %s Mode\n"
		"RAM Heap: %dkiB\n"
		"IDF SDK: %s\n",
		ARDUINO_BOARD,
		ESP.getChipModel(),
		ESP.getChipRevision(),
		ESP.getChipCores(),
		ESP.getCpuFreqMHz(),
		ESP.getFlashChipSize() / (1024 * 1024),
		ESP.getFlashChipSpeed() / (1000 * 1000),
		flash_mode_str,
		ESP.getHeapSize() / 1024,
		ESP.getSdkVersion());

	return buffer;
}

String downloadJSON() {
	HTTPClient http;
	String payload;

	String url = serverURLs[currentServerIndex];
	http.setConnectTimeout(1000);  // Set timeout to 1 second per attempt
	http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

	http.begin(url);

	int httpCode = http.GET();
	if (httpCode == HTTP_CODE_OK) {
		payload = http.getString();
		http.end();
		if (payload.length() == 0) {
			Serial.printf("Fetch from %s returned too little data (%d bytes)\n", url.c_str(), payload.length());
		} else {
			return payload;
		}
	} else {
		Serial.printf("Fetch from %s returned: %i\n", url.c_str(), httpCode);
		http.end();
		currentServerIndex++;  // Try the next server on the next attempt
		currentServerIndex = currentServerIndex % numServers;
	}

	return String();
}

void setBlockColorRGB(uint16_t block, CRGB color) {

	// Apply gamma correction (γ = 2.0)
	auto gammaCorrect = [](float value) -> uint8_t {
		return static_cast<uint8_t>(pow(value / 255.0f, 2.0) * 255.0f);
	};

	color.r = gammaCorrect(color.r);
	color.g = gammaCorrect(color.g);
	color.b = gammaCorrect(color.b);

	// Set the color on the appropriate strand based on the block number
	if (block >= 100 && block < 100 + LED_1_PIXELS) {
		leds1[block - 100] = color;
#if defined(LED_2_PIN)
	} else if (block >= 300 && block < 300 + LED_2_PIXELS) {
		leds2[block - 300] = color;
#endif
	} else if (block != 0) {  // Ignore block 0 (used for trains appearing and disappearing)
		Serial.printf("Block %d is out of range for both strands.\n", block);
	}
}

void setBlockColorId(uint8_t* blockColorIds, uint16_t block, int colorId) {
	if (colorId < blockColorIds[block]) {
		return;	 // Do not update if the new color is lower priority
	}

	blockColorIds[block] = colorId;	 // Update the color ID for the block

	// Get the actual color from the color table, defaulting to black if out of range
	CRGB color = (colorId >= 0 && colorId < static_cast<int>(colorTable.size())) ? colorTable[colorId] : black;

	setBlockColorRGB(block, color);
}

void clearLEDs() {
	// Clear both strands
#if defined(LED_2_PIN)
	fill_solid(leds2, LED_2_PIXELS, black);
#endif
	fill_solid(leds1, LED_1_PIXELS, black);
}

void drawRealtimeMap(time_t epoch) {
	vTaskSuspend(fastLEDDitheringTaskHandle);
	clearLEDs();

	uint8_t blockColorIds[512] = { 0 };	 // Initialize all elements to 0

	// Draw the map based on the current LED update schedule
	for (const auto& update : ledUpdateSchedule) {
		if (epoch >= update.timestamp) {
			setBlockColorId(blockColorIds, update.postBlock, update.colorId);
		} else {
			setBlockColorId(blockColorIds, update.preBlock, update.colorId);
		}
	}

	vTaskResume(fastLEDDitheringTaskHandle);
}

#if defined(TIMETABLE_MODE)
void drawTimetableMap(uint32_t second, const std::vector<const TrainRoute*>& routes) {
	vTaskSuspend(fastLEDDitheringTaskHandle);
	clearLEDs();

	for (size_t routeIndex = 0; routeIndex < routes.size(); routeIndex++) {
		const TrainRoute* route = routes[routeIndex];
		auto trains = createTrainsForRoute(route);
		for (size_t trainIndex = 0; trainIndex < trains.size(); trainIndex++) {
			if (trains[trainIndex].isVisible(second)) {
				uint16_t block = trains[trainIndex].getCurrentBlock(second);
				setBlockColorRGB(block, route->getColor());
			}
		}
	}

	vTaskResume(fastLEDDitheringTaskHandle);
}

void drawFastForwardTimetable(const std::vector<const TrainRoute*>& routes, uint32_t start_time, float xSpeed = 1000.0f) {
	// Calculate the current simulated time in seconds since midnight
	// Start at 5:45 AM ((60*5 + 45) * 60 seconds) @ start_time (millis() at mode start)
	uint32_t seconds = ((millis() - start_time) / 1000.0f * xSpeed) + ((60 * 5 + 45) * 60);
	seconds = seconds % 86400;	// Wrap around at 24 hours (86400 seconds)
	drawTimetableMap(seconds, routes);
}
#endif

time_t parseLEDMap(const String& downloadedJson) {
	JsonDocument doc;
	DeserializationError error = deserializeJson(doc, downloadedJson);

	if (error) {
		Serial.printf("JSON parse error: %s\n", error.c_str());
		return 0;
	}

	String version = doc["version"] | "";
	time_t baseTimestamp = doc["timestamp"] | 0;
	updateInterval = doc["update"] | updateInterval;
	JsonObject colors = doc["colors"];
	JsonArray updates = doc["updates"];

	if (baseTimestamp + updateInterval > nextFetchTime) {
		nextFetchTime = baseTimestamp + updateInterval;
	} else {
		Serial.println("Fetched the same data twice");
		return baseTimestamp;  // No need to update if the data is the same
	}

	if (String(BACKEND_VERSION) != version) {
		Serial.printf("Backend version mismatch: expected %s, got %s\n", BACKEND_VERSION, version.c_str());
	}

	// Serial.printf("%ld Base timestamp: %ld, Update offset: %d, Next fetch time: %ld\n",
	// 			  time(nullptr),
	// 			  baseTimestamp,
	// 			  updateInterval,
	// 			  nextFetchTime);

	// Populate colorTable from the JSON colors object
	colorTable.clear();
	for (JsonPair kv : colors) {
		JsonArray rgb = kv.value().as<JsonArray>();
		colorTable.push_back(CRGB(rgb[0] | 0, rgb[1] | 0, rgb[2] | 0));
	}

	ledUpdateSchedule.clear();
	for (JsonObject update : updates) {
		JsonArray blocks = update["b"];
		int colorId = update["c"];
		int offset = update["t"];

		// Schedule color update
		LedUpdate ledUpdate;
		ledUpdate.preBlock = blocks[0];
		ledUpdate.postBlock = blocks[1];
		if (offset > 0) {
			ledUpdate.timestamp = baseTimestamp + offset;
		} else {
			ledUpdate.timestamp = 0;
		}
		ledUpdate.colorId = colorId;
		ledUpdateSchedule.push_back(ledUpdate);
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
}

#if defined(MODE_BUTTON)
void onMode() {
	// Cycle through modes
	mode = Mode((mode + 1) % 3);
	modeStartTime = millis();	// Reset start time for fast forward mode
	lastMapDrawTime = 0;		// Force immediate redraw
	brightness.setPower(true);	// Ensure brightness is on when changing modes
	Serial.printf("Mode button pressed, mode changed to %s\n",
				  (mode == REALTIME)		  ? "REALTIME"
				  : (mode == ONE_X_TIMETABLE) ? "1x TIMETABLE"
											  : "FAST FORWARD TIMETABLE");
}
#endif

void setup() {
	// USB Serial
	Serial.begin();
	Serial.setDebugOutput(true);
	xTaskCreate(improvSerialTask, "Improv Serial Task", 4096, nullptr, 2, nullptr);

	// --- Setup Addressable LEDs ---
#if defined(LVL_Shifter_EN)
	pinMode(LVL_Shifter_EN, OUTPUT);
	digitalWrite(LVL_Shifter_EN, HIGH);	 // Disable LVL Shifter
#endif
	pinMode(LED_5V_EN, OUTPUT);
	digitalWrite(LED_5V_EN, LOW);  // Disable 5V Power

	// FastLED initialization
	FastLED.addLeds<WS2811, LED_1_PIN, GRB>(leds1, LED_1_PIXELS);
#if defined(LED_2_PIN)
	FastLED.addLeds<WS2811, LED_2_PIN, GRB>(leds2, LED_2_PIXELS);
#endif
	FastLED.clear(true);  // Clear all pixels on both strands
	FastLED.setDither(BINARY_DITHER);

	xTaskCreate(fastLEDDitheringTask, "FastLED Dithering", 1024, NULL, 2, &fastLEDDitheringTaskHandle);

#if defined(LVL_Shifter_EN)
	digitalWrite(LVL_Shifter_EN, LOW);	//Enable LVL Shifter
#endif
	digitalWrite(LED_5V_EN, HIGH);	//Enable 5V Power

	// --- Setup Buttons ---
	buttons.add(BRIGHTNESS_DOWN_BUTTON, onBrightnessDown);
	buttons.add(BRIGHTNESS_UP_BUTTON, onBrightnessUp);
	buttons.add(POWER_BUTTON, onPower);
#if defined(MODE_BUTTON)
	buttons.add(MODE_BUTTON, onMode);
#endif
	buttons.begin();

	Serial.println(getSystemInfo());

#if defined(FACTORY_TEST)
	factoryTestMode();
	buttons.setCallback(POWER_BUTTON, onPower);
#endif

	// --- Time Setup ---
	sntp_set_time_sync_notification_cb(timeavailable);
	sntp_set_sync_interval(1000 * 60 * 15);	 // Set sync interval to 15 minutes
	sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
	configTzTime(time_zone, ntpServers[0], ntpServers[1], ntpServers[2]);

	// --- WiFi Setup ---
	xTaskCreate(statusLedManagerTask, "Status LED Manager", 1024, NULL, 1, &statusLedTaskHandle);
	setStatusLedState(WIFI_LED_PIN, LED_BLINK_GREEN_FAST, SERVER_LED_PIN, LED_OFF);
	WiFi.setTxPower(WIFI_POWER_15dBm);	// Set WiFi power to avoid interference

	fetchOffset = random(0, 999);  // Random delay between 0 and 999 ms to reduce server load

	WiFiImprovSetup();

#if defined(TIMETABLE_MODE)
	printTimetableSize(routes);
#endif
	brightness.begin();
}

void loop() {
	time_t epoch = time(nullptr);  // Get current time
	bool wiFiConnected = (WiFi.status() == WL_CONNECTED);
	if (!wiFiConnected)
		manageWiFiConnection();

	switch (mode) {
		// Run the realtime mode using the LED-Rails backend server (default)
		case REALTIME:
			if (wiFiConnected) {
				// --- Fetch new data periodically ---
				if (epoch > nextFetchTime && millis() % 1000 > fetchOffset) {
					if (epoch > nextFetchTime + updateInterval) {
						setStatusLedState(WIFI_LED_PIN, LED_ON_GREEN, SERVER_LED_PIN, LED_BLINK_GREEN_FAST);
					}

					time_t timeOffset = 0;
					String downloadedJson = downloadJSON();
					if (downloadedJson.length() > 0) {
						setStatusLedState(WIFI_LED_PIN, LED_ON_GREEN, SERVER_LED_PIN, LED_ON_GREEN);
						time_t timeOffset = epoch - parseLEDMap(downloadedJson);
					} else {
						Serial.println("All servers failed to provide data.");
						setStatusLedState(WIFI_LED_PIN, LED_ON_GREEN, SERVER_LED_PIN, LED_ON_RED);
					}

					nextFetchTime = constrain(nextFetchTime, epoch + 6, epoch + updateInterval);

					Serial.printf("%s fetchDelay:%is MCU:%2.0f°C WiFi:%idBm\n",
								  getLocalTime(epoch),
								  timeOffset,
								  temperatureRead(),
								  WiFi.RSSI());
					Serial.flush();
				}

				// --- Push updates to the LED strips only if changes were made ---
				if (lastMapDrawTime < epoch) {
					drawRealtimeMap(epoch);	 // Draw the map with the current updates
					lastMapDrawTime = epoch;
				}

			} else {
				if (millis() < 60 * 1000) {
					setStatusLedState(WIFI_LED_PIN, LED_BLINK_GREEN_FAST, SERVER_LED_PIN, LED_OFF);
				} else {
					setStatusLedState(WIFI_LED_PIN, LED_ON_RED, SERVER_LED_PIN, LED_OFF);
				}
			}
			break;

#if defined(TIMETABLE_MODE)
		// Run the timetable mode at 1x speed (uses wiFi for time sync if available)
		case ONE_X_TIMETABLE:
			if (epoch > lastMapDrawTime) {
				struct tm timeinfo;
				localtime_r(&epoch, &timeinfo);
				uint32_t secondsSinceMidnight = timeinfo.tm_hour * 3600 + timeinfo.tm_min * 60 + timeinfo.tm_sec;
				drawTimetableMap(secondsSinceMidnight, routes);
				lastMapDrawTime = epoch;
			}

			if (wiFiConnected) {
				setStatusLedState(WIFI_LED_PIN, LED_ON_GREEN, SERVER_LED_PIN, LED_OFF);
			} else if (millis() < 60 * 1000) {
				setStatusLedState(WIFI_LED_PIN, LED_BLINK_GREEN_FAST, SERVER_LED_PIN, LED_OFF);
			} else {
				setStatusLedState(WIFI_LED_PIN, LED_ON_RED, SERVER_LED_PIN, LED_OFF);
			}
			break;

		// Run the timetable mode at 1000x speed (no wiFi required)
		case FAST_FORWARD_TIMETABLE:
			drawFastForwardTimetable(routes, modeStartTime, 1000.0f);  // 1000x speed
			setStatusLedState(WIFI_LED_PIN, LED_OFF, SERVER_LED_PIN, LED_OFF);
			nextFetchTime = 0;
			break;
#endif

		default:
			Serial.println("Unknown mode, reverting to REALTIME");
			mode = REALTIME;
			break;
	}

	brightness.update();

	vTaskDelay(pdMS_TO_TICKS(30));
}