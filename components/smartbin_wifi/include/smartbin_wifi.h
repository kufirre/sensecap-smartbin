#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <esp_err.h>
#include <stdbool.h>

/**
 * @brief WiFi operating modes
 */
typedef enum {
    SMARTBIN_WIFI_MODE_CONFIG,  // AP mode for configuration
    SMARTBIN_WIFI_MODE_NORMAL   // STA mode for normal operation
} smartbin_wifi_mode_t;

/**
 * @brief WiFi connection states
 */
typedef enum {
    SMARTBIN_WIFI_STATE_DISCONNECTED,
    SMARTBIN_WIFI_STATE_CONNECTING,
    SMARTBIN_WIFI_STATE_CONNECTED,
    SMARTBIN_WIFI_STATE_AP_MODE
} smartbin_wifi_state_t;

/**
 * @brief Initialize WiFi management system
 * 
 * Sets up WiFi networking infrastructure including:
 * - Network interfaces (STA and AP)
 * - Event handlers
 * - Monitoring task
 * 
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t smartbin_wifi_init(void);

/**
 * @brief Start WiFi in AP mode for device configuration
 * 
 * Creates a configuration portal accessible via WiFi hotspot.
 * SSID format: "SenseCAP-SmartBin-XXXX" where XXXX are MAC bytes.
 * 
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t smartbin_wifi_start_ap_mode(void);

/**
 * @brief Start WiFi in STA mode to connect to existing network
 * 
 * @param ssid Target WiFi network SSID
 * @param password Network password (can be NULL for open networks)
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t smartbin_wifi_start_sta_mode(const char* ssid, const char* password);

/**
 * @brief Connect to WiFi using stored or default configuration
 * 
 * Automatically chooses between STA mode (if credentials available)
 * or AP mode (for initial configuration).
 * 
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t smartbin_wifi_connect(void);

/**
 * @brief Wait for WiFi connection with timeout
 * 
 * @param timeout_ms Maximum time to wait in milliseconds
 * @return ESP_OK if connected, ESP_ERR_TIMEOUT if timeout reached
 */
esp_err_t smartbin_wifi_wait_connection(uint32_t timeout_ms);

/**
 * @brief Get current WiFi connection state
 * 
 * @return Current WiFi state
 */
smartbin_wifi_state_t smartbin_wifi_get_state(void);

/**
 * @brief Check if WiFi is connected to internet
 * 
 * @return true if connected, false otherwise
 */
bool smartbin_wifi_is_connected(void);

/**
 * @brief Get current IP address (STA mode)
 * 
 * @return IP address string or empty string if not connected
 */
char* smartbin_wifi_get_ip_address(void);

/**
 * @brief Get AP mode IP address
 * 
 * @return AP IP address (typically "192.168.4.1")
 */
char* smartbin_wifi_get_ap_ip_address(void);

/**
 * @brief Check and switch WiFi mode based on new configuration
 * 
 * Called after webserver receives new WiFi credentials.
 * Switches from AP mode to STA mode if credentials are available.
 */
void smartbin_wifi_check_and_switch_mode(void);

#ifdef __cplusplus
}
#endif