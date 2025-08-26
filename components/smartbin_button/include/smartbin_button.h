#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

/**
 * @brief Button event types
 */
typedef enum {
    SMARTBIN_BUTTON_EVENT_SHORT_PRESS,
    SMARTBIN_BUTTON_EVENT_LONG_PRESS
} smartbin_button_event_t;

/**
 * @brief Button event callback function type
 * 
 * @param event The button event that occurred
 */
typedef void (*smartbin_button_callback_t)(smartbin_button_event_t event);

/**
 * @brief Initialize the button handler system
 * 
 * Creates the button event queue and handler task.
 * Registers callbacks for UI button integration.
 * 
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t smartbin_button_init(void);

/**
 * @brief Start the button handler task
 * 
 * Begins processing button events and handling the complete
 * camera capture + AI analysis workflow.
 * 
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t smartbin_button_start(void);

/**
 * @brief Stop the button handler system
 * 
 * Stops the handler task and cleans up resources.
 * 
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t smartbin_button_stop(void);

/**
 * @brief Deinitialize the button handler system
 * 
 * Cleans up all resources including queue and task.
 * 
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t smartbin_button_deinit(void);

/**
 * @brief Queue a button event (called from ISR)
 * 
 * This function is called by the UI button callbacks to queue
 * button events for processing by the handler task.
 * 
 * @param event The button event to queue
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t smartbin_button_queue_event(smartbin_button_event_t event);

/**
 * @brief Register button callbacks with UI system
 * 
 * Automatically registers internal button callbacks with the UI system
 * to handle short and long press events.
 * 
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t smartbin_button_register_callbacks(void);

#ifdef __cplusplus
}
#endif