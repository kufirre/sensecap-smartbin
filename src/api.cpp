#include "api.h"
#include <esp_log.h>
#include <esp_http_client.h>
#include <cJSON.h>

static const char *TAG = "SMARTBIN_API";

extern "C" esp_err_t api_init(void)
{
  ESP_LOGI(TAG, "API client initialized");
  return ESP_OK;
}

extern "C" esp_err_t api_analyze_waste(const uint8_t *image_data, size_t image_len, api_result_t *result)
{
  ESP_LOGW(TAG, "Waste analysis API not implemented yet");
  if (result) {
    result->confidence = 0.0f;
    strcpy(result->category, "unknown");
  }
  return ESP_ERR_NOT_SUPPORTED;
}