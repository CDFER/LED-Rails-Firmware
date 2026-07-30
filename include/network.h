#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <FastLED.h>
#include <HTTPClient.h>
#include <ImprovWiFiLibrary.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <memory>
#include <mutex>
#include <vector>

/// Maximum length for a WiFi SSID
#define MAX_SSID_LEN 32
/// Maximum length for a WiFi password
#define MAX_PASS_LEN 64
/// Maximum number of saved WiFi networks
#define MAX_WIFI_NETWORKS 16

enum class NetworkMode {
	REALTIME,	// Fetch map data, show both LEDs
	TIME_ONLY,	// No map fetch, show Wifi LED only
	OFF			// No fetches, LEDs off
};

// --- Data structure for scheduled LED updates ---
struct LedUpdate {
	uint16_t preBlock;
	uint16_t postBlock;
	int colorId;
	time_t timestamp;	// Timestamp for when the update should occur
	uint16_t msOffset;	// Millisecond offset for precise timing within the second
};

// Immutable snapshot of map data
struct MapData {
	std::vector<CRGB> colorTable;
	std::vector<LedUpdate> ledUpdateSchedule;
};

/**
 * @brief Structure representing saved WiFi credentials
 * 
 * This struct is used to store SSID and password combinations
 * that can be retrieved and tried during connection.
 */
struct savedWiFiNetwork {
	char ssid[MAX_SSID_LEN + 1];	  // The SSID of the WiFi network
	char password[MAX_PASS_LEN + 1];  // The password of the WiFi network
};

/**
 * @brief Network manager class for handling WiFi connectivity and web services
 * 
 * This class provides a centralized interface for managing WiFi connections,
 * falling back to saved credentials, hosting a web server for configuration,
 * and supporting Improv WiFi provisioning over serial.
 */
class NetworkManager {
  public:
	/**
	 * @brief Construct a new Network Manager object
	 */
	NetworkManager();

	/**
	 * @brief Initialize the Network Manager
	 * 
	 * Starts background tasks for network management and Improv Wi-Fi, 
	 * and initializes checking for saved credentials.
	 */
	void begin();

	// Set the current desired state from the main loop
	void setSystemState(NetworkMode mode, bool brightnessOn);

	// Get a thread-safe snapshot of the latest parsed map data
	std::shared_ptr<MapData> getMapData();

	// Thread-safe check if WiFi is connected
	bool isConnected() const {
		return wifiConnected;
	}

  private:
	savedWiFiNetwork savedWiFi[MAX_WIFI_NETWORKS];	// Array of saved WiFi network credentials
	uint8_t wifiNetworkIndex = 0;					// Current index for iterating through saved networks
	uint16_t wifiAttemptedMask = 0;					// Saved networks already attempted in the current retry cycle
	TickType_t lastWiFiConnectAttempt = 0;			// Timestamp of the last WiFi connection attempt
	uint8_t wifiConnectAttempts = 0;				// Counter for WiFi connection attempts

	ImprovWiFi improvSerial;  // Instance handling Improv WiFi serial protocol
	AsyncWebServer server;	  // Asynchronous web server (port 80)
	Preferences preferences;  // NVS preferences for saving/loading credentials

	uint8_t failedFetchCount = 0;
	uint8_t updateInterval = 30;  // Default update interval in seconds
	time_t nextFetchTime = 0;	  // Tracks when the next update should occur
	uint16_t fetchOffset = 0;	  // Random time ms to fetch (reduces server load)

	NetworkMode currentMode = NetworkMode::REALTIME;
	bool isBrightnessOn = true;
	bool wifiConnected = false;

	std::shared_ptr<MapData> currentMapData;
	std::mutex mapDataMutex;

	// Array of server URLs for failover
	std::vector<String> serverURLs;
	uint8_t currentServerIndex = 0;

	void updateStatusLEDs();

	void exportWiFi();

	/**
	 * @brief Load saved WiFi credentials from NVS preferences
	 */
	void importWiFi();

	/**
	 * @brief Initialize and start the asynchronous web server
	 */
	void setUpWebserver();

	/**
	 * @brief Main loop for managing WiFi connection state
	 * 
	 * Checks connection status, attempts to reconnect if necessary,
	 * and cycles through saved credentials.
	 */
	void manageWiFiConnection();

	/**
	 * @brief FreeRTOS task for processing Improv Wi-Fi serial communication
	 * 
	 * @param pvParameters Pointer to the NetworkManager instance
	 */
	static void improvSerialTask(void* pvParameters);

	/**
	 * @brief FreeRTOS task for handling general network connectivity
	 * 
	 * @param pvParameters Pointer to the NetworkManager instance
	 */
	static void networkTask(void* pvParameters);

	/**
	 * @brief Internal callback for handling Improv WiFi errors
	 * 
	 * @param err The Improv error that occurred
	 */
	void onImprovWiFiErrorCb(ImprovTypes::Error err);

	/**
	 * @brief Internal callback for handling successful Improv WiFi connections
	 * 
	 * @param ssid The connected network's SSID
	 * @param password The connected network's password
	 */
	void onImprovWiFiConnectedCb(const char* ssid, const char* password);

	String fetchMapUpdateJSON();
	time_t parseLEDMapUpdateJSON(const String& downloadedJson);
	void manageLEDMapAPI();
	const char* formatEpoch(time_t epoch) const;
	const char* getFormattedTimeWithMs() const;
};

// Global instance of the NetworkManager
extern NetworkManager network;
