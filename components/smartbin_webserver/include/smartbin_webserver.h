#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

/**
 * @brief Configuration parameters structure
 * (sizes kept exactly as provided)
 */
typedef struct {
    char wifi_ssid[32];
    char wifi_password[64];
    char openai_api_key[165];
    char post_code[16];      // Post code for location
    char bin_color[16];      // Color of the bin (e.g., "red", "blue", "green")
} smartbin_config_t;

/**
 * Build-time options (override via -D or before including this header)
 * WEB_FS_BASE_PATH:    mount point of your FS (default "/littlefs")
 * WEB_FS_INDEX_PATH:   path to your index (default "/index.html")
 * WEB_TRY_GZIP:        serve *.gz variants when available (1=yes)
 * WEB_CACHE_STATIC:    add Cache-Control immutable for static files (0/1)
 */
#ifndef WEB_FS_BASE_PATH
#define WEB_FS_BASE_PATH "/littlefs"
#endif
#ifndef WEB_FS_INDEX_PATH
#define WEB_FS_INDEX_PATH "/index.html"
#endif
#ifndef WEB_TRY_GZIP
#define WEB_TRY_GZIP 1
#endif
#ifndef WEB_CACHE_STATIC
#define WEB_CACHE_STATIC 0
#endif

/**
 * @brief Initialize the web server for configuration
 * 
 * Starts an HTTP server on port 80 that serves static files from WEB_FS_BASE_PATH,
 * and exposes:
 *   GET  /api/config
 *   POST /api/config
 *   GET  /api/scan   (triggers a one-time Wi-Fi scan)
 * If index.html is missing, a tiny fallback page is served.
 * 
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t smartbin_webserver_init(void);

/**
 * @brief Stop the web server
 * 
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t smartbin_webserver_stop(void);

/**
 * @brief Get current configuration (from NVS)
 * 
 * @param config Pointer to configuration structure to fill
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t smartbin_webserver_get_config(smartbin_config_t *config);

/**
 * @brief Set configuration parameters (to NVS)
 * 
 * @param config Pointer to configuration structure
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t smartbin_webserver_set_config(const smartbin_config_t *config);

/**
 * @brief Initialize static assets with ETag computation
 * Call after LittleFS is mounted, before httpd_start
 * 
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t smartbin_webserver_init_static_assets(void);

/**
 * @brief Start mDNS service for smartbin.local
 * Call after WiFi is connected
 * 
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t smartbin_webserver_start_mdns(void);

/**
 * @brief Stop mDNS service
 * 
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t smartbin_webserver_stop_mdns(void);

#ifdef __cplusplus
}
#endif