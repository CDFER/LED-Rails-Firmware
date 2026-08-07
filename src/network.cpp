#include "network.h"
#include <cmath>
#include <cstdlib>
#include "brightness.h"
#include "dailySchedule.h"
#include "deviceWebAssets.h"
#include "ledStorageSync.h"
#include "modeManager.h"
#include "statusLeds.h"

NetworkManager network;

namespace {
struct LegacySavedWiFiNetwork {
	char ssid[MAX_SSID_LEN];
	char password[MAX_PASS_LEN];
};

void copyUtf8(char* destination, size_t destinationSize, const char* source, size_t sourceSize) {
	if (destinationSize == 0) {
		return;
	}

	const size_t sourceLength = strnlen(source, sourceSize);
	size_t length = min(sourceLength, destinationSize - 1);
	if (length < sourceLength) {
		while (length > 0 && (static_cast<uint8_t>(source[length]) & 0xC0) == 0x80) {
			--length;
		}
	}

	memcpy(destination, source, length);
	destination[length] = '\0';
}

bool serveDeviceWebAsset(AsyncWebServerRequest* request, const String& requestedPath) {
	const String assetPath = requestedPath == "/index.html" ? "/" : requestedPath;
	for (size_t assetIndex = 0; assetIndex < deviceWebAssets::assetCount; assetIndex++) {
		const DeviceWebAsset& asset = deviceWebAssets::assets[assetIndex];
		if (assetPath != asset.path) {
			continue;
		}

		AsyncWebServerResponse* response = request->beginResponse(200, asset.contentType, asset.data, asset.size);
		response->addHeader("Content-Encoding", "gzip");
		response->addHeader("Cache-Control", assetPath == "/" ? "no-store" : "public, max-age=31536000, immutable");
		request->send(response);
		return true;
	}
	return false;
}

bool parseFloatParameter(AsyncWebServerRequest* request, const String& name, float& value) {
	const AsyncWebParameter* parameter = request->getParam(name.c_str(), true);
	if (parameter == nullptr) {
		return false;
	}

	const String text = parameter->value();
	const char* textStart = text.c_str();
	char* textEnd = nullptr;
	const float parsedValue = strtof(textStart, &textEnd);
	if (textEnd == textStart || *textEnd != '\0' || !std::isfinite(parsedValue)) {
		return false;
	}

	value = parsedValue;
	return true;
}

bool isTickDeadlineReached(TickType_t currentTick, TickType_t deadline) {
	return static_cast<int32_t>(currentTick - deadline) >= 0;
}
}

static void onNtpTimeSyncCallback(struct timeval* timeValue) {
	(void)timeValue;
	Serial.println("Time synced via NTP");
}

NetworkManager::NetworkManager() : improvSerial(&Serial), server(80) {
	// Initialize saved WiFi networks to empty
	memset(savedWiFiNetworks, 0, sizeof(savedWiFiNetworks));
	for (int networkIndex = 0; networkIndex < MAX_WIFI_NETWORKS; networkIndex++) {
		savedWiFiNetworks[networkIndex].ssid[0] = '\0';
		savedWiFiNetworks[networkIndex].password[0] = '\0';
	}

	currentMapData = std::make_shared<MapData>();

	serverUrls = {
		String("http://api.keastudios.co.nz/") + CITY_CODE + "-ltm/" + BACKEND_VERSION + ".json",
#if defined(BETA_BUILD)
		String("http://192.168.86.31:3000/") + CITY_CODE + "-ltm/" + BACKEND_VERSION
		    + ".json",  // For testing with a local server
#endif
		String("http://keastudios.co.nz/") + CITY_CODE + "-ltm/" + BACKEND_VERSION + ".json",
		String("http://dirksonline.net/") + CITY_CODE + "-ltm/" + BACKEND_VERSION + ".json",
	};
}

void NetworkManager::exportWiFi() {
	SavedWiFiNetwork savedNetworks[MAX_WIFI_NETWORKS] = {};
	{
		std::lock_guard<std::mutex> lock(wifiNetworksMutex);
		memcpy(savedNetworks, savedWiFiNetworks, sizeof(savedNetworks));
	}

	xSemaphoreTake(fastLEDPreferencesMutex, portMAX_DELAY);
	preferences.begin("wifi");
	preferences.putBytes("wifi", savedNetworks, sizeof(savedNetworks));
	preferences.end();
	xSemaphoreGive(fastLEDPreferencesMutex);
}

void NetworkManager::importWiFi() {
	SavedWiFiNetwork loadedNetworks[MAX_WIFI_NETWORKS] = {};
	xSemaphoreTake(fastLEDPreferencesMutex, portMAX_DELAY);
	preferences.begin("wifi", true);
	const size_t storedSize = preferences.getBytesLength("wifi");
	if (storedSize == sizeof(LegacySavedWiFiNetwork) * MAX_WIFI_NETWORKS) {
		LegacySavedWiFiNetwork legacyNetworks[MAX_WIFI_NETWORKS] = {};
		preferences.getBytes("wifi", legacyNetworks, sizeof(legacyNetworks));
		for (int networkIndex = 0; networkIndex < MAX_WIFI_NETWORKS; networkIndex++) {
			copyUtf8(loadedNetworks[networkIndex].ssid,
			         sizeof(loadedNetworks[networkIndex].ssid),
			         legacyNetworks[networkIndex].ssid,
			         sizeof(legacyNetworks[networkIndex].ssid));
			copyUtf8(loadedNetworks[networkIndex].password,
			         sizeof(loadedNetworks[networkIndex].password),
			         legacyNetworks[networkIndex].password,
			         sizeof(legacyNetworks[networkIndex].password));
		}
	} else {
		preferences.getBytes("wifi", loadedNetworks, sizeof(loadedNetworks));
		for (int networkIndex = 0; networkIndex < MAX_WIFI_NETWORKS; networkIndex++) {
			loadedNetworks[networkIndex].ssid[MAX_SSID_LEN] = '\0';
			loadedNetworks[networkIndex].password[MAX_PASS_LEN] = '\0';
		}
	}
	preferences.end();
	xSemaphoreGive(fastLEDPreferencesMutex);

	std::lock_guard<std::mutex> lock(wifiNetworksMutex);
	memcpy(savedWiFiNetworks, loadedNetworks, sizeof(savedWiFiNetworks));
}

bool NetworkManager::saveWiFiNetwork(const String& ssid, const String& password) {
	if (ssid.isEmpty() || ssid.length() > MAX_SSID_LEN || password.length() > MAX_PASS_LEN) {
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(wifiNetworksMutex);
		int existingNetworkIndex = -1;
		for (int networkIndex = 0; networkIndex < MAX_WIFI_NETWORKS; networkIndex++) {
			if (ssid == savedWiFiNetworks[networkIndex].ssid) {
				existingNetworkIndex = networkIndex;
				break;
			}
		}

		const int firstNetworkToShift = existingNetworkIndex >= 0 ? existingNetworkIndex : MAX_WIFI_NETWORKS - 1;
		for (int networkIndex = firstNetworkToShift; networkIndex > 0; networkIndex--) {
			savedWiFiNetworks[networkIndex] = savedWiFiNetworks[networkIndex - 1];
		}

		copyUtf8(savedWiFiNetworks[0].ssid, sizeof(savedWiFiNetworks[0].ssid), ssid.c_str(), ssid.length());
		copyUtf8(
		    savedWiFiNetworks[0].password, sizeof(savedWiFiNetworks[0].password), password.c_str(), password.length());
	}

	exportWiFi();
	return true;
}

bool NetworkManager::forgetWiFiNetwork(const String& ssid) {
	bool networkRemoved = false;
	{
		std::lock_guard<std::mutex> lock(wifiNetworksMutex);
		for (int networkIndex = 0; networkIndex < MAX_WIFI_NETWORKS; networkIndex++) {
			if (ssid == savedWiFiNetworks[networkIndex].ssid) {
				for (int followingIndex = networkIndex; followingIndex < MAX_WIFI_NETWORKS - 1; followingIndex++) {
					savedWiFiNetworks[followingIndex] = savedWiFiNetworks[followingIndex + 1];
				}
				memset(&savedWiFiNetworks[MAX_WIFI_NETWORKS - 1], 0, sizeof(SavedWiFiNetwork));
				networkRemoved = true;
				break;
			}
		}
	}

	if (networkRemoved) {
		exportWiFi();
	}
	return networkRemoved;
}

std::vector<String> NetworkManager::getSavedWiFiNetworkNames() {
	std::vector<String> networkNames;
	std::lock_guard<std::mutex> lock(wifiNetworksMutex);
	for (const SavedWiFiNetwork& savedNetwork : savedWiFiNetworks) {
		if (savedNetwork.ssid[0] != '\0') {
			networkNames.emplace_back(savedNetwork.ssid);
		}
	}
	return networkNames;
}

void NetworkManager::onImprovWiFiErrorCallback(ImprovTypes::Error error) {
	(void)error;
	// Serial.printf("Improv WiFi Error: %d\n", error);
	server.end();
	server.begin();
}

void NetworkManager::onImprovWiFiConnectedCallback(const char* ssid, const char* password) {
	saveWiFiNetwork(String(ssid), String(password));

	// Restart the web server
	server.end();
	server.begin();
}

void NetworkManager::setupWebServer() {
	server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
		serveDeviceWebAsset(request, "/");
	});

	server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* request) {
		JsonDocument document;
		JsonObject device = document["device"].to<JsonObject>();
		device["city"] = CITY_CODE;
		device["firmware"] = FIRMWARE;
		device["version"] = FIRMWARE_VERSION;
		device["board"] = ARDUINO_BOARD;
		device["uptime"] = millis();

		JsonObject wifi = document["wifi"].to<JsonObject>();
		const bool wifiIsConnected = WiFi.status() == WL_CONNECTED;
		wifi["connected"] = wifiIsConnected;
		if (wifiIsConnected) {
			wifi["ssid"] = WiFi.SSID();
			wifi["rssi"] = WiFi.RSSI();
		}
		JsonArray savedNetworks = wifi["savedNetworks"].to<JsonArray>();
		for (const String& savedNetwork : network.getSavedWiFiNetworkNames()) {
			savedNetworks.add(savedNetwork);
		}

		JsonObject leds = document["leds"].to<JsonObject>();
		leds["on"] = brightnessManager.isOn();
		leds["brightness"] = brightnessManager.getBrightness();
		leds["minimumBrightness"] = MIN_BRIGHTNESS;
		leds["maximumBrightness"] = MAX_BRIGHTNESS;
#if defined(LIGHT_SENSOR)
		leds["automaticBrightness"] = true;
		leds["ambientLux"] = brightnessManager.getAmbientLux();
		leds["ambientBrightness"] = brightnessManager.getAmbientBrightness();
		BrightnessCurve curve;
		brightnessManager.getBrightnessCurve(curve);
		JsonArray curveValues = leds["curve"].to<JsonArray>();
		for (int bucketNumber = 0; bucketNumber < BRIGHTNESS_BUCKET_COUNT; bucketNumber++) {
			JsonObject curvePoint = curveValues.add<JsonObject>();
			curvePoint["lux"] = curve.buckets[bucketNumber].luxMax;
			curvePoint["brightness"] = curve.buckets[bucketNumber].brightnessMax * 100.0f;
		}
#else
		leds["automaticBrightness"] = false;
#endif

		JsonObject mode = document["mode"].to<JsonObject>();
		const Mode currentMode = modeManager.getCurrentMode();
		mode["current"] = currentMode;
		mode["name"] = ModeManager::getModeName(currentMode);
		JsonArray availableModes = mode["available"].to<JsonArray>();
		for (int modeIndex = REALTIME_MODE; modeIndex < NUM_MODES; modeIndex++) {
			JsonObject availableMode = availableModes.add<JsonObject>();
			availableMode["id"] = modeIndex;
			availableMode["name"] = ModeManager::getModeName(static_cast<Mode>(modeIndex));
		}

		std::array<DailyScheduleEntry, DAILY_SCHEDULE_ENTRY_COUNT> scheduleEntries;
		dailyScheduleManager.getSchedule(scheduleEntries);
		JsonArray schedule = document["schedule"].to<JsonArray>();
		for (const DailyScheduleEntry& entry : scheduleEntries) {
			JsonObject scheduleEntry = schedule.add<JsonObject>();
			scheduleEntry["enabled"] = entry.enabled;
			scheduleEntry["minute"] = entry.minuteOfDay;
			scheduleEntry["on"] = entry.turnOn;
			scheduleEntry["mode"] = entry.mode;
		}

		AsyncResponseStream* response = request->beginResponseStream("application/json");
		response->addHeader("Cache-Control", "no-store");
		serializeJson(document, *response);
		request->send(response);
	});

	server.on("/api/led/power", HTTP_POST, [](AsyncWebServerRequest* request) {
		const AsyncWebParameter* powerParameter = request->getParam("on", true);
		if (powerParameter == nullptr) {
			request->send(400, "application/json", "{\"error\":\"Missing power state\"}");
			return;
		}

		const String requestedState = powerParameter->value();
		if (requestedState != "0" && requestedState != "1") {
			request->send(400, "application/json", "{\"error\":\"Invalid power state\"}");
			return;
		}

		brightnessManager.setPower(requestedState == "1");
		request->send(202, "application/json", "{\"ok\":true}");
	});

	server.on("/api/schedule", HTTP_POST, [](AsyncWebServerRequest* request) {
		std::array<DailyScheduleEntry, DAILY_SCHEDULE_ENTRY_COUNT> requestedSchedule;
		for (uint8_t entryIndex = 0; entryIndex < DAILY_SCHEDULE_ENTRY_COUNT; entryIndex++) {
			const String prefix = "entry" + String(entryIndex);
			const AsyncWebParameter* enabledParameter = request->getParam((prefix + "Enabled").c_str(), true);
			const AsyncWebParameter* minuteParameter = request->getParam((prefix + "Minute").c_str(), true);
			const AsyncWebParameter* onParameter = request->getParam((prefix + "On").c_str(), true);
			const AsyncWebParameter* modeParameter = request->getParam((prefix + "Mode").c_str(), true);
			if (enabledParameter == nullptr || minuteParameter == nullptr || onParameter == nullptr
			    || modeParameter == nullptr) {
				request->send(400, "application/json", "{\"error\":\"All daily schedule entries are required\"}");
				return;
			}

			char* minuteEnd = nullptr;
			char* modeEnd = nullptr;
			const String minuteText = minuteParameter->value();
			const String modeText = modeParameter->value();
			const long minute = strtol(minuteText.c_str(), &minuteEnd, 10);
			const long mode = strtol(modeText.c_str(), &modeEnd, 10);
			const String enabled = enabledParameter->value();
			const String turnOn = onParameter->value();
			if ((enabled != "0" && enabled != "1") || (turnOn != "0" && turnOn != "1")
			    || minuteEnd == minuteText.c_str() || *minuteEnd != '\0' || modeEnd == modeText.c_str()
			    || *modeEnd != '\0' || minute < 0 || minute >= 24 * 60 || mode < REALTIME_MODE || mode >= NUM_MODES) {
				request->send(400, "application/json", "{\"error\":\"Invalid daily schedule entry\"}");
				return;
			}

			requestedSchedule[entryIndex] = {
				enabled == "1", static_cast<uint16_t>(minute), turnOn == "1", static_cast<Mode>(mode)
			};
		}

		if (!dailyScheduleManager.requestSchedule(requestedSchedule)) {
			request->send(400, "application/json", "{\"error\":\"Enabled entries must be in time order\"}");
			return;
		}
		request->send(202, "application/json", "{\"ok\":true}");
	});

#if defined(LIGHT_SENSOR)
	server.on("/api/led/curve", HTTP_POST, [](AsyncWebServerRequest* request) {
		BrightnessCurve requestedCurve;
		for (int bucketNumber = 0; bucketNumber < BRIGHTNESS_BUCKET_COUNT; bucketNumber++) {
			const String luxName = "lux" + String(bucketNumber);
			const String brightnessName = "brightness" + String(bucketNumber);
			float lux = 0.0f;
			float brightnessPercent = 0.0f;
			if (!parseFloatParameter(request, luxName, lux)
			    || !parseFloatParameter(request, brightnessName, brightnessPercent)) {
				request->send(400, "application/json", "{\"error\":\"All curve points are required\"}");
				return;
			}

			requestedCurve.buckets[bucketNumber] = { lux, brightnessPercent / 100.0f };
		}

		if (!brightnessManager.requestBrightnessCurve(requestedCurve)) {
			request->send(400, "application/json", "{\"error\":\"Curve points must increase in lux and brightness\"}");
			return;
		}

		request->send(202, "application/json", "{\"ok\":true}");
	});
#else
	server.on("/api/led/brightness", HTTP_POST, [](AsyncWebServerRequest* request) {
		const AsyncWebParameter* directionParameter = request->getParam("direction", true);
		if (directionParameter == nullptr) {
			request->send(400, "application/json", "{\"error\":\"Missing brightness direction\"}");
			return;
		}

		const String direction = directionParameter->value();
		if (direction == "up") {
			brightnessManager.increase();
		} else if (direction == "down") {
			brightnessManager.decrease();
		} else {
			request->send(400, "application/json", "{\"error\":\"Invalid brightness direction\"}");
			return;
		}

		request->send(202, "application/json", "{\"ok\":true}");
	});
#endif

	server.on("/api/mode", HTTP_POST, [](AsyncWebServerRequest* request) {
		const AsyncWebParameter* modeParameter = request->getParam("mode", true);
		if (modeParameter == nullptr) {
			request->send(400, "application/json", "{\"error\":\"Missing mode\"}");
			return;
		}

		const String requestedModeValue = modeParameter->value();
		const char* modeText = requestedModeValue.c_str();
		char* end = nullptr;
		const long requestedMode = strtol(modeText, &end, 10);
		if (end == modeText || *end != '\0' || requestedMode < REALTIME_MODE || requestedMode >= NUM_MODES) {
			request->send(400, "application/json", "{\"error\":\"Invalid mode\"}");
			return;
		}

		modeManager.requestMode(static_cast<Mode>(requestedMode));
		request->send(202, "application/json", "{\"ok\":true}");
	});

	server.on("/api/wifi/forget", HTTP_POST, [this](AsyncWebServerRequest* request) {
		const AsyncWebParameter* ssidParameter = request->getParam("ssid", true);
		if (ssidParameter == nullptr || !forgetWiFiNetwork(ssidParameter->value())) {
			request->send(404, "application/json", "{\"error\":\"Saved network not found\"}");
			return;
		}

		requestWiFiReconnect();
		request->send(202, "application/json", "{\"ok\":true}");
	});

	server.onNotFound([](AsyncWebServerRequest* request) {
		if (request->method() == HTTP_GET && !request->url().startsWith("/api/")
		    && serveDeviceWebAsset(request, request->url())) {
			return;
		}
		request->send(404);
	});

	server.begin();
}

void NetworkManager::requestWiFiReconnect() {
	wifiReconnectRequested.store(true);
}

void NetworkManager::begin() {
	WiFi.setHostname(CITY_CODE "-train-map");

	// --- Time Setup ---
	const char* ntpServers[] = { "pool.ntp.org", "pool.msltime.measurement.govt.nz", "nz.pool.ntp.org" };
	sntp_set_time_sync_notification_cb(onNtpTimeSyncCallback);
	sntp_set_sync_interval(1000 * 60 * 15);  // Set sync interval to 15 minutes
	sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
	configTzTime(TIMEZONE, ntpServers[0], ntpServers[1], ntpServers[2]);

	importWiFi();

#if defined(CONFIG_IDF_TARGET_ESP32S2)
	enum ImprovTypes::ChipFamily chip = ImprovTypes::ChipFamily::CF_ESP32_S2;
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
	enum ImprovTypes::ChipFamily chip = ImprovTypes::ChipFamily::CF_ESP32_C3;
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
	enum ImprovTypes::ChipFamily chip = ImprovTypes::ChipFamily::CF_ESP32_S3;
#else
	enum ImprovTypes::ChipFamily chip = ImprovTypes::ChipFamily::CF_ESP32;
#endif

	improvSerial.setDeviceInfo(chip, FIRMWARE, FIRMWARE_VERSION, ARDUINO_BOARD, "http://{LOCAL_IPV4}/");
	improvSerial.onImprovError([](ImprovTypes::Error err) {
		network.onImprovWiFiErrorCallback(err);
	});
	improvSerial.onImprovConnected([](const char* ssid, const char* password) {
		network.onImprovWiFiConnectedCallback(ssid, password);
	});

	setupWebServer();

	const bool savedWiFiFound = !getSavedWiFiNetworkNames().empty();

	if (savedWiFiFound) {
		Serial.println("WiFi credentials found...");
		statusLeds.setState(WIFI_LED_PIN, LED_BLINK_GREEN_FAST);
	} else {
		Serial.println("No WiFi credentials found...");
		statusLeds.setState(WIFI_LED_PIN, LED_ON_RED);
	}

#if defined(BETA_BUILD)
	fetchOffset = 0;  // No need for random offset in beta builds
#else
	fetchOffset = static_cast<TickType_t>(random(static_cast<long>(xPortGetTickRateHz())));
#endif

	// Create tasks
	xTaskCreatePinnedToCore(improvSerialTask, "Improv Serial Task", 4096, this, 4, nullptr, ARDUINO_RUNNING_CORE);
	xTaskCreatePinnedToCore(networkTask, "Network Task", 16384, this, 3, nullptr, ARDUINO_RUNNING_CORE);
}

void NetworkManager::improvSerialTask(void* pvParameters) {
	NetworkManager* manager = static_cast<NetworkManager*>(pvParameters);
	while (true) {
		while (Serial.available() > 0) {
			manager->improvSerial.handleSerial();
		}
		vTaskDelay(pdMS_TO_TICKS(20));
	}
}

String NetworkManager::fetchMapUpdateJson() {
	HTTPClient httpClient;
	const String& url = serverUrls[currentServerIndex];

	// Avoid stale keep-alive sockets and bound both connection and response reads.
	httpClient.setReuse(false);
	httpClient.setConnectTimeout(5000);
	httpClient.setTimeout(5000);
	httpClient.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
	if (!httpClient.begin(url)) {
		Serial.printf("Fetch from %s: unable to begin request\n", url.c_str());
	} else {
		int httpCode = httpClient.GET();
		if (httpCode == HTTP_CODE_OK) {
			String responseBody = httpClient.getString();
			httpClient.end();
			if (responseBody.length() > 0) {
				failedFetchCount = 0;
				return responseBody;
			}
			Serial.printf("Fetch from %s: empty payload\n", url.c_str());
		} else {
			Serial.printf("Fetch from %s error: %i\n", url.c_str(), httpCode);
		}
		httpClient.end();
	}

	if (++failedFetchCount > 3) {
		currentServerIndex = (currentServerIndex + 1) % serverUrls.size();
	}
	return "";
}

time_t NetworkManager::parseLedMapUpdateJson(const String& downloadedJson) {
	JsonDocument doc;
	if (DeserializationError error = deserializeJson(doc, downloadedJson)) {
		Serial.printf("JSON parse error: %s\n", error.c_str());
		return 0;
	}

	time_t baseTimestamp = doc["timestamp"] | 0;
	updateInterval = doc["update"] | updateInterval;

	if (baseTimestamp + updateInterval <= nextFetchTime) {
		Serial.println("Fetched the same data twice");
		lastFetchTime = time(nullptr);
		return baseTimestamp;
	}
	nextFetchTime = baseTimestamp + updateInterval;

	const char* version = doc["version"] | "";
	if (strcmp(BACKEND_VERSION, version) != 0) {
		Serial.printf("Backend version mismatch: expected %s, got %s\n", BACKEND_VERSION, version);
	}

	auto newMapData = std::make_shared<MapData>();

	JsonObject colors = doc["colors"];
	newMapData->colorTable.reserve(colors.size());
	for (JsonPair colorEntry : colors) {
		JsonArray rgbValues = colorEntry.value();
		newMapData->colorTable.push_back(CRGB(rgbValues[0] | 0, rgbValues[1] | 0, rgbValues[2] | 0));
	}

	JsonArray updates = doc["updates"];
	newMapData->ledUpdateSchedule.reserve(updates.size());
	for (JsonObject updateEntry : updates) {
		int transitionOffsetSeconds = updateEntry["t"];

		time_t timestamp = 0;
		uint16_t msOffset = 0;
		if (transitionOffsetSeconds > 0) {
			timestamp = baseTimestamp + transitionOffsetSeconds;
			// Random Seed based on block for consistency to spread out updates within the second
			randomSeed(updateEntry["b"][0]);
			msOffset = random(0, 1000);
		}

		newMapData->ledUpdateSchedule.push_back(
		    { updateEntry["b"][0], updateEntry["b"][1], updateEntry["c"], timestamp, msOffset });
	}

	{
		std::lock_guard<std::mutex> lock(mapDataMutex);
		currentMapData = newMapData;
	}

	lastFetchTime = time(nullptr);

	return baseTimestamp;
}

void NetworkManager::setSystemState(NetworkMode mode, bool statusLedsEnabled) {
	this->currentMode = mode;
	this->statusLedsEnabled = statusLedsEnabled;
}

std::shared_ptr<MapData> NetworkManager::getMapData() {
	std::lock_guard<std::mutex> lock(mapDataMutex);
	return currentMapData;
}

const char* NetworkManager::formatEpoch(time_t epoch) const {
	struct tm localTime;
	static char formattedTimeBuffer[64];

	// Convert epoch to local time
	if (!localtime_r(&epoch, &localTime)) {
		return "No time available";
	}
	if (strftime(formattedTimeBuffer, sizeof(formattedTimeBuffer), "%H:%M:%S", &localTime)) {
		return formattedTimeBuffer;
	}
	return "Format error";
}

const char* NetworkManager::getFormattedTimeWithMs() const {
	struct timeval timeValue;
	gettimeofday(&timeValue, NULL);  // Get epoch time with microsecond precision

	struct tm localTime;
	localtime_r(&timeValue.tv_sec, &localTime);  // Convert seconds to local time struct

	long milliseconds = timeValue.tv_usec / 1000;  // Convert microseconds to milliseconds

	static char formattedTimeBuffer[16];
	// Format: HH:MM:SS.mmm
	snprintf(formattedTimeBuffer,
	         sizeof(formattedTimeBuffer),
	         "%02d:%02d:%02d.%03ld",
	         localTime.tm_hour,
	         localTime.tm_min,
	         localTime.tm_sec,
	         milliseconds);

	return formattedTimeBuffer;
}

void NetworkManager::networkTask(void* pvParameters) {
	NetworkManager* manager = static_cast<NetworkManager*>(pvParameters);

	while (true) {
		if (manager->wifiReconnectRequested.exchange(false)) {
			WiFi.disconnect();
			manager->lastWiFiConnectAttempt = 0;
			manager->wifiAttemptedMask = 0;
		}

		manager->updateStatusLeds();

		if (WiFi.status() == WL_CONNECTED) {
			if (manager->currentMode == NetworkMode::REALTIME) {
				manager->manageLedMapApi();
			}
		} else {
			manager->manageWiFiConnection();
		}
		vTaskDelay(pdMS_TO_TICKS(100));  // Sleep bit to yield to other tasks
	}
}

void NetworkManager::updateStatusLeds() {
	wifiConnected = (WiFi.status() == WL_CONNECTED);
	time_t epoch = time(nullptr);

	if (!statusLedsEnabled || currentMode == NetworkMode::OFF) {
		statusLeds.setState(WIFI_LED_PIN, LED_OFF, SERVER_LED_PIN, LED_OFF);

	} else if (currentMode == NetworkMode::TIME_ONLY) {
		statusLeds.setState(SERVER_LED_PIN, LED_OFF);
		if (wifiConnected) {
			statusLeds.setState(WIFI_LED_PIN, LED_ON_GREEN);
		} else if (millis() > 60 * 1000) {
			statusLeds.setState(WIFI_LED_PIN, LED_ON_RED);
		} else {
			statusLeds.setState(WIFI_LED_PIN, LED_BLINK_GREEN_FAST);
		}

	} else if (currentMode == NetworkMode::REALTIME) {
		if (wifiConnected) {
			statusLeds.setState(WIFI_LED_PIN, LED_ON_GREEN);
			if (failedFetchCount > 3 + serverUrls.size()) {
				statusLeds.setState(SERVER_LED_PIN, LED_ON_RED);
			} else if (lastFetchTime == 0 || failedFetchCount > 3) {
				statusLeds.setState(SERVER_LED_PIN, LED_BLINK_GREEN_FAST);
			} else {
				statusLeds.setState(SERVER_LED_PIN, LED_ON_GREEN);
			}
		} else {
			statusLeds.setState(SERVER_LED_PIN, LED_OFF);
			if (millis() > 60 * 1000) {
				statusLeds.setState(WIFI_LED_PIN, LED_ON_RED);
			} else {
				statusLeds.setState(WIFI_LED_PIN, LED_BLINK_GREEN_FAST);
			}
		}
	}
}

void NetworkManager::manageLedMapApi() {
	const time_t epoch = time(nullptr);
	const TickType_t currentTick = xTaskGetTickCount();
	const time_t minimumFetchInterval = 2;
	const time_t maximumFetchTime = epoch + max<time_t>(updateInterval, minimumFetchInterval);
	if (nextFetchTime > maximumFetchTime) {
		nextFetchTime = maximumFetchTime;
	}
	const bool fetchRequired = fetchScheduled || epoch > nextFetchTime;

	if (!fetchRequired) {
		fetchScheduled = false;
		return;
	}

	if (!fetchScheduled) {
		fetchDueTick = currentTick + fetchOffset;
		fetchScheduled = true;
	}

	if (!isTickDeadlineReached(currentTick, fetchDueTick)) {
		return;
	}

	time_t timeOffset = 0;
	String downloadedJson = fetchMapUpdateJson();
	const time_t mapTimestamp = downloadedJson.length() > 0 ? parseLedMapUpdateJson(downloadedJson) : 0;
	if (mapTimestamp != 0) {
		timeOffset = epoch - mapTimestamp;
		const time_t earliestNextFetch = time(nullptr) + 2;
		if (nextFetchTime < earliestNextFetch) {
			nextFetchTime = earliestNextFetch;
		}
		fetchScheduled = false;
	} else {
		if (downloadedJson.length() > 0) {
			++failedFetchCount;
		}
		if (failedFetchCount > 3 + serverUrls.size()) {
			Serial.println("All servers failed to provide data.");
		}
		nextFetchTime = time(nullptr);
		const TickType_t retryDelay = static_cast<TickType_t>(xPortGetTickRateHz()) + fetchOffset;
		fetchDueTick = xTaskGetTickCount() + retryDelay;
		fetchScheduled = true;
	}

	// Get current time incl ms HH:MM:SS.mmm
	Serial.printf("%s fetchDelay:%2is size:%1.1fkiB MCU:%2.0f°C WiFi:%2idBm\n",
	              getFormattedTimeWithMs(),
	              timeOffset,
	              downloadedJson.length() / 1024.0,
	              temperatureRead(),
	              WiFi.RSSI());
	Serial.flush();
}

void NetworkManager::manageWiFiConnection() {
	const TickType_t attemptTimeout = pdMS_TO_TICKS(30000);  // 30 seconds between scans/attempts

	if (xTaskGetTickCount() - lastWiFiConnectAttempt > attemptTimeout || lastWiFiConnectAttempt == 0) {
		lastWiFiConnectAttempt = xTaskGetTickCount();
		SavedWiFiNetwork savedNetworks[MAX_WIFI_NETWORKS] = {};
		{
			std::lock_guard<std::mutex> lock(wifiNetworksMutex);
			memcpy(savedNetworks, savedWiFiNetworks, sizeof(savedNetworks));
		}

		uint8_t savedWiFiCount = 0;
		int8_t onlySavedWiFiIndex = -1;
		for (uint8_t savedNetworkIndex = 0; savedNetworkIndex < MAX_WIFI_NETWORKS; savedNetworkIndex++) {
			if (savedNetworks[savedNetworkIndex].ssid[0] != '\0') {
				savedWiFiCount++;
				onlySavedWiFiIndex = savedNetworkIndex;
			}
		}

		if (savedWiFiCount == 0) {
			return;
		}

		int8_t bestSavedWiFiIndex = -1;
		int32_t bestSignalStrength = INT32_MIN;

		if (savedWiFiCount == 1) {
			bestSavedWiFiIndex = onlySavedWiFiIndex;
		} else {
			int scanResult =
			    WiFi.scanNetworks(/*async=*/false, /*hidden=*/false, /*passive=*/false, /*max_ms_per_channel=*/150);

			if (scanResult >= 0) {
				for (uint8_t selectionPass = 0; selectionPass < 2 && bestSavedWiFiIndex < 0; selectionPass++) {
					if (selectionPass == 1) {
						// All visible saved networks have failed; begin a new retry cycle.
						wifiAttemptedMask = 0;
					}

					for (int scanResultIndex = 0; scanResultIndex < scanResult; scanResultIndex++) {
						const String scannedSsid = WiFi.SSID(scanResultIndex);
						for (uint8_t savedNetworkIndex = 0; savedNetworkIndex < MAX_WIFI_NETWORKS;
						     savedNetworkIndex++) {
							if (savedNetworks[savedNetworkIndex].ssid[0] != '\0'
							    && (wifiAttemptedMask & (1U << savedNetworkIndex)) == 0
							    && scannedSsid == savedNetworks[savedNetworkIndex].ssid
							    && WiFi.RSSI(scanResultIndex) > bestSignalStrength) {
								bestSavedWiFiIndex = savedNetworkIndex;
								bestSignalStrength = WiFi.RSSI(scanResultIndex);
							}
						}
					}
				}
			}

			WiFi.scanDelete();

			if (bestSavedWiFiIndex < 0) {
				if (scanResult >= 0) {
					Serial.println("No saved WiFi networks are available");
				} else {
					Serial.printf("WiFi scan failed: %i\n", scanResult);
				}
			}
		}

		if (bestSavedWiFiIndex >= 0) {
			wifiNetworkIndex = bestSavedWiFiIndex;
			wifiAttemptedMask |= 1U << wifiNetworkIndex;  // Mark this network as attempted in the current retry cycle
			Serial.printf("Attempting to connect to saved network %i: %s",
			              wifiNetworkIndex,
			              savedNetworks[wifiNetworkIndex].ssid);
			if (bestSignalStrength != INT32_MIN) {
				Serial.printf("(%ld dBm)\n", bestSignalStrength);
			} else {
				Serial.println();
			}
			WiFi.disconnect();  // Disconnect from any current network
			WiFi.begin(savedNetworks[wifiNetworkIndex].ssid, savedNetworks[wifiNetworkIndex].password);
			WiFi.setTxPower(WIFI_POWER_15dBm);  // Set WiFi power to avoid interference
		}
	}
}
