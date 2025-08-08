#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <esp_err.h>
#include <stdint.h>
#include <stddef.h>

typedef struct {
  uint8_t *buf;
  size_t len;
  size_t width;
  size_t height;
  int format;
} camera_fb_t;

esp_err_t camera_init(void);
esp_err_t camera_capture(camera_fb_t **fb);

#ifdef __cplusplus
}
#endif