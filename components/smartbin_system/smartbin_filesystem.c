#include "smartbin_system.h"

#include <esp_log.h>
#include <esp_vfs.h>
#include <esp_littlefs.h>

static const char *TAG = "SMARTBIN_FILESYSTEM";

esp_err_t smartbin_filesystem_init(void)
{
    ESP_LOGI(TAG, "Initializing LittleFS for web assets...");
    
    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/littlefs",
        .partition_label = "storage", 
        .format_if_mount_failed = true,
        .dont_mount = false,
    };
    
    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find LittleFS partition 'storage'");
        } else {
            ESP_LOGE(TAG, "Failed to initialize LittleFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }
    
    size_t total = 0, used = 0;
    ret = esp_littlefs_info(conf.partition_label, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get LittleFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "LittleFS mounted: %d kB total, %d kB used", total / 1024, used / 1024);
    }
    
    return ESP_OK;
}

esp_err_t smartbin_filesystem_deinit(void)
{
    ESP_LOGI(TAG, "Unmounting LittleFS...");
    esp_err_t ret = esp_vfs_littlefs_unregister("storage");
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to unmount LittleFS (%s)", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "LittleFS unmounted successfully");
    return ESP_OK;
}

esp_err_t smartbin_filesystem_get_info(size_t *total_bytes, size_t *used_bytes)
{
    if (!total_bytes || !used_bytes) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = esp_littlefs_info("storage", total_bytes, used_bytes);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get filesystem info (%s)", esp_err_to_name(ret));
        return ret;
    }
    
    return ESP_OK;
}