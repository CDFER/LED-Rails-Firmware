#include "network.h"

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

bool NetworkManager::begin() {

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

	// Use hardcoded values that match what was previously used.
	// FIRMWARE, FIRMWARE_VERSION, ARDUINO_BOARD need to be defined.
	// They should be available if we include Arduino.h or any config header.
	// But let's verify if main.cpp or somewhere else defines them.
	// Often they are passed in via build flags (platformio.ini).
	improvSerial.setDeviceInfo(chip, FIRMWARE, FIRMWARE_VERSION, ARDUINO_BOARD, "http://{LOCAL_IPV4}/");

	// ImprovWiFiLibrary typically needs a standard function pointer or std::function.
	// We'll use lambda wrappers or static methods.
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

	// Create tasks
	xTaskCreate(improvSerialTask, "Improv Serial Task", 4096, this, 3, nullptr);
	xTaskCreate(networkTask, "Network Task", 4096, this, 2, nullptr);

	return savedWifiFound;
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

void NetworkManager::networkTask(void* pvParameters) {
	NetworkManager* manager = static_cast<NetworkManager*>(pvParameters);

	while (true) {
		manager->manageWiFiConnection();
		vTaskDelay(pdMS_TO_TICKS(100));	 // Sleep bit to yield to other tasks
	}
}

void NetworkManager::manageWiFiConnection() {
	if (WiFi.status() == WL_CONNECTED) {
		return;
	}

	const unsigned long attemptTimeout = 5000;	// 5 seconds for each attempt
	const uint8_t maxAttempts = 3;				// Max attempts per network

	if (millis() - lastWiFiConnectAttempt > attemptTimeout || lastWiFiConnectAttempt == 0) {
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

		lastWiFiConnectAttempt = millis();
	}
}
