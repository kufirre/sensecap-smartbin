// WiFi management with AP/STA mode switching for IoT device configuration
#include <assert.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <freertos/event_groups.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>
#include <esp_netif.h>
#include "main.h"
#include "wifi.h"
#include "webserver.h"

static const char *TAG = "SMARTBIN_WIFI";

// WiFi state management
static smartbin_wifi_state_t g_wifi_state = WIFI_STATE_DISCONNECTED;
static bool g_wifi_connected = false;
static esp_netif_t *g_sta_netif = NULL;
static esp_netif_t *g_ap_netif = NULL;
static char g_ip_address[16] = {0};
static TaskHandle_t g_wifi_task_handle = NULL;

// Configuration
#define AP_SSID_PREFIX "SenseCAP-SmartBin-"
#define AP_PASSWORD "sensecap123"
#define AP_MAX_CONNECTIONS 4
#define WIFI_CONNECTION_TIMEOUT_MS 15000
#define WIFI_CHECK_INTERVAL_MS 30000

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    static int s_retry_num = 0;
    
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WiFi STA started, attempting connection...");
                // Only connect if we're not already connected or connecting
                if (g_wifi_state != WIFI_STATE_CONNECTED && g_wifi_state != WIFI_STATE_CONNECTING) {
                    esp_err_t connect_result = esp_wifi_connect();
                    if (connect_result == ESP_OK) {
                        g_wifi_state = WIFI_STATE_CONNECTING;
                    } else {
                        ESP_LOGW(TAG, "WiFi connect failed: %s", esp_err_to_name(connect_result));
                    }
                } else {
                    ESP_LOGI(TAG, "WiFi already connecting/connected, skipping connect call");
                }
                break;
                
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGW(TAG, "WiFi STA disconnected");
                g_wifi_connected = false;
                g_wifi_state = WIFI_STATE_DISCONNECTED;
                memset(g_ip_address, 0, sizeof(g_ip_address));
                
                if (s_retry_num < 3) {
                    esp_wifi_connect();
                    s_retry_num++;
                    ESP_LOGI(TAG, "Retrying WiFi connection (%d/3)", s_retry_num);
                    g_wifi_state = WIFI_STATE_CONNECTING;
                } else {
                    ESP_LOGE(TAG, "WiFi connection failed after 3 attempts, switching to AP mode");
                    s_retry_num = 0;
                    // Switch to AP mode will be handled by the monitoring task
                }
                break;
                
            case WIFI_EVENT_AP_START:
                ESP_LOGI(TAG, "WiFi AP started - SenseCAP SmartBin Configuration Portal");
                g_wifi_state = WIFI_STATE_AP_MODE;
                
                // Start mDNS for configuration portal access (AP mode)
                webserver_start_mdns();
                
                break;
                
            case WIFI_EVENT_AP_STOP:
                ESP_LOGI(TAG, "WiFi AP stopped");
                break;
                
            case WIFI_EVENT_AP_STACONNECTED:
                ESP_LOGI(TAG, "Client connected to configuration portal");
                break;
                
            case WIFI_EVENT_AP_STADISCONNECTED:
                ESP_LOGI(TAG, "Client disconnected from configuration portal");
                break;
                
            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        switch (event_id) {
            case IP_EVENT_STA_GOT_IP: {
                ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
                snprintf(g_ip_address, sizeof(g_ip_address), IPSTR, IP2STR(&event->ip_info.ip));
                ESP_LOGI(TAG, "[CONNECTED] WiFi connected! IP: %s", g_ip_address);
                g_wifi_connected = true;
                g_wifi_state = WIFI_STATE_CONNECTED;
                s_retry_num = 0; // Reset retry counter on successful connection
                
                // Start mDNS for smartbin.local access
                webserver_start_mdns();
                
                ui_wifi_connected(); // Update UI
                break;
            }
                
            default:
                break;
        }
    }
}

static void wifi_monitoring_task(void *pvParameters) {
    TickType_t last_check = xTaskGetTickCount();
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000)); // Check every 5 seconds
        
        // If we've been disconnected for too long, switch to AP mode
        if (g_wifi_state == WIFI_STATE_DISCONNECTED) {
            TickType_t current_time = xTaskGetTickCount();
            if ((current_time - last_check) > pdMS_TO_TICKS(WIFI_CHECK_INTERVAL_MS)) {
                ESP_LOGW(TAG, "No WiFi connection for extended period, starting configuration portal");
                smartbin_wifi_start_ap_mode();
                last_check = current_time;
            }
        } else {
            last_check = xTaskGetTickCount();
        }
    }
}

esp_err_t smartbin_wifi_init(void) {
    ESP_LOGI(TAG, "Initializing WiFi management system...");
    
    // Initialize network interface
    ESP_ERROR_CHECK(esp_netif_init());
    
    // Create default WiFi interfaces
    g_sta_netif = esp_netif_create_default_wifi_sta();
    g_ap_netif = esp_netif_create_default_wifi_ap();
    
    if (!g_sta_netif || !g_ap_netif) {
        ESP_LOGE(TAG, "Failed to create WiFi network interfaces");
        return ESP_FAIL;
    }
    
    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    
    // Set storage type
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    
    // Start monitoring task
    xTaskCreate(wifi_monitoring_task, "wifi_monitor", 4096, NULL, 5, &g_wifi_task_handle);
    
    ESP_LOGI(TAG, "WiFi management system initialized");
    return ESP_OK;
}

esp_err_t smartbin_wifi_start_ap_mode(void) {
    ESP_LOGI(TAG, "Starting AP mode for device configuration...");
    
    // Stop WiFi if running
    esp_wifi_stop();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Generate unique AP SSID using MAC address
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_AP, mac);
    
    char ap_ssid[32];
    snprintf(ap_ssid, sizeof(ap_ssid), "%s%02X%02X", AP_SSID_PREFIX, mac[4], mac[5]);
    
    wifi_config_t ap_config = {};
    ap_config.ap.ssid_len = 0;
    ap_config.ap.channel = 1;
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap_config.ap.ssid_hidden = 0;
    ap_config.ap.max_connection = AP_MAX_CONNECTIONS;
    ap_config.ap.beacon_interval = 100;
    ap_config.ap.pairwise_cipher = WIFI_CIPHER_TYPE_CCMP;
    ap_config.ap.ftm_responder = false;
    ap_config.ap.pmf_cfg.required = false;
    ap_config.ap.pmf_cfg.capable = true;
    ap_config.ap.sae_pwe_h2e = WPA3_SAE_PWE_UNSPECIFIED;
    strcpy((char*)ap_config.ap.password, AP_PASSWORD);
    
    strcpy((char*)ap_config.ap.ssid, ap_ssid);
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "[CONFIG] Configuration Portal Active");
    ESP_LOGI(TAG, "[WIFI] SSID: %s", ap_ssid);
    ESP_LOGI(TAG, "[KEY] Password: %s", AP_PASSWORD);
    ESP_LOGI(TAG, "[WEB] Connect and visit: http://192.168.4.1");
    
    ui_wifi_config_mode(); // Update UI to show config mode
    
    return ESP_OK;
}

esp_err_t smartbin_wifi_start_sta_mode(const char* ssid, const char* password) {
    if (!ssid) {
        ESP_LOGE(TAG, "SSID cannot be NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Connecting to WiFi: %s (keeping config portal active)", ssid);
    
    // Keep webserver running - don't stop it!
    // Use APSTA mode to maintain both AP (for config portal) and STA (for internet)
    
    // Configure STA mode
    wifi_config_t sta_config = {};
    strncpy((char*)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid) - 1);
    if (password) {
        strncpy((char*)sta_config.sta.password, password, sizeof(sta_config.sta.password) - 1);
    }
    
    // Switch to APSTA mode to keep both AP and STA active
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    
    // WiFi should already be started from AP mode
    esp_err_t result = esp_wifi_start();
    if (result != ESP_OK && result != ESP_ERR_WIFI_STATE) {
        ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(result));
        return result;
    }
    
    // Connection will be triggered automatically by WIFI_EVENT_STA_START event
    // No need to call esp_wifi_connect() here as it's handled by the event handler
    
    ui_wifi_connecting(); // Update UI
    
    return ESP_OK;
}

smartbin_wifi_state_t smartbin_wifi_get_state(void) {
    return g_wifi_state;
}

bool smartbin_wifi_is_connected(void) {
    return g_wifi_connected;
}

char* smartbin_wifi_get_ip_address(void) {
    return g_ip_address;
}

char* smartbin_wifi_get_ap_ip_address(void) {
    static char ap_ip[16] = {0};
    
    if (g_ap_netif) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(g_ap_netif, &ip_info) == ESP_OK) {
            snprintf(ap_ip, sizeof(ap_ip), IPSTR, IP2STR(&ip_info.ip));
            return ap_ip;
        }
    }
    
    // Fallback to default AP IP
    strcpy(ap_ip, "192.168.4.1");
    return ap_ip;
}

esp_err_t smartbin_wifi_connect(void) {
    // Always start webserver first for configuration access
    ESP_LOGI(TAG, "Starting web server for configuration access...");
    webserver_init();
    
    // Try to read stored WiFi credentials
    smartbin_config_t config = {};
    if (webserver_get_config(&config) == ESP_OK && strlen(config.wifi_ssid) > 0) {
        ESP_LOGI(TAG, "Found stored WiFi credentials, attempting connection...");
        return smartbin_wifi_start_sta_mode(config.wifi_ssid, config.wifi_password);
    } else {
        ESP_LOGI(TAG, "No stored WiFi credentials, starting configuration portal");
        return smartbin_wifi_start_ap_mode();
    }
}

esp_err_t smartbin_wifi_wait_connection(uint32_t timeout_ms) {
    uint32_t elapsed = 0;
    const uint32_t check_interval = 100;
    
    while (elapsed < timeout_ms) {
        if (g_wifi_connected) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(check_interval));
        elapsed += check_interval;
    }
    
    return ESP_ERR_TIMEOUT;
}

void smartbin_wifi_check_and_switch_mode(void) {
    // Called by webserver after successful configuration
    smartbin_config_t config = {};
    if (webserver_get_config(&config) == ESP_OK && strlen(config.wifi_ssid) > 0) {
        ESP_LOGI(TAG, "New WiFi configuration received, switching to STA mode");
        smartbin_wifi_start_sta_mode(config.wifi_ssid, config.wifi_password);
    }
}

// Legacy functions for compatibility with existing code
void oai_wifi_init(void) {
    smartbin_wifi_init();
}

void oai_wifi(void) {
    smartbin_wifi_connect();
    
    // Wait for connection or AP mode
    uint32_t wait_time = 0;
    const uint32_t max_wait = 30000; // 30 seconds
    
    while (wait_time < max_wait) {
        if (g_wifi_connected || g_wifi_state == WIFI_STATE_AP_MODE) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
        wait_time += 200;
    }
    
    if (g_wifi_connected) {
        ESP_LOGI(TAG, "[SUCCESS] WiFi connected successfully");
    } else if (g_wifi_state == WIFI_STATE_AP_MODE) {
        ESP_LOGI(TAG, "[CONFIG] Running in configuration mode");
    } else {
        ESP_LOGW(TAG, "⚠️ WiFi connection timeout, check configuration");
    }
}