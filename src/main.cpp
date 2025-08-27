#include <esp_log.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include "main.h"
#include "sensecap-watcher.h"
#include "ui/ui.h"
#include "smartbin_webserver.h"
#include "smartbin_system.h"
#include "smartbin_wifi.h"
#include "smartbin_config.h"
#include "smartbin_button.h"
#include "smartbin_audio.h"

// Camera resolution constants (matches camera.c)
#define IMG_WIDTH  640
#define IMG_HEIGHT 480

static const char *TAG = "SMARTBIN_MAIN";


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
  if (smartbin_filesystem_init() != ESP_OK) {
    ESP_LOGW(TAG, "Failed to initialize LittleFS - web interface may not work");
  }

  // Initialize board
  smartbin_board_init();

  // Initialize audio I/O early so playback works on first TTS
  smartbin_audio_init_capture();   // sets up speaker/mic handles + volume
  smartbin_audio_init_decoder();   // creates Opus decoder for playback

  // Initialize configuration system (must be done early for API keys)
  ESP_LOGI(TAG, "Initializing configuration system...");
  if (smartbin_config_init() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize configuration system");
    return;
  }

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
  if (smartbin_camera_init() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize camera module");
    return;
  }

  // Flash callbacks are now handled automatically by the camera component

  // Get camera information
  ESP_LOGI(TAG, "Getting camera module information...");
  smartbin_camera_get_info();

  // Camera is ready for picture capture
  ESP_LOGI(TAG, "Camera ready for picture capture");

  // Initialize button handler system
  ESP_LOGI(TAG, "Initializing button handler system...");
  if (smartbin_button_init() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize button handler system");
    return;
  }
  
  // Start button handler task
  if (smartbin_button_start() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start button handler system");
    return;
  }

  // Register button callbacks after system is initialized and started
  ESP_LOGI(TAG, "Registering button system callbacks...");
  if (smartbin_button_register_callbacks() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register button callbacks");
    return;
  }

  ESP_LOGI(TAG, "SenseCap SmartBin camera module ready!");
  ESP_LOGI(TAG, "Features: Picture capture at %dx%d with flash", IMG_WIDTH, IMG_HEIGHT);
  ESP_LOGI(TAG, "Controls: Short press = capture picture, Long press = power off");
  ESP_LOGI(TAG, "Configuration: Look for 'SenseCAP-SmartBin-XXXX' WiFi network");
  
  ESP_LOGI(TAG, "✅ Button handler system ready");
  ESP_LOGI(TAG, "📱 Press button: Short=Capture+Analyze, Long=Shutdown");

  // Main application loop
  static bool ui_updated = false;
  for (;;) {
    // Update UI with WiFi status (once), then switch to Ready
    if (!ui_updated) {
      if (smartbin_wifi_is_connected()) {
        char status_msg[64];
        snprintf(status_msg, sizeof(status_msg), "smartbin.local\n%s", smartbin_wifi_get_ip_address());
        ui_show_status(status_msg);
        ESP_LOGI(TAG, "UI updated: smartbin.local / %s", smartbin_wifi_get_ip_address());
        
        // Show WiFi info for 3 seconds, then switch to Ready
        vTaskDelay(pdMS_TO_TICKS(3000));
        ui_show_status("Ready");
        ui_updated = true;
      } else if (smartbin_wifi_get_state() == SMARTBIN_WIFI_STATE_AP_MODE) {
        ui_wifi_config_mode();
        ui_updated = true;
        ESP_LOGI(TAG, "UI updated with config mode");
      } else if (smartbin_wifi_get_state() == SMARTBIN_WIFI_STATE_CONNECTING) {
        ui_wifi_connecting();
        ESP_LOGI(TAG, "UI showing WiFi connecting");
      }
    }
    
    // Process any background tasks
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Periodic status logging
    static int status_counter = 0;
    if (++status_counter >= 60) { // Every 60 seconds (60 * 1 second)
      status_counter = 0;
      ESP_LOGI(TAG, "SmartBin status: Camera=%s, WiFi=%s, IP=%s", 
               smartbin_camera_is_initialized() ? "Active" : "Inactive",
               smartbin_wifi_is_connected() ? "Connected" : "Disconnected",
               smartbin_wifi_is_connected() ? smartbin_wifi_get_ip_address() : "None");
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
  // smartbin_wifi_init();
  //
  // // Initialize peer connection for WebRTC
  // peer_init();
  // smartbin_audio_init_capture();
  // smartbin_audio_init_decoder();
  //
  // // WiFi connect and wait
  // smartbin_wifi_connect();
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