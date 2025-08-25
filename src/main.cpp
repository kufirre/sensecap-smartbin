#include <esp_log.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include "main.h"
#include "camera.h"
#include "wifi.h"
#include "filesystem.h"
// #include "api.h"
// #include "council.h"
// #include "cmd.h"
#include "sensecap-watcher.h"
#include "ui/ui.h"
#include "webserver.h"

// Camera resolution constants (matches camera.c)
#define IMG_WIDTH  640
#define IMG_HEIGHT 480

static const char *TAG = "SMARTBIN_MAIN";

// Button callback for capturing a picture (short press)
static void button_capture_callback(void)
{
  ESP_LOGI(TAG, "Short press - capturing picture with flash");
  
  if (camera_capture_picture(true, true) == ESP_OK) {  // Enable flash and SD save for button captures
    ESP_LOGI(TAG, "✓ Picture capture initiated");
  } else {
    ESP_LOGE(TAG, "✗ Failed to capture picture");
  }
}

// Button callback for power management (long press)
static void button_power_callback(void)
{
  ESP_LOGI(TAG, "Long press - initiating system shutdown/restart");
  
  // Turn off RGB LED
  ui_rgb_off();
  
  // Display shutdown message
  ui_show_status("Shutting down...");
  
  // Small delay to show message
  vTaskDelay(pdMS_TO_TICKS(2000));
  
  // System shutdown or restart
  ESP_LOGI(TAG, "System shutdown initiated by user");
  bsp_system_shutdown();  // This will put the device into deep sleep
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

  // Initialize LittleFS for web assets
  ESP_LOGI(TAG, "Initializing filesystem...");
  if (filesystem_init() != ESP_OK) {
    ESP_LOGW(TAG, "Failed to initialize LittleFS - web interface may not work");
  }

  // Initialize board
  board_init();

  // Initialize UI system (must be done before WiFi for status display)
  ESP_LOGI(TAG, "Initializing UI system...");
  ui_init();

  // Initialize WiFi management system
  ESP_LOGI(TAG, "Initializing WiFi management...");
  smartbin_wifi_init();
  
  // Start WiFi connection (will automatically enter AP mode if no credentials)
  ESP_LOGI(TAG, "Starting WiFi connection...");
  smartbin_wifi_connect();

  // Initialize camera module
  ESP_LOGI(TAG, "Initializing camera module...");
  if (camera_init() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize camera module");
    return;
  }

  // Get camera information
  ESP_LOGI(TAG, "Getting camera module information...");
  camera_get_info();

  // Initialize button using UI component
  ESP_LOGI(TAG, "Initializing button (short=capture, long=power)...");
  if (ui_button_init(button_capture_callback, button_power_callback) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize button");
  }

  // Camera is ready for picture capture
  ESP_LOGI(TAG, "Camera ready for picture capture");

  ESP_LOGI(TAG, "SenseCap SmartBin camera module ready!");
  ESP_LOGI(TAG, "Features: Picture capture at %dx%d with flash", IMG_WIDTH, IMG_HEIGHT);
  ESP_LOGI(TAG, "Controls: Short press = capture picture, Long press = power off");
  ESP_LOGI(TAG, "Configuration: Look for 'SenseCAP-SmartBin-XXXX' WiFi network");

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