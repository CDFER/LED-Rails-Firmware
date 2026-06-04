#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ImprovWiFiLibrary.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_sntp.h>

/// Maximum length for a WiFi SSID
#define MAX_SSID_LEN 32
/// Maximum length for a WiFi password
#define MAX_PASS_LEN 64
/// Maximum number of saved WiFi networks
#define MAX_WIFI_NETWORKS 16

/**
 * @brief Structure representing saved WiFi credentials
 * 
 * This struct is used to store SSID and password combinations
 * that can be retrieved and tried during connection.
 */
struct savedWiFiNetwork {
	char ssid[MAX_SSID_LEN];	  // The SSID of the WiFi network
	char password[MAX_PASS_LEN];  // The password of the WiFi network
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
	 * @brief Destroy the Network Manager object
	 */
	~NetworkManager();

	/**
	 * @brief Initialize the Network Manager
	 * 
	 * Starts background tasks for network management and Improv Wi-Fi, 
	 * and initializes checking for saved credentials.
	 * 
	 * @return true if saved WiFi credentials were found, false otherwise
	 */
	bool begin();

  private:
	savedWiFiNetwork savedWiFi[MAX_WIFI_NETWORKS];	// Array of saved WiFi network credentials
	int wifiNetworkIndex = 0;						// Current index for iterating through saved networks
	unsigned long lastWiFiConnectAttempt = 0;		// Timestamp of the last WiFi connection attempt
	uint8_t wifiConnectAttempts = 0;				// Counter for WiFi connection attempts

	ImprovWiFi improvSerial;  // Instance handling Improv WiFi serial protocol
	AsyncWebServer server;	  // Asynchronous web server (port 80)
	Preferences preferences;  // NVS preferences for saving/loading credentials

	/**
	 * @brief Save the current WiFi credentials to NVS preferences
	 */
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
	 * @brief Static wrapper for Improv WiFi error callback
	 * 
	 * @param err The Improv error that occurred
	 */
	static void onImprovWiFiErrorCbWrapper(ImprovTypes::Error err);

	/**
	 * @brief Static wrapper for Improv WiFi connected callback
	 * 
	 * @param ssid The connected network's SSID
	 * @param password The connected network's password
	 */
	static void onImprovWiFiConnectedCbWrapper(const char* ssid, const char* password);

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

	/// Static instance pointer used for routing callbacks if lambdas aren't supported
	static NetworkManager* instance;
};

// Global instance of the NetworkManager
extern NetworkManager network;
