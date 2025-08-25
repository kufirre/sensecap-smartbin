#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

/**
 * @brief Initialize LittleFS filesystem for web assets
 * 
 * Mounts the LittleFS partition at /littlefs for serving static web files.
 * The partition is automatically formatted if mounting fails.
 * 
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t filesystem_init(void);

/**
 * @brief Deinitialize LittleFS filesystem
 * 
 * Unmounts the LittleFS filesystem and frees resources.
 * 
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t filesystem_deinit(void);

/**
 * @brief Get filesystem usage information
 * 
 * @param total_bytes Pointer to store total filesystem size
 * @param used_bytes Pointer to store used filesystem size
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t filesystem_get_info(size_t *total_bytes, size_t *used_bytes);

#ifdef __cplusplus
}
#endif