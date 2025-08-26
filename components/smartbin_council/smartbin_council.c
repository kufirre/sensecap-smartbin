#include "smartbin_council.h"

#include <esp_log.h>
#include <esp_http_client.h>
#include <cJSON.h>
#include <string.h>

static const char *TAG = "SMARTBIN_COUNCIL";

// HTTP event handler for council API requests
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    static int output_len = 0;

    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (!esp_http_client_is_chunked_response(evt->client)) {
                if (evt->user_data) {
                    memcpy((char*)evt->user_data + output_len, evt->data, evt->data_len);
                    output_len += evt->data_len;
                }
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            output_len = 0;
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            output_len = 0;
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
            break;
    }
    return ESP_OK;
}

esp_err_t smartbin_council_get_info(const char *postcode, smartbin_council_info_t *info)
{
    if (!postcode || !info) {
        return ESP_ERR_INVALID_ARG;
    }

    char url[256];
    snprintf(url, sizeof(url), "https://api.postcodes.io/postcodes/%s", postcode);

    char *response_buffer = (char*)malloc(4096);
    if (!response_buffer) {
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_config_t config = {};
    config.url = url;
    config.event_handler = http_event_handler;
    config.user_data = response_buffer;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        if (status_code == 200) {
            cJSON *json = cJSON_Parse(response_buffer);
            if (json) {
                cJSON *result = cJSON_GetObjectItem(json, "result");
                if (result) {
                    cJSON *admin_district = cJSON_GetObjectItem(result, "admin_district");
                    cJSON *region = cJSON_GetObjectItem(result, "region");
                    cJSON *country = cJSON_GetObjectItem(result, "country");

                    if (admin_district && cJSON_IsString(admin_district)) {
                        strncpy(info->council_name, admin_district->valuestring, sizeof(info->council_name) - 1);
                    }
                    if (region && cJSON_IsString(region)) {
                        strncpy(info->region, region->valuestring, sizeof(info->region) - 1);
                    }
                    if (country && cJSON_IsString(country)) {
                        strncpy(info->country, country->valuestring, sizeof(info->country) - 1);
                    }
                    
                    err = ESP_OK;
                } else {
                    err = ESP_ERR_NOT_FOUND;
                }
                cJSON_Delete(json);
            } else {
                err = ESP_ERR_INVALID_RESPONSE;
            }
        } else {
            ESP_LOGW(TAG, "HTTP GET failed with status: %d", status_code);
            err = ESP_ERR_INVALID_RESPONSE;
        }
    }

    esp_http_client_cleanup(client);
    free(response_buffer);
    return err;
}

esp_err_t smartbin_api_init(void)
{
    ESP_LOGI(TAG, "API client initialized");
    return ESP_OK;
}

esp_err_t smartbin_api_analyze_waste(const uint8_t *image_data, size_t image_len, smartbin_api_result_t *result)
{
    ESP_LOGW(TAG, "Waste analysis API not implemented yet");
    if (result) {
        result->confidence = 0.0f;
        strcpy(result->category, "unknown");
    }
    return ESP_ERR_NOT_SUPPORTED;
}