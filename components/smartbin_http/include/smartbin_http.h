#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "esp_http_client.h"
#include <stdint.h>
#include <stddef.h>

/**
 * @brief Perform OpenAI multimodal waste analysis
 * 
 * Sends image data to OpenAI GPT-4o with vision for waste classification
 * and disposal advice, returning audio response for UK postcode.
 * 
 * @param image_data    JPEG image data buffer
 * @param image_len     Size of image data in bytes
 * @param postcode      UK postcode for location-specific advice (can be NULL)
 * @param audio_data    Output pointer for decoded audio data (caller must free)
 * @param audio_len     Output pointer for audio data length
 * @return ESP_OK on success, error code on failure
 */
esp_err_t smartbin_http_analyze_waste(const uint8_t* image_data, size_t image_len, 
                                     const char* postcode, uint8_t** audio_data, size_t* audio_len);

/**
 * @brief Perform OpenAI waste analysis with text response
 * 
 * Sends image data to OpenAI GPT-4o with vision for waste classification
 * and disposal advice, returning text response for UK postcode.
 * 
 * @param image_data    JPEG image data buffer
 * @param image_len     Size of image data in bytes  
 * @param postcode      UK postcode for location-specific advice (can be NULL)
 * @param response_text Output pointer for response text (caller must free)
 * @return ESP_OK on success, error code on failure
 */
esp_err_t smartbin_http_analyze_waste_text(const uint8_t* image_data, size_t image_len, 
                                          const char* postcode, char** response_text);


/**
 * @brief Send WebRTC offer to OpenAI Realtime API (legacy)
 * 
 * Original function for realtime audio communication with OpenAI.
 * Used by existing WebRTC implementation.
 * 
 * @param offer     SDP offer string
 * @param answer    Buffer for SDP answer response
 */
void smartbin_http_send_webrtc_offer(char *offer, char *answer);

/**
 * @brief HTTP event handler for ESP-IDF HTTP client
 * 
 * Common HTTP event handler that can be used by other components
 * for HTTP requests with response buffering.
 * 
 * @param evt HTTP client event structure
 * @return ESP_OK on success
 */
esp_err_t smartbin_http_event_handler(esp_http_client_event_t *evt);

/**
 * @brief Encode binary data to base64 string
 * 
 * @param data Input binary data
 * @param input_length Length of input data
 * @return Allocated base64 string (caller must free), or NULL on error
 */
char* smartbin_http_base64_encode(const uint8_t* data, size_t input_length);

/**
 * @brief Get council name for a postcode using gov.uk API
 * 
 * @param postcode UK postcode (e.g., "SW1A 1AA") 
 * @param council_name Buffer to store council name
 * @param max_len Maximum length of council_name buffer
 * @return esp_err_t ESP_OK on success, ESP_ERR_NOT_FOUND if postcode not found
 */
esp_err_t smartbin_http_get_council_name(const char* postcode, char* council_name, size_t max_len);

#ifdef __cplusplus
}
#endif