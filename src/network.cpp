#include "network.h"
#include "statusLeds.h"

NetworkManager* NetworkManager::instance = nullptr;
NetworkManager network;

static void onNTPTimeSyncCb(struct timeval* t) {
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
	instance = this;

	// Initialize savedWiFi to empty
	for (int i = 0; i < MAX_WIFI_NETWORKS; i++) {
		savedWiFi[i].ssid[0] = '\0';
		savedWiFi[i].password[0] = '\0';
	}

	currentMapData = std::make_shared<MapData>();

	serverURLs = {
		String("http://api.keastudios.co.nz/") + CITY_CODE + "-ltm/" + BACKEND_VERSION + ".json",
#if defined(BETA_BUILD)
		String("http://192.168.86.31:3000/") + CITY_CODE + "-ltm/" + BACKEND_VERSION
			+ ".json",	// For testing with a local server
#endif
		String("http://keastudios.co.nz/") + CITY_CODE + "-ltm/" + BACKEND_VERSION + ".json",
		String("http://dirksonline.net/") + CITY_CODE + "-ltm/" + BACKEND_VERSION + ".json",
	};
}

NetworkManager::~NetworkManager() {
	if (instance == this) {
		instance = nullptr;
	}
}

void NetworkManager::exportWiFi() {
	preferences.begin("wifi");
	preferences.putBytes("wifi", savedWiFi, sizeof(savedWiFi));
	preferences.end();
}

void NetworkManager::importWiFi() {
	preferences.begin("wifi", true);
	preferences.getBytes("wifi", savedWiFi, sizeof(savedWiFi));
	preferences.end();
}

void NetworkManager::onImprovWiFiErrorCbWrapper(ImprovTypes::Error err) {
	if (instance) {
		instance->onImprovWiFiErrorCb(err);
	}
}

void NetworkManager::onImprovWiFiConnectedCbWrapper(const char* ssid, const char* password) {
	if (instance) {
		instance->onImprovWiFiConnectedCb(ssid, password);
	}
}

void NetworkManager::onImprovWiFiErrorCb(ImprovTypes::Error err) {
	Serial.printf("Improv WiFi Error: %d\n", err);
	server.end();
	server.begin();
}

void NetworkManager::onImprovWiFiConnectedCb(const char* ssid, const char* password) {
	// Move the networks all down one position
	for (int i = MAX_WIFI_NETWORKS - 1; i > 0; i--) {
		strncpy(savedWiFi[i].ssid, savedWiFi[i - 1].ssid, MAX_SSID_LEN);
		strncpy(savedWiFi[i].password, savedWiFi[i - 1].password, MAX_PASS_LEN);
	}

	// Save the new network at the top
	strncpy(savedWiFi[0].ssid, ssid, MAX_SSID_LEN);
	strncpy(savedWiFi[0].password, password, MAX_PASS_LEN);

	// Save the updated WiFi networks to Preferences
	exportWiFi();

	// Restart the web server
	server.end();
	server.begin();
}

void NetworkManager::setUpWebserver() {
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

	// --- Time Setup ---
	const char* ntpServers[] = { "pool.ntp.org", "pool.msltime.measurement.govt.nz", "nz.pool.ntp.org" };
	const char* time_zone = "NZST-12NZDT,M9.5.0,M4.1.0/3";
	sntp_set_time_sync_notification_cb(onNTPTimeSyncCb);
	sntp_set_sync_interval(1000 * 60 * 15);	 // Set sync interval to 15 minutes
	sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
	configTzTime(time_zone, ntpServers[0], ntpServers[1], ntpServers[2]);

	importWiFi();

#if defined(CONFIG_IDF_TARGET_ESP32S2)
	enum ImprovTypes::ChipFamily chip = ImprovTypes::ChipFamily::CF_ESP32_S2;
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
	enum ImprovTypes::ChipFamily chip = ImprovTypes::ChipFamily::CF_ESP32_C3;
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
	enum ImprovTypes::ChipFamily chip = ImprovTypes::ChipFamily::CF_ESP32_S3;
#else
	// Default or add fallback if unknown chip
	enum ImprovTypes::ChipFamily chip = ImprovTypes::ChipFamily::CF_ESP32;
#endif

	improvSerial.setDeviceInfo(chip, FIRMWARE, FIRMWARE_VERSION, ARDUINO_BOARD, "http://{LOCAL_IPV4}/");
	improvSerial.onImprovError(onImprovWiFiErrorCbWrapper);
	improvSerial.onImprovConnected(onImprovWiFiConnectedCbWrapper);

	setUpWebserver();

	bool savedWifiFound = false;
	for (int i = 0; i < MAX_WIFI_NETWORKS; i++) {
		if (strlen(savedWiFi[i].ssid) > 0) {
			savedWifiFound = true;
			break;
		}
	}

	if (savedWifiFound) {
		Serial.println("WiFi credentials found...");
		statusLEDs.setState(WIFI_LED_PIN, LED_BLINK_GREEN_FAST);
	} else {
		Serial.println("No WiFi credentials found...");
		statusLEDs.setState(WIFI_LED_PIN, LED_ON_RED);
	}

#if defined(BETA_BUILD)
	fetchOffset = 0;  // No need for random offset in beta builds
#else
	fetchOffset = random(0, 999);  // Random delay between 0 and 999 ms to reduce server load
#endif

	// Create tasks
	xTaskCreate(improvSerialTask, "Improv Serial Task", 4096, this, 3, nullptr);
	xTaskCreate(networkTask, "Network Task", 16384, this, 2, nullptr);
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

String NetworkManager::fetchMapUpdateJSON() {
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
		currentServerIndex = (currentServerIndex + 1) % serverURLs.size();
	}
	return "";
}

time_t NetworkManager::parseLEDMapUpdateJSON(const String& downloadedJson) {
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
	for (JsonPair kv : colors) {
		JsonArray rgb = kv.value();
		newMapData->colorTable.push_back(CRGB(rgb[0] | 0, rgb[1] | 0, rgb[2] | 0));
	}

	JsonArray updates = doc["updates"];
	newMapData->ledUpdateSchedule.reserve(updates.size());
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

		newMapData->ledUpdateSchedule.push_back({ update["b"][0], update["b"][1], update["c"], timestamp, msOffset });
	}

	{
		std::lock_guard<std::mutex> lock(mapDataMutex);
		currentMapData = newMapData;
	}

	return baseTimestamp;
}

void NetworkManager::setSystemState(NetworkMode mode, bool brightnessOn) {
	this->currentMode = mode;
	this->isBrightnessOn = brightnessOn;
}

std::shared_ptr<MapData> NetworkManager::getMapData() {
	std::lock_guard<std::mutex> lock(mapDataMutex);
	return currentMapData;
}

const char* NetworkManager::formatEpoch(time_t epoch) const {
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

void NetworkManager::networkTask(void* pvParameters) {
	NetworkManager* manager = static_cast<NetworkManager*>(pvParameters);

	while (true) {
		manager->updateStatusLEDs();

		if (WiFi.status() == WL_CONNECTED) {
			if (manager->currentMode == NetworkMode::REALTIME) {
				manager->manageLEDMapAPI();
			}
		} else {
			manager->manageWiFiConnection();
		}
		vTaskDelay(pdMS_TO_TICKS(100));	 // Sleep bit to yield to other tasks
	}
}

void NetworkManager::updateStatusLEDs() {
	wifiConnected = (WiFi.status() == WL_CONNECTED);
	time_t epoch = time(nullptr);

	if (!isBrightnessOn || currentMode == NetworkMode::OFF) {
		statusLEDs.setState(WIFI_LED_PIN, LED_OFF, SERVER_LED_PIN, LED_OFF);
		return;
	}

	if (currentMode == NetworkMode::TIME_ONLY) {
		if (wifiConnected) {
			statusLEDs.setState(WIFI_LED_PIN, LED_ON_GREEN, SERVER_LED_PIN, LED_OFF);
		} else if (millis() > 60 * 1000) {
			statusLEDs.setState(WIFI_LED_PIN, LED_ON_RED, SERVER_LED_PIN, LED_OFF);
		}
	} else if (currentMode == NetworkMode::REALTIME) {
		if (wifiConnected) {
			if (failedFetchCount > 3 + serverURLs.size()) {
				statusLEDs.setState(WIFI_LED_PIN, LED_ON_GREEN, SERVER_LED_PIN, LED_ON_RED);
			} else if (epoch > nextFetchTime + updateInterval) {
				statusLEDs.setState(WIFI_LED_PIN, LED_ON_GREEN, SERVER_LED_PIN, LED_BLINK_GREEN_FAST);
			} else {
				statusLEDs.setState(WIFI_LED_PIN, LED_ON_GREEN, SERVER_LED_PIN, LED_ON_GREEN);
			}
		} else {
			if (millis() > 60 * 1000) {
				statusLEDs.setState(WIFI_LED_PIN, LED_ON_RED, SERVER_LED_PIN, LED_OFF);
			}
		}
	}
}

void NetworkManager::manageLEDMapAPI() {
	time_t epoch = time(nullptr);  // Get current time
	if (epoch > nextFetchTime && millis() % 1000 > fetchOffset) {
		time_t timeOffset = 0;
		String downloadedJson = fetchMapUpdateJSON();
		if (downloadedJson.length() > 0) {
			timeOffset = epoch - parseLEDMapUpdateJSON(downloadedJson);
		} else {
			if (failedFetchCount > 3 + serverURLs.size()) {
				Serial.println("All servers failed to provide data.");
			}
		}
		nextFetchTime = constrain(nextFetchTime, epoch + 2, epoch + updateInterval);
		Serial.printf("%s fetchDelay:%2is size:%1.1fkiB MCU:%2.0f°C WiFi:%2idBm\n",
					  formatEpoch(epoch),
					  timeOffset,
					  downloadedJson.length() / 1024.0,
					  temperatureRead(),
					  WiFi.RSSI());
	}
}

void NetworkManager::manageWiFiConnection() {
	const TickType_t attemptTimeout = pdMS_TO_TICKS(5000);	// 5 seconds for each attempt
	const uint8_t maxAttempts = 3;							// Max attempts per network

	if (xTaskGetTickCount() - lastWiFiConnectAttempt > attemptTimeout || lastWiFiConnectAttempt == 0) {
		if (wifiConnectAttempts < maxAttempts) {
			wifiConnectAttempts++;
		} else {
			wifiConnectAttempts = 0;

			while (strlen(savedWiFi[wifiNetworkIndex].ssid) == 0 && wifiNetworkIndex < MAX_WIFI_NETWORKS) {
				wifiNetworkIndex++;	 // Skip empty SSIDs
			}

			if (wifiNetworkIndex >= MAX_WIFI_NETWORKS) {
				wifiNetworkIndex = 0;
			}
		}

		if (strlen(savedWiFi[wifiNetworkIndex].ssid) != 0) {
			// Attempt to connect to the current network
			Serial.printf("Attempting to connect to saved network %i: %s\n", wifiNetworkIndex, savedWiFi[wifiNetworkIndex].ssid);
			WiFi.disconnect();	// Disconnect from any current network
			WiFi.begin(savedWiFi[wifiNetworkIndex].ssid, savedWiFi[wifiNetworkIndex].password);
			WiFi.setTxPower(WIFI_POWER_15dBm);	// Set WiFi power to avoid interference
		}

		lastWiFiConnectAttempt = xTaskGetTickCount();
	}
}
