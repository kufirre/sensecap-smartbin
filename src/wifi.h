#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <esp_err.h>
#include <stdbool.h>

typedef enum {
    WIFI_MODE_CONFIG,  // AP mode for configuration
    WIFI_MODE_NORMAL   // STA mode for normal operation
} smartbin_wifi_mode_t;

typedef enum {
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_AP_MODE
} smartbin_wifi_state_t;

esp_err_t smartbin_wifi_init(void);
esp_err_t smartbin_wifi_start_ap_mode(void);
esp_err_t smartbin_wifi_start_sta_mode(const char* ssid, const char* password);
esp_err_t smartbin_wifi_connect(void);
esp_err_t smartbin_wifi_wait_connection(uint32_t timeout_ms);
smartbin_wifi_state_t smartbin_wifi_get_state(void);
bool smartbin_wifi_is_connected(void);
char* smartbin_wifi_get_ip_address(void);
char* smartbin_wifi_get_ap_ip_address(void);
void smartbin_wifi_check_and_switch_mode(void);

// Legacy functions for compatibility
void oai_wifi_init(void);
void oai_wifi(void);

#ifdef __cplusplus
}
#endif