#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <esp_err.h>
#include <stdint.h>
#include <stddef.h>

typedef struct {
  char category[64];
  float confidence;
} api_result_t;

esp_err_t api_init(void);
esp_err_t api_analyze_waste(const uint8_t *image_data, size_t image_len, api_result_t *result);

#ifdef __cplusplus
}
#endif