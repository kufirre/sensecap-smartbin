#include <esp_log.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include "main.h"
#include "camera.h"
// #include "wifi.h"
// #include "api.h"
// #include "council.h"
// #include "cmd.h"
#include "sensecap-watcher.h"

static const char *TAG = "SMARTBIN_MAIN";

// Button callback for capturing a picture
static void button_capture_callback(void)
{
  ESP_LOGI(TAG, "Button pressed - capturing picture");
  
  if (camera_capture_picture() == ESP_OK) {
    ESP_LOGI(TAG, "✓ Picture capture initiated");
  } else {
    ESP_LOGE(TAG, "✗ Failed to capture picture");
  }
}

// // Task handles for OpenAI realtime functionality
// static TaskHandle_t webrtc_task_handle = NULL;

// static void webrtc_task(void *pvParameters) {
//   // Initialize audio encoder before starting WebRTC
//   oai_init_audio_encoder();
//   // Start WebRTC connection - this runs its own event loop
//   oai_webrtc();
// }

// // Using UI functions declared in ui.h and implemented in src/ui/ui.c

extern "C" void app_main(void)
{
  ESP_LOGI(TAG, "Starting SenseCap SmartBin v2.0 with Camera Module...");

  // Initialize NVS first
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // Initialize event loop
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  // Initialize board
  board_init();

  // Initialize camera module
  ESP_LOGI(TAG, "Initializing camera module...");
  if (camera_init() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize camera module");
    return;
  }

  // Get camera information
  ESP_LOGI(TAG, "Getting camera module information...");
  camera_get_info();

  // Initialize and register button callback for picture capture
  ESP_LOGI(TAG, "Initializing button for picture capture...");
  if (bsp_knob_btn_init(NULL) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize button");
  } else {
    ESP_LOGI(TAG, "Button initialized successfully");
    bsp_set_btn_long_press_cb(button_capture_callback);
    ESP_LOGI(TAG, "Button callback registered for long press");
  }

  // Camera is ready for picture capture
  ESP_LOGI(TAG, "Camera ready for picture capture");

  ESP_LOGI(TAG, "SenseCap SmartBin camera module ready!");
  ESP_LOGI(TAG, "Features: Picture capture with AI inference");
  ESP_LOGI(TAG, "Controls: Long press button to capture picture");

  // Main application loop
  for (;;) {
    // Process any background tasks
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    // Periodic status logging
    static int status_counter = 0;
    if (++status_counter >= 12) { // Every 60 seconds (12 * 5 seconds)
      status_counter = 0;
      ESP_LOGI(TAG, "SmartBin status: Camera=%s, Ready=%s", 
               camera_is_initialized() ? "Active" : "Inactive",
               camera_is_initialized() ? "YES" : "NO");
    }
  }

  // Future features to be uncommented gradually:
  // 
  // // Initialize console commands for configuration
  // cmd_init();
  //
  // // Initialize UI
  // bsp_set_btn_long_press_cb(long_press_event_cb);
  // ui_init();
  // oai_wifi_init();
  //
  // // Initialize peer connection for WebRTC
  // peer_init();
  // oai_init_audio_capture();
  // oai_init_audio_decoder();
  //
  // // WiFi connect and wait
  // oai_wifi();
  // ui_show_status("WiFi Connected!");
  // vTaskDelay(pdMS_TO_TICKS(1000)); // Brief pause to show status
  //
  // // Start realtime pipeline if key is present
  // if (strlen(g_openai_api_key_buf) > 0) {
  //   ESP_LOGI(TAG, "OpenAI API key configured - starting realtime audio connection");
  //   ui_show_status("Starting WebRTC...");
  //   // Use larger stack size for WebRTC task to handle audio processing
  //   xTaskCreate(webrtc_task, "webrtc_task", 16384, NULL, 5, &webrtc_task_handle);
  //   vTaskDelay(pdMS_TO_TICKS(2000)); // Give WebRTC time to connect
  //   ui_show_status("WebRTC Active - Ready to talk!");
  //   vTaskDelay(pdMS_TO_TICKS(2000)); // Show status for 2 seconds
  // }
  //
  // // Switch to listening mode
  // ui_listening();
}