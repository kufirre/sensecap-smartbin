#include "camera.h"
#include <esp_log.h>

static const char *TAG = "SMARTBIN_CAMERA";

extern "C" esp_err_t camera_init(void)
{
  ESP_LOGI(TAG, "Camera initialization stubbed - ready for future implementation");
  return ESP_OK;
}

extern "C" esp_err_t camera_capture(camera_fb_t **fb)
{
  ESP_LOGW(TAG, "Camera capture not implemented yet");
  *fb = NULL;
  return ESP_ERR_NOT_SUPPORTED;
}