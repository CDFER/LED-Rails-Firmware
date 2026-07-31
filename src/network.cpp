#include "network.h"
#include "ledStorageSync.h"
#include "statusLeds.h"

NetworkManager network;

namespace {
struct LegacySavedWiFiNetwork {
	char ssid[MAX_SSID_LEN];
	char password[MAX_PASS_LEN];
};

void copyUtf8(char* destination, size_t destinationSize, const char* source) {
	if (destinationSize == 0) {
		return;
	}

	size_t length = strnlen(source, destinationSize - 1);
	if (source[length] != '\0') {
		length = destinationSize - 1;
		while (length > 0 && (static_cast<uint8_t>(source[length]) & 0xC0) == 0x80) {
			--length;
		}
	}

	memcpy(destination, source, length);
	destination[length] = '\0';
}
}

static void onNtpTimeSyncCallback(struct timeval* timeValue) {
	(void)timeValue;
	Serial.println("Time synced via NTP");
}

const char index_html[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>LED Rails Device</title>
  <style>
    body {
      background: #222;
      color: #fff;
      font-family: -apple-system, system-ui, BlinkMacSystemFont, "Segoe UI", Roboto, Ubuntu, sans-serif;
      margin: 0;
      padding: 0;
      min-height: 100vh;
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
    }
    .container {
      background: #222;
      border-radius: 12px;
      box-shadow: 0 2px 12px rgba(0,0,0,0.08);
      padding: 32px 24px;
      max-width: 600px;
      width: 90%;
      text-align: center;
    }
    h1 {
      color: #fff;
      font-family: inherit;
      margin-bottom: 16px;
    }
    h2 {
      color: #fff;
      font-family: inherit;
      font-weight: 400;
      margin-top: 0;
    }
    @media (max-width: 600px) {
      .container { padding: 18px 4px; }
      h1 { font-size: 1.6em; }
      h2 { font-size: 1.1em; }
    }
  </style>
</head>
<body>
  <div class="container">
    <h1>Kea Studios</h1>
    <h2>This is just an empty page for now, in the future settings will be added here...</h2>
  </div>
</body>
</html>
)=====";

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
	xSemaphoreTake(fastLEDPreferencesMutex, portMAX_DELAY);
	preferences.begin("wifi");
	preferences.putBytes("wifi", savedWiFiNetworks, sizeof(savedWiFiNetworks));
	preferences.end();
	xSemaphoreGive(fastLEDPreferencesMutex);
}

void NetworkManager::importWiFi() {
	xSemaphoreTake(fastLEDPreferencesMutex, portMAX_DELAY);
	preferences.begin("wifi", true);
	const size_t storedSize = preferences.getBytesLength("wifi");
	if (storedSize == sizeof(LegacySavedWiFiNetwork) * MAX_WIFI_NETWORKS) {
		LegacySavedWiFiNetwork legacyNetworks[MAX_WIFI_NETWORKS] = {};
		preferences.getBytes("wifi", legacyNetworks, sizeof(legacyNetworks));
		for (int networkIndex = 0; networkIndex < MAX_WIFI_NETWORKS; networkIndex++) {
			copyUtf8(savedWiFiNetworks[networkIndex].ssid,
			         sizeof(savedWiFiNetworks[networkIndex].ssid),
			         legacyNetworks[networkIndex].ssid);
			copyUtf8(savedWiFiNetworks[networkIndex].password,
			         sizeof(savedWiFiNetworks[networkIndex].password),
			         legacyNetworks[networkIndex].password);
		}
	} else {
		preferences.getBytes("wifi", savedWiFiNetworks, sizeof(savedWiFiNetworks));
		for (int networkIndex = 0; networkIndex < MAX_WIFI_NETWORKS; networkIndex++) {
			savedWiFiNetworks[networkIndex].ssid[MAX_SSID_LEN] = '\0';
			savedWiFiNetworks[networkIndex].password[MAX_PASS_LEN] = '\0';
		}
	}
	preferences.end();
	xSemaphoreGive(fastLEDPreferencesMutex);
}

void NetworkManager::onImprovWiFiErrorCallback(ImprovTypes::Error error) {
	(void)error;
	// Serial.printf("Improv WiFi Error: %d\n", error);
	server.end();
	server.begin();
}

void NetworkManager::onImprovWiFiConnectedCallback(const char* ssid, const char* password) {
	// Move the networks all down one position
	for (int networkIndex = MAX_WIFI_NETWORKS - 1; networkIndex > 0; networkIndex--) {
		strncpy(savedWiFiNetworks[networkIndex].ssid, savedWiFiNetworks[networkIndex - 1].ssid, MAX_SSID_LEN);
		strncpy(savedWiFiNetworks[networkIndex].password, savedWiFiNetworks[networkIndex - 1].password, MAX_PASS_LEN);
	}

	// Save the new network at the top
	copyUtf8(savedWiFiNetworks[0].ssid, sizeof(savedWiFiNetworks[0].ssid), ssid);
	copyUtf8(savedWiFiNetworks[0].password, sizeof(savedWiFiNetworks[0].password), password);

	// Save the updated WiFi networks to Preferences
	exportWiFi();

	// Restart the web server
	server.end();
	server.begin();
}

void NetworkManager::setupWebServer() {
	server.on("/favicon.ico", [](AsyncWebServerRequest* request) {
		request->send(404);
	});

	// Serve Basic HTML Page
	server.on("/", HTTP_ANY, [](AsyncWebServerRequest* request) {
		AsyncWebServerResponse* response = request->beginResponse(200, "text/html", index_html);
		response->addHeader(
		    "Cache-Control", "public,max-age=31536000");  // save this file to cache for 1 year (unless you refresh)
		request->send(response);
		Serial.println("Served Basic HTML Page");
	});

	server.onNotFound([](AsyncWebServerRequest* request) {
		request->send(404);
	});

	server.begin();
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

	bool savedWiFiFound = false;
	for (int savedNetworkIndex = 0; savedNetworkIndex < MAX_WIFI_NETWORKS; savedNetworkIndex++) {
		if (strlen(savedWiFiNetworks[savedNetworkIndex].ssid) > 0) {
			savedWiFiFound = true;
			break;
		}
	}

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
	fetchOffset = random(0, 999);  // Random delay between 0 and 999 ms to reduce server load
#endif

	// Create tasks
	xTaskCreatePinnedToCore(improvSerialTask, "Improv Serial Task", 4096, this, 3, nullptr, ARDUINO_RUNNING_CORE);
	xTaskCreatePinnedToCore(networkTask, "Network Task", 16384, this, 2, nullptr, ARDUINO_RUNNING_CORE);
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

	httpClient.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
	httpClient.begin(url);

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
		return;
	}

	if (currentMode == NetworkMode::TIME_ONLY) {
		if (wifiConnected) {
			statusLeds.setState(WIFI_LED_PIN, LED_ON_GREEN, SERVER_LED_PIN, LED_OFF);
		} else if (millis() > 60 * 1000) {
			statusLeds.setState(WIFI_LED_PIN, LED_ON_RED, SERVER_LED_PIN, LED_OFF);
		}
	} else if (currentMode == NetworkMode::REALTIME) {
		if (wifiConnected) {
			if (failedFetchCount > 3 + serverUrls.size()) {
				statusLeds.setState(WIFI_LED_PIN, LED_ON_GREEN, SERVER_LED_PIN, LED_ON_RED);
			} else if (epoch > nextFetchTime + updateInterval) {
				statusLeds.setState(WIFI_LED_PIN, LED_ON_GREEN, SERVER_LED_PIN, LED_BLINK_GREEN_FAST);
			} else {
				statusLeds.setState(WIFI_LED_PIN, LED_ON_GREEN, SERVER_LED_PIN, LED_ON_GREEN);
			}
		} else {
			if (millis() > 60 * 1000) {
				statusLeds.setState(WIFI_LED_PIN, LED_ON_RED, SERVER_LED_PIN, LED_OFF);
			}
		}
	}
}

void NetworkManager::manageLedMapApi() {
	time_t epoch = time(nullptr);  // Get current time

	// Update nextFetchTime to account for time drift
	nextFetchTime = constrain(nextFetchTime, epoch - 1, epoch + updateInterval);

	if (epoch > nextFetchTime && millis() % 1000 > fetchOffset) {
		time_t timeOffset = 0;
		String downloadedJson = fetchMapUpdateJson();
		if (downloadedJson.length() > 0) {
			timeOffset = epoch - parseLedMapUpdateJson(downloadedJson);
			nextFetchTime = constrain(nextFetchTime, epoch + 2, epoch + updateInterval);
		} else {
			if (failedFetchCount > 3 + serverUrls.size()) {
				Serial.println("All servers failed to provide data.");
			}
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
}

void NetworkManager::manageWiFiConnection() {
	const TickType_t attemptTimeout = pdMS_TO_TICKS(30000);  // 30 seconds between scans/attempts

	if (xTaskGetTickCount() - lastWiFiConnectAttempt > attemptTimeout || lastWiFiConnectAttempt == 0) {
		lastWiFiConnectAttempt = xTaskGetTickCount();
		uint8_t savedWiFiCount = 0;
		int8_t onlySavedWiFiIndex = -1;
		for (uint8_t savedNetworkIndex = 0; savedNetworkIndex < MAX_WIFI_NETWORKS; savedNetworkIndex++) {
			if (savedWiFiNetworks[savedNetworkIndex].ssid[0] != '\0') {
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
							if (savedWiFiNetworks[savedNetworkIndex].ssid[0] != '\0'
							    && (wifiAttemptedMask & (1U << savedNetworkIndex)) == 0
							    && scannedSsid == savedWiFiNetworks[savedNetworkIndex].ssid
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
			              savedWiFiNetworks[wifiNetworkIndex].ssid);
			if (bestSignalStrength != INT32_MIN) {
				Serial.printf("(%ld dBm)\n", bestSignalStrength);
			} else {
				Serial.println();
			}
			WiFi.disconnect();  // Disconnect from any current network
			WiFi.begin(savedWiFiNetworks[wifiNetworkIndex].ssid, savedWiFiNetworks[wifiNetworkIndex].password);
			WiFi.setTxPower(WIFI_POWER_15dBm);  // Set WiFi power to avoid interference
		}
	}
}
