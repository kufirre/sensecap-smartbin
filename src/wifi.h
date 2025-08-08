#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <esp_err.h>

esp_err_t smartbin_wifi_init(void);
esp_err_t smartbin_wifi_connect(void);
esp_err_t smartbin_wifi_wait_connection(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif