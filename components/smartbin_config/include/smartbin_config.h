#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <esp_err.h>
#include <stdbool.h>

// Configuration keys
#define SMARTBIN_CONFIG_OPENAI_API_KEY_MAX_LEN 200
#define SMARTBIN_CONFIG_WIFI_SSID_MAX_LEN      32
#define SMARTBIN_CONFIG_WIFI_PASSWORD_MAX_LEN  64

/**
 * @brief Initialize the configuration system
 * 
 * Sets up secure storage and encryption for configuration data.
 * Must be called before any other config functions.
 * 
 * @return ESP_OK on success, error code on failure
 */
esp_err_t smartbin_config_init(void);

/**
 * @brief Deinitialize the configuration system
 * 
 * Cleans up resources and clears sensitive data from memory.
 * 
 * @return ESP_OK on success
 */
esp_err_t smartbin_config_deinit(void);

// ==== OpenAI API Configuration ====

/**
 * @brief Set OpenAI API key securely
 * 
 * Encrypts and stores the API key in NVS. Previous key is securely erased.
 * 
 * @param api_key The OpenAI API key (will be encrypted)
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if key is invalid
 */
esp_err_t smartbin_config_set_openai_key(const char* api_key);

/**
 * @brief Get OpenAI API key
 * 
 * Retrieves and decrypts the stored API key. Caller must not modify
 * or store the returned pointer - it's only valid until next config call.
 * 
 * @return Decrypted API key string, or NULL if not set or error
 */
const char* smartbin_config_get_openai_key(void);

/**
 * @brief Check if OpenAI API key is configured
 * 
 * @return true if key is set and valid, false otherwise
 */
bool smartbin_config_has_openai_key(void);

/**
 * @brief Clear OpenAI API key
 * 
 * Securely erases the stored API key from NVS and memory.
 * 
 * @return ESP_OK on success
 */
esp_err_t smartbin_config_clear_openai_key(void);

// ==== WiFi Configuration ====

/**
 * @brief Set WiFi credentials
 * 
 * @param ssid WiFi network SSID
 * @param password WiFi password (can be NULL for open networks)
 * @return ESP_OK on success
 */
esp_err_t smartbin_config_set_wifi_credentials(const char* ssid, const char* password);

/**
 * @brief Get WiFi SSID
 * 
 * @param ssid Buffer to store SSID (min SMARTBIN_CONFIG_WIFI_SSID_MAX_LEN bytes)
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if not set
 */
esp_err_t smartbin_config_get_wifi_ssid(char* ssid);

/**
 * @brief Get WiFi password
 * 
 * @param password Buffer to store password (min SMARTBIN_CONFIG_WIFI_PASSWORD_MAX_LEN bytes)
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if not set
 */
esp_err_t smartbin_config_get_wifi_password(char* password);

/**
 * @brief Check if WiFi credentials are configured
 * 
 * @return true if both SSID and password are set
 */
bool smartbin_config_has_wifi_credentials(void);

/**
 * @brief Clear WiFi credentials
 * 
 * @return ESP_OK on success
 */
esp_err_t smartbin_config_clear_wifi_credentials(void);

// ==== Factory Reset ====

/**
 * @brief Factory reset all configuration
 * 
 * Securely erases all stored configuration data.
 * 
 * @return ESP_OK on success
 */
esp_err_t smartbin_config_factory_reset(void);

#ifdef __cplusplus
}
#endif