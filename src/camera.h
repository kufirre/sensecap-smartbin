#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "esp_io_expander.h"
#include "sscma_client_io.h"
#include "sscma_client_ops.h"
#include "lvgl.h"

/**
 * @brief Camera module initialization and management
 * 
 * This module handles the SSCMA camera client initialization,
 * image processing, and display functionality.
 */

/**
 * @brief Initialize the camera module
 * 
 * This function initializes all camera-related components including:
 * - IO expander
 * - LVGL display  
 * - SSCMA client
 * - LCD panel
 * - LVGL image object
 * 
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t camera_init(void);

/**
 * @brief Start the camera streaming
 * 
 * Configures the camera sensor and starts the inference/streaming
 * 
 * @param enable_flash Whether to use LED flash during streaming (for low light)
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t camera_start_streaming(bool enable_flash);

/**
 * @brief Stop the camera streaming
 * 
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t camera_stop_streaming(void);

/**
 * @brief Capture a single picture and display it
 * 
 * Takes a single photo and displays it on the screen
 * 
 * @param enable_flash Whether to use LED flash before capture
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t camera_capture_picture(bool enable_flash);

/**
 * @brief Get camera module information
 * 
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t camera_get_info(void);

/**
 * @brief Cleanup camera resources
 * 
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t camera_deinit(void);

/**
 * @brief Check if camera is initialized
 * 
 * @return true if initialized, false otherwise
 */
bool camera_is_initialized(void);

/**
 * @brief Check if camera is currently streaming
 * 
 * @return true if streaming, false otherwise
 */
bool camera_is_streaming(void);

/**
 * @brief Get the SSCMA client handle (for advanced usage)
 * 
 * @return sscma_client_handle_t or NULL if not initialized
 */
sscma_client_handle_t camera_get_client(void);

#ifdef __cplusplus
}
#endif