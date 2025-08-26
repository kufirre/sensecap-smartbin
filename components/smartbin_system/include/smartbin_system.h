#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "esp_io_expander.h"
#include "sscma_client_io.h"
#include "sscma_client_ops.h"
#include "lvgl.h"

// ==== BOARD FUNCTIONS ====

/**
 * @brief Initialize board hardware components
 * 
 * Initializes all board-level hardware including:
 * - IO expander
 * - LVGL display system
 * - RGB LED
 * - Audio codec
 * - Display brightness and volume settings
 */
void smartbin_board_init(void);

/**
 * @brief Handle long press button event
 * 
 * Called when the board detects a long button press.
 * Performs system shutdown sequence and restart.
 */
void smartbin_board_long_press_event_cb(void);

// ==== FILESYSTEM FUNCTIONS ====

/**
 * @brief Initialize LittleFS filesystem for web assets
 * 
 * Mounts the LittleFS partition at /littlefs for serving static web files.
 * The partition is automatically formatted if mounting fails.
 * 
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t smartbin_filesystem_init(void);

/**
 * @brief Deinitialize LittleFS filesystem
 * 
 * Unmounts the LittleFS filesystem and frees resources.
 * 
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t smartbin_filesystem_deinit(void);

/**
 * @brief Get filesystem usage information
 * 
 * @param total_bytes Pointer to store total filesystem size
 * @param used_bytes Pointer to store used filesystem size
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t smartbin_filesystem_get_info(size_t *total_bytes, size_t *used_bytes);

// ==== CAMERA FUNCTIONS ====

/**
 * @brief Flash control callback function types
 */
typedef esp_err_t (*smartbin_camera_flash_enable_cb_t)(void);
typedef esp_err_t (*smartbin_camera_flash_disable_cb_t)(void);
typedef esp_err_t (*smartbin_camera_flash_pulse_cb_t)(uint32_t duration_ms);

/**
 * @brief Register flash control callbacks
 * 
 * Allows the main application to provide flash control functions
 * to maintain component decoupling while preserving functionality.
 * 
 * @param enable_cb Callback to enable continuous flash
 * @param disable_cb Callback to disable flash
 * @param pulse_cb Callback to pulse flash for a duration
 */
void smartbin_camera_register_flash_callbacks(
    smartbin_camera_flash_enable_cb_t enable_cb,
    smartbin_camera_flash_disable_cb_t disable_cb,
    smartbin_camera_flash_pulse_cb_t pulse_cb
);

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
esp_err_t smartbin_camera_init(void);

/**
 * @brief Start the camera streaming
 * 
 * Configures the camera sensor and starts the inference/streaming
 * 
 * @param enable_flash Whether to use LED flash during streaming (for low light)
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t smartbin_camera_start_streaming(bool enable_flash);

/**
 * @brief Stop the camera streaming
 * 
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t smartbin_camera_stop_streaming(void);

/**
 * @brief Capture a single picture and display it
 * 
 * Takes a single photo and displays it on the screen.
 * Optionally saves the image to SD card with timestamp filename.
 * 
 * @param enable_flash Whether to use LED flash before capture
 * @param save_to_sd Whether to save image to SD card (if available)
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t smartbin_camera_capture_picture(bool enable_flash, bool save_to_sd);

/**
 * @brief Get camera module information
 * 
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t smartbin_camera_get_info(void);

/**
 * @brief Cleanup camera resources
 * 
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t smartbin_camera_deinit(void);

/**
 * @brief Check if camera is initialized
 * 
 * @return true if initialized, false otherwise
 */
bool smartbin_camera_is_initialized(void);

/**
 * @brief Check if camera is currently streaming
 * 
 * @return true if streaming, false otherwise
 */
bool smartbin_camera_is_streaming(void);

/**
 * @brief Get the SSCMA client handle (for advanced usage)
 * 
 * @return sscma_client_handle_t or NULL if not initialized
 */
sscma_client_handle_t smartbin_camera_get_client(void);

/**
 * @brief Get the last captured image data
 * 
 * Returns the JPEG data from the last successful image capture.
 * The data is valid until the next capture operation.
 * 
 * @param image_data Pointer to store the image data pointer
 * @param image_len Pointer to store the image data length
 * @return ESP_OK if image data is available, ESP_ERR_NOT_FOUND if no image captured yet
 */
esp_err_t smartbin_camera_get_last_image(const uint8_t **image_data, size_t *image_len);

/**
 * @brief Wait for image capture to complete
 * 
 * Blocks until an image is captured and processed, or timeout occurs.
 * Should be called after smartbin_camera_capture_picture().
 * 
 * @param timeout_ms Maximum time to wait in milliseconds
 * @return ESP_OK if image captured, ESP_ERR_TIMEOUT if timeout occurred
 */
esp_err_t smartbin_camera_wait_for_image(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif