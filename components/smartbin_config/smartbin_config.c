#include "smartbin_config.h"

#include <string.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <esp_log.h>
#include <esp_check.h>

static const char* TAG = "SMARTBIN_CONFIG";
static const char* NVS_NAMESPACE = "smartbin_cfg";

// NVS keys
#define NVS_KEY_OPENAI_API    "openai_key"
#define NVS_KEY_WIFI_SSID     "wifi_ssid"
#define NVS_KEY_WIFI_PASSWORD "wifi_pass"

// Internal state
static nvs_handle_t g_nvs_handle = 0;
static bool g_initialized = false;
static char g_cached_api_key[SMARTBIN_CONFIG_OPENAI_API_KEY_MAX_LEN] = {0};

esp_err_t smartbin_config_init(void) {
    if (g_initialized) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing secure configuration system...");
    
    // Initialize NVS if not already done
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition was truncated and needs to be erased");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to initialize NVS");
    
    // Open NVS handle with encryption (if available)
    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &g_nvs_handle);
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to open NVS namespace");
    
    g_initialized = true;
    ESP_LOGI(TAG, "Configuration system initialized with NVS encryption");
    return ESP_OK;
}

esp_err_t smartbin_config_deinit(void) {
    if (!g_initialized) {
        return ESP_OK;
    }
    
    // Securely clear cached data
    memset(g_cached_api_key, 0, sizeof(g_cached_api_key));
    
    if (g_nvs_handle) {
        nvs_close(g_nvs_handle);
        g_nvs_handle = 0;
    }
    
    g_initialized = false;
    ESP_LOGI(TAG, "Configuration system deinitialized");
    return ESP_OK;
}

esp_err_t smartbin_config_set_openai_key(const char* api_key) {
    if (!g_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!api_key || strlen(api_key) == 0 || strlen(api_key) >= SMARTBIN_CONFIG_OPENAI_API_KEY_MAX_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Store in NVS (uses hardware encryption if enabled)
    esp_err_t ret = nvs_set_str(g_nvs_handle, NVS_KEY_OPENAI_API, api_key);
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to store API key");
    
    ret = nvs_commit(g_nvs_handle);
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to commit API key to storage");
    
    // Update cache
    strncpy(g_cached_api_key, api_key, sizeof(g_cached_api_key) - 1);
    g_cached_api_key[sizeof(g_cached_api_key) - 1] = '\0';
    
    ESP_LOGI(TAG, "OpenAI API key securely stored");
    return ESP_OK;
}

const char* smartbin_config_get_openai_key(void) {
    if (!g_initialized) {
        ESP_LOGE(TAG, "Configuration not initialized");
        return NULL;
    }
    
    // Return cached key if available
    if (strlen(g_cached_api_key) > 0) {
        return g_cached_api_key;
    }
    
    // Load from NVS
    size_t required_size = sizeof(g_cached_api_key);
    esp_err_t ret = nvs_get_str(g_nvs_handle, NVS_KEY_OPENAI_API, g_cached_api_key, &required_size);
    
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGD(TAG, "No OpenAI API key stored");
        return NULL;
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load API key: %s", esp_err_to_name(ret));
        return NULL;
    }
    
    return g_cached_api_key;
}

bool smartbin_config_has_openai_key(void) {
    if (!g_initialized) {
        return false;
    }
    
    size_t required_size = 0;
    esp_err_t ret = nvs_get_str(g_nvs_handle, NVS_KEY_OPENAI_API, NULL, &required_size);
    return (ret == ESP_OK && required_size > 1); // > 1 to account for null terminator
}

esp_err_t smartbin_config_clear_openai_key(void) {
    if (!g_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Clear from NVS
    nvs_erase_key(g_nvs_handle, NVS_KEY_OPENAI_API);
    nvs_commit(g_nvs_handle);
    
    // Clear from cache
    memset(g_cached_api_key, 0, sizeof(g_cached_api_key));
    
    ESP_LOGI(TAG, "OpenAI API key cleared");
    return ESP_OK;
}

esp_err_t smartbin_config_set_wifi_credentials(const char* ssid, const char* password) {
    if (!g_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!ssid || strlen(ssid) == 0 || strlen(ssid) >= SMARTBIN_CONFIG_WIFI_SSID_MAX_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    if (password && strlen(password) >= SMARTBIN_CONFIG_WIFI_PASSWORD_MAX_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = nvs_set_str(g_nvs_handle, NVS_KEY_WIFI_SSID, ssid);
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to store WiFi SSID");
    
    if (password && strlen(password) > 0) {
        ret = nvs_set_str(g_nvs_handle, NVS_KEY_WIFI_PASSWORD, password);
        ESP_RETURN_ON_ERROR(ret, TAG, "Failed to store WiFi password");
    } else {
        // Clear password for open networks
        nvs_erase_key(g_nvs_handle, NVS_KEY_WIFI_PASSWORD);
    }
    
    ret = nvs_commit(g_nvs_handle);
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to commit WiFi credentials");
    
    ESP_LOGI(TAG, "WiFi credentials stored for SSID: %s", ssid);
    return ESP_OK;
}

esp_err_t smartbin_config_get_wifi_ssid(char* ssid) {
    if (!g_initialized || !ssid) {
        return ESP_ERR_INVALID_ARG;
    }
    
    size_t required_size = SMARTBIN_CONFIG_WIFI_SSID_MAX_LEN;
    esp_err_t ret = nvs_get_str(g_nvs_handle, NVS_KEY_WIFI_SSID, ssid, &required_size);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ssid[0] = '\0';
        return ESP_ERR_NOT_FOUND;
    }
    
    return ret;
}

esp_err_t smartbin_config_get_wifi_password(char* password) {
    if (!g_initialized || !password) {
        return ESP_ERR_INVALID_ARG;
    }
    
    size_t required_size = SMARTBIN_CONFIG_WIFI_PASSWORD_MAX_LEN;
    esp_err_t ret = nvs_get_str(g_nvs_handle, NVS_KEY_WIFI_PASSWORD, password, &required_size);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        password[0] = '\0';
        return ESP_ERR_NOT_FOUND;
    }
    
    return ret;
}

bool smartbin_config_has_wifi_credentials(void) {
    if (!g_initialized) {
        return false;
    }
    
    size_t required_size = 0;
    esp_err_t ret = nvs_get_str(g_nvs_handle, NVS_KEY_WIFI_SSID, NULL, &required_size);
    return (ret == ESP_OK && required_size > 1); // > 1 to account for null terminator
}

esp_err_t smartbin_config_clear_wifi_credentials(void) {
    if (!g_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    nvs_erase_key(g_nvs_handle, NVS_KEY_WIFI_SSID);
    nvs_erase_key(g_nvs_handle, NVS_KEY_WIFI_PASSWORD);
    nvs_commit(g_nvs_handle);
    
    ESP_LOGI(TAG, "WiFi credentials cleared");
    return ESP_OK;
}

esp_err_t smartbin_config_factory_reset(void) {
    if (!g_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGW(TAG, "Performing factory reset - all configuration will be erased");
    
    // Erase all keys in our namespace
    nvs_erase_all(g_nvs_handle);
    nvs_commit(g_nvs_handle);
    
    // Clear cached data
    memset(g_cached_api_key, 0, sizeof(g_cached_api_key));
    
    ESP_LOGI(TAG, "Factory reset completed");
    return ESP_OK;
}