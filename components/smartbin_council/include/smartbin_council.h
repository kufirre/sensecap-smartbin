#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <esp_err.h>
#include <stdint.h>
#include <stddef.h>

/**
 * @brief Council information structure
 * Contains UK postcode lookup results
 */
typedef struct {
    char council_name[128];
    char region[64]; 
    char country[64];
} smartbin_council_info_t;

/**
 * @brief API analysis result structure
 * Contains waste analysis results
 */
typedef struct {
    char category[64];
    float confidence;
} smartbin_api_result_t;

/**
 * @brief Get council information for a UK postcode
 * 
 * Performs HTTP lookup using postcodes.io API to get council
 * and location information for recycling guidance.
 * 
 * @param postcode UK postcode (e.g., "SW1A 1AA")
 * @param info Output structure for council information
 * @return ESP_OK on success, error code on failure
 */
esp_err_t smartbin_council_get_info(const char *postcode, smartbin_council_info_t *info);

/**
 * @brief Initialize API client (placeholder)
 * 
 * Initializes any required API client resources.
 * Currently a placeholder for future API integrations.
 * 
 * @return ESP_OK on success
 */
esp_err_t smartbin_api_init(void);

/**
 * @brief Analyze waste from image data (placeholder)
 * 
 * Placeholder function for future waste analysis API integration.
 * Currently returns ESP_ERR_NOT_SUPPORTED.
 * 
 * @param image_data JPEG image data
 * @param image_len Size of image data
 * @param result Output structure for analysis results
 * @return ESP_ERR_NOT_SUPPORTED (not implemented)
 */
esp_err_t smartbin_api_analyze_waste(const uint8_t *image_data, size_t image_len, smartbin_api_result_t *result);

#ifdef __cplusplus
}
#endif