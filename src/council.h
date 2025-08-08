#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <esp_err.h>

typedef struct {
  char council_name[128];
  char region[64];
  char country[64];
} council_info_t;

esp_err_t council_get_info(const char *postcode, council_info_t *info);

#ifdef __cplusplus
}
#endif