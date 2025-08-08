#include <esp_log.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include "main.h"
#include "wifi.h"
#include "camera.h"
#include "api.h"
#include "council.h"
#include "cmd.h"
#include "sensecap-watcher.h"

static const char *TAG = "SMARTBIN_MAIN";

// Task handles for OpenAI realtime functionality
static TaskHandle_t webrtc_task_handle = NULL;

static void webrtc_task(void *pvParameters) {
  oai_init_audio_encoder();
  while (1) {
    oai_webrtc();
    vTaskDelay(pdMS_TO_TICKS(15));
  }
}

// Using UI functions declared in ui.h and implemented in src/ui/ui.c

extern "C" void app_main(void)
{
  ESP_LOGI(TAG, "Starting SenseCap SmartBin v2.0 with OpenAI Realtime API...");

  // Initialize NVS first
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // Initialize event loop
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  // Initialize console commands for configuration
  cmd_init();

  // Initialize board and UI
  board_init();
  bsp_set_btn_long_press_cb(long_press_event_cb);
  ui_init();
  oai_wifi_init();
  
  // Initialize peer connection for WebRTC
  peer_init();
  oai_init_audio_capture();
  oai_init_audio_decoder();

  // WiFi connect and wait
  oai_wifi();
  ui_listening();

  // Start realtime pipeline if key is present
  if (strlen(g_openai_api_key_buf) > 0) {
    ESP_LOGI(TAG, "OpenAI API key configured - starting realtime audio connection");
    xTaskCreate(webrtc_task, "webrtc_task", 8192, NULL, 5, &webrtc_task_handle);
  }

  ESP_LOGI(TAG, "SenseCap SmartBin ready!");
  ESP_LOGI(TAG, "Features: Waste Analysis (camera), Council Lookup, OpenAI Realtime Audio");
  ESP_LOGI(TAG, "Console commands: wifi_sta, openai_api, reboot");

  // Main application loop
  for (;;) {
    // Process any background tasks
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Periodic status logging
    static int status_counter = 0;
    if (++status_counter >= 60) { // Every 60 seconds
      status_counter = 0;
      ESP_LOGI(TAG, "SmartBin status: WebRTC=%s, WiFi=Connected", 
               webrtc_task_handle ? "Active" : "Inactive");
    }
  }
}