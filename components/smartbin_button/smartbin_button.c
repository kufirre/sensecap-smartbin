#include "smartbin_button.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_log.h>

#include "smartbin_http.h"
#include "smartbin_webserver.h"
#include "smartbin_system.h"
#include "sensecap-watcher.h"
#include "ui.h"

static const char *TAG = "SMARTBIN_BUTTON";

// Button handler state
static QueueHandle_t button_event_queue = NULL;
static TaskHandle_t button_handler_task = NULL;
static bool button_system_initialized = false;
static bool button_system_running = false;

// Button handler task - processes button events and handles camera + OpenAI workflow
static void button_handler_task_func(void *pvParameters) {
    smartbin_button_event_t event;
    
    ESP_LOGI(TAG, "Button handler task started");
    
    while (button_system_running) {
        // Wait for button events with timeout to check for stop condition
        if (xQueueReceive(button_event_queue, &event, pdMS_TO_TICKS(1000)) == pdTRUE) {
            ESP_LOGI(TAG, "📬 Button event received in handler task");
            switch (event) {
                case SMARTBIN_BUTTON_EVENT_SHORT_PRESS:
                    ESP_LOGI(TAG, "🔘 Processing short press - camera capture + AI analysis");
                    
                    // Update UI to show capture in progress
                    ui_show_status("Capturing image...");
                    
                    // Check if camera is ready
                    if (!smartbin_camera_is_initialized()) {
                        ESP_LOGE(TAG, "❌ Camera not initialized");
                        ui_show_status("Camera error");
                        vTaskDelay(pdMS_TO_TICKS(2000));
                        ui_show_status("Ready");
                        break;
                    }
                    
                    // Capture picture with flash and SD save
                    esp_err_t capture_result = smartbin_camera_capture_picture(true, true);
                    if (capture_result != ESP_OK) {
                        ESP_LOGE(TAG, "❌ Failed to capture picture: %s", esp_err_to_name(capture_result));
                        ui_show_status("Capture failed");
                        vTaskDelay(pdMS_TO_TICKS(2000));
                        ui_show_status("Ready");
                        break;
                    }
                    
                    // Wait for image to be processed
                    ui_show_status("Processing...");
                    esp_err_t wait_result = smartbin_camera_wait_for_image(10000);
                    if (wait_result != ESP_OK) {
                        ESP_LOGE(TAG, "❌ Image capture timeout: %s", esp_err_to_name(wait_result));
                        ui_show_status("Timeout");
                        vTaskDelay(pdMS_TO_TICKS(2000));
                        ui_show_status("Ready");
                        break;
                    }
                    
                    // Get the captured image
                    const uint8_t* image_data = NULL;
                    size_t image_len = 0;
                    esp_err_t get_result = smartbin_camera_get_last_image(&image_data, &image_len);
                    
                    if (get_result != ESP_OK || !image_data || image_len == 0) {
                        ESP_LOGE(TAG, "❌ Failed to get image data: %s", esp_err_to_name(get_result));
                        ui_show_status("Image error");
                        vTaskDelay(pdMS_TO_TICKS(2000));
                        ui_show_status("Ready");
                        break;
                    }
                    
                    ESP_LOGI(TAG, "📷 Image captured: %zu bytes", image_len);
                    
                    // Check if API key is configured
                    smartbin_config_t smartbin_cfg = {};
                    esp_err_t config_err = smartbin_webserver_get_config(&smartbin_cfg);
                    if (config_err != ESP_OK || strlen(smartbin_cfg.openai_api_key) == 0) {
                        ESP_LOGW(TAG, "⚠️ OpenAI API key not configured");
                        ui_show_status("No API key");
                        vTaskDelay(pdMS_TO_TICKS(3000));
                        ui_show_status("Ready");
                        break;
                    }
                    
                    // Send to OpenAI for analysis
                    ui_show_status("Analyzing...");
                    const char* postcode = strlen(smartbin_cfg.post_code) > 0 ? smartbin_cfg.post_code : "SW1A 1AA";
                    char* response_text = NULL;
                    
                    ESP_LOGI(TAG, "🤖 Sending image to OpenAI for analysis (postcode: %s)", postcode);
                    esp_err_t analysis_result = smartbin_http_analyze_waste_text(image_data, image_len, postcode, &response_text);
                    
                    if (analysis_result == ESP_OK && response_text) {
                        ESP_LOGI(TAG, "✅ AI analysis completed successfully");
                        ESP_LOGI(TAG, "📝 AI Response: %s", response_text);
                        
                        // Show response on display
                        ui_show_status("Analysis complete!");
                        vTaskDelay(pdMS_TO_TICKS(2000));
                        
                        // Show AI response text on display for 5 seconds
                        ui_show_status(response_text);
                        vTaskDelay(pdMS_TO_TICKS(5000));
                        
                        // Clean up response text
                        heap_caps_free(response_text);
                    } else {
                        ESP_LOGE(TAG, "❌ AI analysis failed: %s", esp_err_to_name(analysis_result));
                        ui_show_status("Analysis failed");
                        vTaskDelay(pdMS_TO_TICKS(3000));
                    }
                    
                    // Return to ready state
                    ui_show_status("Ready");
                    break;
                    
                case SMARTBIN_BUTTON_EVENT_LONG_PRESS:
                    ESP_LOGI(TAG, "🔘 Processing long press - power management");
                    
                    // Turn off RGB LED
                    ui_rgb_off();
                    
                    // Display shutdown message
                    ui_show_status("Shutting down...");
                    
                    // Small delay to show message
                    vTaskDelay(pdMS_TO_TICKS(2000));
                    
                    // System shutdown or restart
                    ESP_LOGI(TAG, "System shutdown initiated by user");
                    bsp_system_shutdown();  // This will put the device into deep sleep
                    break;
                    
                default:
                    ESP_LOGW(TAG, "Unknown button event: %d", event);
                    break;
            }
        }
    }
    
    ESP_LOGI(TAG, "Button handler task stopped");
    vTaskDelete(NULL);
}

esp_err_t smartbin_button_init(void) {
    if (button_system_initialized) {
        ESP_LOGW(TAG, "Button system already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing button handler system...");
    
    // Create button event queue
    button_event_queue = xQueueCreate(5, sizeof(smartbin_button_event_t));
    if (button_event_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create button event queue");
        return ESP_FAIL;
    }
    
    button_system_initialized = true;
    ESP_LOGI(TAG, "Button handler system initialized");
    return ESP_OK;
}

esp_err_t smartbin_button_start(void) {
    if (!button_system_initialized) {
        ESP_LOGE(TAG, "Button system not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (button_system_running) {
        ESP_LOGW(TAG, "Button system already running");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Starting button handler task...");
    ESP_LOGI(TAG, "Queue handle: %p", button_event_queue);
    
    button_system_running = true;  // Set this BEFORE creating the task
    
    // Create button handler task with large stack for HTTP operations
    BaseType_t result = xTaskCreate(
        button_handler_task_func,
        "smartbin_button",     // Task name
        20480,                 // Stack size (20KB for HTTP operations)
        NULL,                  // Parameters
        6,                     // Higher priority than main loop
        &button_handler_task   // Task handle
    );
    
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create button handler task");
        button_system_running = false;  // Reset on failure
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "✅ Button handler task created successfully");
    
    // Give the task a moment to start
    vTaskDelay(pdMS_TO_TICKS(100));
    
    ESP_LOGI(TAG, "✅ Button handler system started - ready for button presses");
    return ESP_OK;
}

esp_err_t smartbin_button_stop(void) {
    if (!button_system_running) {
        ESP_LOGW(TAG, "Button system not running");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Stopping button handler system...");
    button_system_running = false;
    
    // Wait for task to finish (it will check the flag and exit)
    if (button_handler_task != NULL) {
        // Give task time to finish gracefully
        vTaskDelay(pdMS_TO_TICKS(2000));
        button_handler_task = NULL;
    }
    
    ESP_LOGI(TAG, "Button handler system stopped");
    return ESP_OK;
}

esp_err_t smartbin_button_deinit(void) {
    if (button_system_running) {
        smartbin_button_stop();
    }
    
    if (!button_system_initialized) {
        ESP_LOGW(TAG, "Button system not initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Deinitializing button handler system...");
    
    // Clean up queue
    if (button_event_queue != NULL) {
        vQueueDelete(button_event_queue);
        button_event_queue = NULL;
    }
    
    button_system_initialized = false;
    ESP_LOGI(TAG, "Button handler system deinitialized");
    return ESP_OK;
}

esp_err_t smartbin_button_queue_event(smartbin_button_event_t event) {
    ESP_LOGI(TAG, "📨 Attempting to queue button event %d", event);
    
    if (!button_system_initialized || button_event_queue == NULL) {
        ESP_LOGE(TAG, "❌ Button system not initialized (init=%d, queue=%p)", 
                 button_system_initialized, button_event_queue);
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!button_system_running) {
        ESP_LOGE(TAG, "❌ Button system not running");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "📤 Sending event to queue (running=%d, queue=%p)", 
             button_system_running, button_event_queue);
    
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    BaseType_t result = xQueueSendFromISR(button_event_queue, &event, &xHigherPriorityTaskWoken);
    
    if (result == pdPASS) {
        ESP_LOGI(TAG, "✅ Event queued successfully");
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "❌ Failed to queue button event (queue full)");
        return ESP_FAIL;
    }
}

// Internal button callback for capturing a picture (short press)
static void internal_button_capture_callback(void) {
    ESP_LOGI(TAG, "Short press detected - queueing capture event");
    smartbin_button_queue_event(SMARTBIN_BUTTON_EVENT_SHORT_PRESS);
}

// Internal button callback for power management (long press)
static void internal_button_power_callback(void) {
    ESP_LOGI(TAG, "Long press detected - queueing shutdown event");
    smartbin_button_queue_event(SMARTBIN_BUTTON_EVENT_LONG_PRESS);
}

esp_err_t smartbin_button_register_callbacks(void) {
    ESP_LOGI(TAG, "Registering button callbacks with UI system...");
    
    esp_err_t result = ui_button_init(internal_button_capture_callback, internal_button_power_callback);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register button callbacks: %s", esp_err_to_name(result));
        return result;
    }
    
    ESP_LOGI(TAG, "✅ Button callbacks registered successfully");
    return ESP_OK;
}