/**
 * @file network.h
 * @brief WiFi connection management, map-data snapshots, and web services.
 */

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <FastLED.h>
#include <HTTPClient.h>
#include <ImprovWiFiLibrary.h>
#include <Preferences.h>
#include <WiFi.h>
#include <atomic>
#include <esp_sntp.h>
#include <memory>
#include <mutex>
#include <vector>

/// Maximum SSID length in bytes, excluding the null terminator.
#define MAX_SSID_LEN 32
/// Maximum WiFi password length in bytes, excluding the null terminator.
#define MAX_PASS_LEN 64
/// Maximum number of credential records retained in NVS.
#define MAX_WIFI_NETWORKS 16

/**
 * @brief Network activity required by the selected display mode.
 */
enum class NetworkMode {
	REALTIME,   ///< Fetch map data and show both status LEDs.
	TIME_ONLY,  ///< Skip map fetches and show only the WiFi status LED.
	OFF         ///< Stop map fetches and turn status LEDs off.
};

/**
 * @brief One scheduled transition between two LED map blocks.
 */
struct LedUpdate {
	uint16_t preBlock;   ///< Block shown before the transition.
	uint16_t postBlock;  ///< Block shown after the transition.
	int colorId;         ///< Index into MapData::colorTable.
	time_t timestamp;    ///< Unix timestamp at which the transition occurs.
	uint16_t msOffset;   ///< Millisecond offset within timestamp's second.
};

/**
 * @brief Immutable snapshot of the map data used by the renderer.
 *
 * A new snapshot is built when a JSON update is parsed and swapped into the
 * network manager under a mutex. Readers receive a shared pointer so an old
 * snapshot remains valid while a frame is being rendered.
 */
struct MapData {
	std::vector<CRGB> colorTable;              ///< Colors referenced by scheduled updates.
	std::vector<LedUpdate> ledUpdateSchedule;  ///< Chronologically scheduled block transitions.
};

/**
 * @brief Null-terminated SSID and password stored for reconnection attempts.
 */
struct SavedWiFiNetwork {
	char ssid[MAX_SSID_LEN + 1];      ///< SSID, including space for the null terminator.
	char password[MAX_PASS_LEN + 1];  ///< Password, including space for the null terminator.
};

/**
 * @brief Manages WiFi reconnection, map downloads, web services, and provisioning.
 *
 * The manager runs its connection and Improv serial loops in FreeRTOS tasks.
 * Map data is published as immutable shared snapshots so the renderer can
 * consume it without holding the network task's mutable state.
 */
class NetworkManager {
  public:
	/**
	 * @brief Construct an empty network manager and initialize its map snapshot.
	 */
	NetworkManager();

	/**
	 * @brief Load credentials, configure services, and start network tasks.
	 */
	void begin();

	/**
	 * @brief Publish the display mode and power state required by the main loop.
	 * @param mode Network behavior required by the current display mode.
	 * @param statusLedsEnabled True when diagnostic LEDs may be shown.
	 */
	void setSystemState(NetworkMode mode, bool statusLedsEnabled);

	/**
	 * @brief Return a shared, thread-safe snapshot of the latest parsed map data.
	 */
	std::shared_ptr<MapData> getMapData();

	/**
	 * @brief Return the latest connection status recorded by the network task.
	 */
	bool isConnected() const {
		return wifiConnected;
	}

	/**
	 * @brief Add or update a saved WiFi network without exposing its password.
	 * @return True when the supplied UTF-8 credential lengths are valid.
	 */
	bool saveWiFiNetwork(const String& ssid, const String& password);

	/**
	 * @brief Remove a saved WiFi network by SSID.
	 * @return True when a saved network was removed.
	 */
	bool forgetWiFiNetwork(const String& ssid);

	/** @brief Return the saved SSIDs without their passwords. */
	std::vector<String> getSavedWiFiNetworkNames();

  private:
	SavedWiFiNetwork savedWiFiNetworks[MAX_WIFI_NETWORKS];  ///< Saved credentials tried during reconnection.
	std::mutex wifiNetworksMutex;                           ///< Protects saved WiFi credentials across tasks.
	uint8_t wifiNetworkIndex = 0;                           ///< Index of the credential currently being attempted.
	uint16_t wifiAttemptedMask = 0;                         ///< Credentials already tried in the current retry cycle.
	TickType_t lastWiFiConnectAttempt = 0;                  ///< Tick count of the last connection attempt or scan.
	std::atomic<bool> wifiReconnectRequested{ false };      ///< Requests a reconnect from a web API update.

	ImprovWiFi improvSerial;  ///< Improv WiFi serial provisioning instance.
	AsyncWebServer server;    ///< Asynchronous configuration web server on port 80.
	Preferences preferences;  ///< NVS preferences used for saved WiFi credentials.

	uint8_t failedFetchCount = 0;  ///< Consecutive map-fetch failures.
	uint8_t updateInterval = 30;   ///< Map-fetch interval in seconds from the server response.
	time_t nextFetchTime = 0;      ///< Unix timestamp at which the next map fetch is due.
	time_t lastFetchTime = 0;      ///< Unix timestamp of the last successful map fetch.
	TickType_t fetchOffset = 0;    ///< Random ticks offset used to spread server load.
	TickType_t fetchDueTick = 0;   ///< Monotonic deadline for the currently pending map fetch.
	bool fetchScheduled = false;   ///< True when a fetch deadline has been scheduled.

	NetworkMode currentMode = NetworkMode::REALTIME;  ///< Latest requested network mode.
	bool statusLedsEnabled = true;                    ///< Whether status LEDs may be displayed.
	bool wifiConnected = false;                       ///< Latest value observed from WiFi.status().

	std::shared_ptr<MapData> currentMapData;  ///< Published immutable map snapshot.
	std::mutex mapDataMutex;                  ///< Protects replacement of currentMapData.

	std::vector<String> serverUrls;  ///< Map endpoints tried during fetch failover.
	uint8_t currentServerIndex = 0;  ///< Index of the endpoint used for the next fetch.

	/** @brief Update diagnostic LEDs from current network and display state. */
	void updateStatusLeds();

	/** @brief Persist saved WiFi credentials to NVS. */
	void exportWiFi();

	/**
	 * @brief Load saved WiFi credentials from NVS, including legacy records.
	 */
	void importWiFi();

	/**
	 * @brief Register HTTP handlers and start the asynchronous web server.
	 */
	void setupWebServer();
	/** @brief Schedule a saved-network reconnect from the network task. */
	void requestWiFiReconnect();

	/**
	 * @brief Scan or attempt saved credentials when WiFi is disconnected.
	 *
	 * A single saved network is attempted directly; multiple saved networks are
	 * selected from a scan and retried in RSSI order.
	 */
	void manageWiFiConnection();

	/**
	 * @brief FreeRTOS task for processing Improv WiFi serial communication.
	 * 
	 * @param pvParameters Pointer to the NetworkManager instance.
	 */
	static void improvSerialTask(void* pvParameters);

	/**
	 * @brief FreeRTOS task for handling general network connectivity.
	 * 
	 * @param pvParameters Pointer to the NetworkManager instance.
	 */
	static void networkTask(void* pvParameters);

	/**
	 * @brief Restart the web server after an Improv WiFi error.
	 * 
	 * @param error The Improv error that occurred.
	 */
	void onImprovWiFiErrorCallback(ImprovTypes::Error error);

	/**
	 * @brief Save credentials received from Improv WiFi and restart the server.
	 * 
	 * @param ssid The connected network's SSID.
	 * @param password The connected network's password.
	 */
	void onImprovWiFiConnectedCallback(const char* ssid, const char* password);

	/** @brief Fetch the current map JSON from the selected endpoint. */
	String fetchMapUpdateJson();
	/** @brief Parse map JSON and publish a new immutable snapshot. */
	time_t parseLedMapUpdateJson(const String& downloadedJson);
	/** @brief Fetch map updates when the current network mode requires them. */
	void manageLedMapApi();
	/** @brief Format a Unix timestamp as local HH:MM:SS text. */
	const char* formatEpoch(time_t epoch) const;
	/** @brief Return the current local time as HH:MM:SS.mmm text. */
	const char* getFormattedTimeWithMs() const;
};

/** @brief Global network manager instance. */
extern NetworkManager network;
