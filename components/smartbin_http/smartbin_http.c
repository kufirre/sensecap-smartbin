#include "smartbin_http.h"

#include <esp_http_client.h>
#include <esp_log.h>
#include <string.h>
#include <cJSON.h>
#include <smartbin_config.h>
#include <smartbin_webserver.h>
#include <smartbin_system.h>
#include <mbedtls/base64.h>
#include <esp_heap_caps.h>

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

#define MAX_HTTP_OUTPUT_BUFFER 8192

static const char *TAG = "SMARTBIN_HTTP";

// Using secure config component for API key access

// Use mbedtls for reliable base64 encoding
char* smartbin_http_base64_encode(const uint8_t* data, size_t input_length) {
  if (!data || input_length == 0) {
    ESP_LOGE(TAG, "Invalid input for base64 encoding");
    return NULL;
  }
  
  // Check available heap before allocation
  size_t free_heap = esp_get_free_heap_size();
  size_t output_length = 4 * ((input_length + 2) / 3) + 1; // +1 for null terminator
  
  if (free_heap < output_length + 1024) { // Keep 1KB buffer
    ESP_LOGE(TAG, "Insufficient heap: need %zu bytes, have %zu bytes", output_length, free_heap);
    return NULL;
  }
  
  // Use heap caps to allocate from DRAM
  char* encoded_data = (char*)heap_caps_malloc(output_length, MALLOC_CAP_8BIT);
  if (!encoded_data) {
    ESP_LOGE(TAG, "Failed to allocate %zu bytes for base64 encoding", output_length);
    return NULL;
  }
  
  size_t olen = 0;
  int ret = mbedtls_base64_encode((unsigned char*)encoded_data, output_length, &olen, data, input_length);
  
  if (ret != 0) {
    ESP_LOGE(TAG, "mbedtls_base64_encode failed with error: %d", ret);
    heap_caps_free(encoded_data);
    return NULL;
  }
  
  ESP_LOGD(TAG, "Base64 encoded %zu bytes → %zu chars (heap free: %zu)", 
           input_length, olen, esp_get_free_heap_size());
  return encoded_data;
}

esp_err_t smartbin_http_event_handler(esp_http_client_event_t *evt) {
  static int output_len;
  switch (evt->event_id) {
    case HTTP_EVENT_REDIRECT:
      ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
      esp_http_client_set_header(evt->client, "From", "user@sensecap-smartbin.com");
      esp_http_client_set_header(evt->client, "User-Agent", "SenseCAP-SmartBin");
      break;
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
      ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s",
               evt->header_key, evt->header_value);
      break;
    case HTTP_EVENT_ON_DATA: {
      ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
      
      // Support both chunked and non-chunked responses for REST APIs
      if (esp_http_client_is_chunked_response(evt->client)) {
        ESP_LOGD(TAG, "Handling chunked HTTP response");
      }

      if (output_len == 0 && evt->user_data) {
        memset(evt->user_data, 0, MAX_HTTP_OUTPUT_BUFFER);
      }

      // If user_data buffer is configured, copy the response into the buffer
      int copy_len = 0;
      if (evt->user_data) {
        // The last byte in evt->user_data is kept for the NULL character in
        // case of out-of-bound access.
        copy_len = MIN(evt->data_len, (MAX_HTTP_OUTPUT_BUFFER - output_len));
        if (copy_len) {
          memcpy(((char *)evt->user_data) + output_len, evt->data, copy_len);
        }
      }
      output_len += copy_len;

      break;
    }
    case HTTP_EVENT_ON_FINISH:
      ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
      output_len = 0;
      break;
    case HTTP_EVENT_DISCONNECTED:
      ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
      output_len = 0;
      break;
  }
  return ESP_OK;
}

void smartbin_http_send_webrtc_offer(char *offer, char *answer) {
  esp_http_client_config_t config;
  memset(&config, 0, sizeof(esp_http_client_config_t));

  config.url = "https://api.openai.com/v1/realtime?model=gpt-4o-mini-realtime-preview-2024-12-17";
  config.event_handler = smartbin_http_event_handler;
  config.user_data = answer;

  // Get API key from webserver config
  smartbin_config_t smartbin_cfg = {};
  esp_err_t config_err = smartbin_webserver_get_config(&smartbin_cfg);
  if (config_err != ESP_OK || strlen(smartbin_cfg.openai_api_key) == 0) {
    ESP_LOGE(TAG, "OpenAI API key not configured");
    return;
  }
  const char* api_key = smartbin_cfg.openai_api_key;
  
  // Prepare authorization header
  char auth_header[300];  // Increased size for longer keys
  snprintf(auth_header, sizeof(auth_header), "Bearer %s", api_key);

  esp_http_client_handle_t client = esp_http_client_init(&config);
  esp_http_client_set_method(client, HTTP_METHOD_POST);
  esp_http_client_set_header(client, "Content-Type", "application/sdp");
  esp_http_client_set_header(client, "Authorization", auth_header);
  esp_http_client_set_post_field(client, offer, strlen(offer));

  ESP_LOGI(TAG, "📤 Sending WebRTC offer to OpenAI (%d bytes)", strlen(offer));
  ESP_LOGD(TAG, "Offer SDP:\n%s", offer);
  
  esp_err_t err = esp_http_client_perform(client);
  int status_code = esp_http_client_get_status_code(client);
  int content_length = esp_http_client_get_content_length(client);
  
  if (err != ESP_OK || status_code != 201) {
    ESP_LOGE(TAG, "❌ HTTP request failed: %s, status: %d", esp_err_to_name(err), status_code);
    esp_http_client_cleanup(client);
    return;
  }

  ESP_LOGI(TAG, "✅ OpenAI response: %d bytes, status: %d", content_length, status_code);
  ESP_LOGD(TAG, "Answer SDP:\n%s", answer);
  esp_http_client_cleanup(client);
}

esp_err_t smartbin_http_analyze_waste(const uint8_t* image_data, size_t image_len, 
                                     const char* postcode, uint8_t** audio_data, size_t* audio_len) {
  ESP_LOGI(TAG, "Starting multimodal waste analysis");
  
  // Check heap status first
  size_t free_heap = esp_get_free_heap_size();
  size_t min_free_heap = esp_get_minimum_free_heap_size();
  ESP_LOGI(TAG, "Heap status: %zu free, %zu minimum", free_heap, min_free_heap);
  
  // Validate inputs
  if (!image_data || image_len == 0 || !audio_data || !audio_len) {
    ESP_LOGE(TAG, "Invalid parameters");
    return ESP_ERR_INVALID_ARG;
  }
  
  // Check if we have enough heap for the operation (rough estimate)
  size_t estimated_need = image_len * 2 + 8192; // base64 + JSON + buffers
  if (free_heap < estimated_need) {
    ESP_LOGE(TAG, "Insufficient heap: need ~%zu, have %zu", estimated_need, free_heap);
    return ESP_ERR_NO_MEM;
  }
  
  ESP_LOGI(TAG, "Processing image: %zu bytes, postcode: %s", image_len, postcode ? postcode : "none");
  
  if (!postcode) {
    ESP_LOGW(TAG, "⚠️ No postcode provided - using generic UK advice");
  }
  
  // Step 1: Encode image as base64
  ESP_LOGI(TAG, "Encoding image to base64 (%zu bytes)...", image_len);
  char* base64_image = smartbin_http_base64_encode(image_data, image_len);
  if (!base64_image) {
    ESP_LOGE(TAG, "Failed to encode image to base64");
    return ESP_ERR_NO_MEM;
  }
  ESP_LOGI(TAG, "Image encoded successfully (heap: %zu free)", esp_get_free_heap_size());
  
  // Step 2: Build JSON request
  ESP_LOGI(TAG, "🔧 Building multimodal API request...");
  
  // Create main JSON object for multimodal API
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "model", "gpt-4o-audio-preview");  // Multimodal model with audio output
  
  // Add modalities for audio output
  cJSON *modalities = cJSON_CreateArray();
  cJSON_AddItemToArray(modalities, cJSON_CreateString("text"));
  cJSON_AddItemToArray(modalities, cJSON_CreateString("audio"));
  cJSON_AddItemToObject(root, "modalities", modalities);
  
  // Add audio settings
  cJSON *audio = cJSON_CreateObject();
  cJSON_AddStringToObject(audio, "voice", "nova");
  cJSON_AddStringToObject(audio, "format", "opus");
  cJSON_AddItemToObject(root, "audio", audio);
  
  // Build prompt text with postcode  
  char prompt[1024];
  // snprintf(prompt, sizeof(prompt), 
  //   "You are a UK waste disposal assistant. Analyze the MAIN waste item in this image and provide disposal advice for postcode %s. "
  //   "Focus on the most prominent object, ignore background items. Most of the time, a hand will be captured holding the waste item. this is an important clue"
  //   "Keep response brief and clear. "
  //   "Format: '{Item} goes in {Color} bin. {Brief reason}.'",
  //   postcode ? postcode : "UK");

  snprintf(prompt, sizeof(prompt), 
    "You are a UK waste disposal assistant. Analyze the MAIN waste item in this image and provide disposal advice for postcode %s. "
    "Most of the time, a hand will be captured holding the waste item. this is an IMPORTANT clue. Check council information on advise on how to dispose waste detected (or object held to be disposed)"
    "Some times, the item to be disposed doesn't look like trash; use the fact that it is being hold to know it is meant to be disposed"
    "Keep response brief and clear. "
    "Format: '{Item} goes in {Color} bin. {Brief reason}.'",
    postcode ? postcode : "UK");
  
  // Add messages array
  cJSON *messages = cJSON_CreateArray();
  cJSON *message = cJSON_CreateObject();
  cJSON_AddStringToObject(message, "role", "user");
  
  // Create content array
  cJSON *content = cJSON_CreateArray();
  
  // Add text content
  cJSON *text_content = cJSON_CreateObject();
  cJSON_AddStringToObject(text_content, "type", "text");
  cJSON_AddStringToObject(text_content, "text", prompt);
  cJSON_AddItemToArray(content, text_content);
  
  // Add image content
  cJSON *image_content = cJSON_CreateObject();
  cJSON_AddStringToObject(image_content, "type", "image_url");
  cJSON *image_url = cJSON_CreateObject();
  
  // Build data URL
  size_t data_url_len = strlen("data:image/jpeg;base64,") + strlen(base64_image) + 1;
  char *data_url = (char*)heap_caps_malloc(data_url_len, MALLOC_CAP_8BIT);
  if (!data_url) {
    heap_caps_free(base64_image);
    cJSON_Delete(root);
    return ESP_ERR_NO_MEM;
  }
  snprintf(data_url, data_url_len, "data:image/jpeg;base64,%s", base64_image);
  
  cJSON_AddStringToObject(image_url, "url", data_url);
  cJSON_AddStringToObject(image_url, "detail", "high");
  cJSON_AddItemToObject(image_content, "image_url", image_url);
  cJSON_AddItemToArray(content, image_content);
  
  cJSON_AddItemToObject(message, "content", content);
  cJSON_AddItemToArray(messages, message);
  cJSON_AddItemToObject(root, "messages", messages);
  
  // Add completion tokens limit
  cJSON_AddNumberToObject(root, "max_completion_tokens", 100);
  
  // Convert to JSON string
  char *json_string = cJSON_PrintUnformatted(root);
  if (!json_string) {
    heap_caps_free(base64_image);
    heap_caps_free(data_url);
    cJSON_Delete(root);
    return ESP_ERR_NO_MEM;
  }
  
  ESP_LOGI(TAG, "✅ JSON request built: %d bytes", strlen(json_string));
  ESP_LOGD(TAG, "📤 Request JSON: %s", json_string);
  
  // Step 3: Send HTTP request to OpenAI Chat API
  ESP_LOGI(TAG, "🌐 Sending request to OpenAI Chat API...");
  
  char response_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};
  
  esp_http_client_config_t config = {};
  config.url = "https://api.openai.com/v1/chat/completions";
  config.event_handler = smartbin_http_event_handler;
  config.user_data = response_buffer;
  config.timeout_ms = 30000;  // 30 second timeout
  
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    ESP_LOGE(TAG, "❌ Failed to initialize HTTP client");
    heap_caps_free(base64_image);
    heap_caps_free(data_url);
    free(json_string);
    cJSON_Delete(root);
    return ESP_FAIL;
  }
  
  // Set request method and headers
  esp_http_client_set_method(client, HTTP_METHOD_POST);
  esp_http_client_set_header(client, "Content-Type", "application/json");
  
  // Get API key from webserver config
  smartbin_config_t smartbin_cfg = {};
  esp_err_t config_err = smartbin_webserver_get_config(&smartbin_cfg);
  if (config_err != ESP_OK || strlen(smartbin_cfg.openai_api_key) == 0) {
    ESP_LOGE(TAG, "OpenAI API key not configured");
    esp_http_client_cleanup(client);
    return ESP_ERR_INVALID_STATE;
  }
  const char* api_key = smartbin_cfg.openai_api_key;
  
  // Add authorization header with API key
  char auth_header[300];  // Increased size for longer keys
  snprintf(auth_header, sizeof(auth_header), "Bearer %s", api_key);
  esp_http_client_set_header(client, "Authorization", auth_header);
  
  // Set POST data
  esp_http_client_set_post_field(client, json_string, strlen(json_string));
  
  // Perform the request
  esp_err_t err = esp_http_client_perform(client);
  int status_code = esp_http_client_get_status_code(client);
  int content_length = esp_http_client_get_content_length(client);
  
  ESP_LOGI(TAG, "📡 HTTP Response: status=%d, content_length=%d, err=%s", 
           status_code, content_length, esp_err_to_name(err));
  
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "❌ HTTP request failed: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    heap_caps_free(base64_image);
    heap_caps_free(data_url);
    free(json_string);
    cJSON_Delete(root);
    return err;
  }
  
  if (status_code != 200) {
    ESP_LOGE(TAG, "❌ OpenAI API error: status %d", status_code);
    ESP_LOGE(TAG, "Response: %s", response_buffer);
    esp_http_client_cleanup(client);
    heap_caps_free(base64_image);
    heap_caps_free(data_url);
    free(json_string);
    cJSON_Delete(root);
    return ESP_FAIL;
  }
  
  ESP_LOGI(TAG, "✅ OpenAI API success: %d bytes received", strlen(response_buffer));
  ESP_LOGD(TAG, "📥 Response: %s", response_buffer);
  
  // Step 4: Parse response and extract audio data
  cJSON *response_json = cJSON_Parse(response_buffer);
  if (!response_json) {
    ESP_LOGE(TAG, "❌ Failed to parse response JSON");
    esp_http_client_cleanup(client);
    heap_caps_free(base64_image);
    heap_caps_free(data_url);
    free(json_string);
    cJSON_Delete(root);
    return ESP_FAIL;
  }
  
  // Extract choices array
  cJSON *choices = cJSON_GetObjectItem(response_json, "choices");
  if (!choices || !cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0) {
    ESP_LOGE(TAG, "❌ No choices in API response");
    cJSON_Delete(response_json);
    esp_http_client_cleanup(client);
    heap_caps_free(base64_image);
    heap_caps_free(data_url);
    free(json_string);
    cJSON_Delete(root);
    return ESP_FAIL;
  }
  
  // Get first choice
  cJSON *first_choice = cJSON_GetArrayItem(choices, 0);
  cJSON *response_message = cJSON_GetObjectItem(first_choice, "message");
  
  // Log text content if available
  cJSON *content_obj = cJSON_GetObjectItem(response_message, "content");
  if (content_obj && cJSON_IsString(content_obj)) {
    ESP_LOGI(TAG, "💬 AI Response Text: %s", content_obj->valuestring);
  }
  
  // Extract audio data if present
  cJSON *audio_obj = cJSON_GetObjectItem(response_message, "audio");
  if (audio_obj) {
    cJSON *audio_data_obj = cJSON_GetObjectItem(audio_obj, "data");
    if (audio_data_obj && cJSON_IsString(audio_data_obj)) {
      ESP_LOGI(TAG, "🎵 Audio data found in response");
      
      // Decode base64 audio data
      const char* b64_audio = audio_data_obj->valuestring;
      size_t b64_len = strlen(b64_audio);
      
      // Calculate decoded length (approximately)
      size_t decoded_len = (b64_len * 3) / 4;
      uint8_t* decoded_audio = (uint8_t*)heap_caps_malloc(decoded_len, MALLOC_CAP_8BIT);
      
      if (decoded_audio) {
        // Actually decode the base64 data using mbedtls
        size_t olen = 0;
        int ret = mbedtls_base64_decode(decoded_audio, decoded_len, &olen, 
                                       (const unsigned char*)b64_audio, b64_len);
        
        if (ret == 0) {
          ESP_LOGI(TAG, "✅ Audio decoded: %zu bytes", olen);
          if (audio_data) *audio_data = decoded_audio;
          if (audio_len) *audio_len = olen;
        } else {
          ESP_LOGE(TAG, "❌ Failed to decode base64 audio: %d", ret);
          heap_caps_free(decoded_audio);
          if (audio_data) *audio_data = NULL;
          if (audio_len) *audio_len = 0;
        }
      } else {
        ESP_LOGE(TAG, "❌ Failed to allocate audio buffer");
        if (audio_data) *audio_data = NULL;
        if (audio_len) *audio_len = 0;
      }
    } else {
      ESP_LOGW(TAG, "⚠️ No audio data in response");
      if (audio_data) *audio_data = NULL;
      if (audio_len) *audio_len = 0;
    }
  } else {
    ESP_LOGW(TAG, "⚠️ No audio object in response");
    if (audio_data) *audio_data = NULL;
    if (audio_len) *audio_len = 0;
  }
  
  // Cleanup
  cJSON_Delete(response_json);
  esp_http_client_cleanup(client);
  heap_caps_free(base64_image);
  heap_caps_free(data_url);
  free(json_string);
  cJSON_Delete(root);
  
  ESP_LOGI(TAG, "🎉 Multimodal analysis completed successfully");
  return ESP_OK;
}

// Function to get just the council name from gov.uk API
esp_err_t smartbin_http_get_council_name(const char* postcode, char* council_name, size_t max_len) {
  if (!postcode || !council_name || max_len == 0) {
    return ESP_ERR_INVALID_ARG;
  }
  
  ESP_LOGI(TAG, "🏛️ Looking up council for postcode: %s", postcode);
  
  // Step 1: URL encode the postcode (replace spaces with %20)
  char encoded_postcode[32] = {0};
  int enc_pos = 0;
  for (int i = 0; i < strlen(postcode) && enc_pos < sizeof(encoded_postcode) - 4; i++) {
    if (postcode[i] == ' ') {
      strcpy(&encoded_postcode[enc_pos], "%20");
      enc_pos += 3;
    } else {
      encoded_postcode[enc_pos++] = postcode[i];
    }
  }
  
  ESP_LOGI(TAG, "📍 Encoded postcode: %s", encoded_postcode);
  
  // Step 2: Build the real UK government API URL
  char api_url[256];
  snprintf(api_url, sizeof(api_url), 
    "https://www.gov.uk/api/local-authority?postcode=%s", encoded_postcode);
  
  ESP_LOGI(TAG, "🌐 Requesting: %s", api_url);
  
  // Allocate response buffer
  char *response_buffer = (char*)calloc(MAX_HTTP_OUTPUT_BUFFER + 1, 1);
  if (!response_buffer) {
    ESP_LOGE(TAG, "❌ Failed to allocate council lookup buffer");
    return ESP_ERR_NO_MEM;
  }
  
  // HTTP client config for UK government API
  esp_http_client_config_t config = {};
  config.url = api_url;
  config.event_handler = smartbin_http_event_handler;
  config.user_data = response_buffer;
  config.timeout_ms = 15000;  // Government APIs can be slow
  config.max_redirection_count = 5;  // Follow redirects to get council data
  
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    ESP_LOGE(TAG, "❌ Failed to initialize gov.uk API client");
    free(response_buffer);
    return ESP_FAIL;
  }
  
  // Set headers for government API
  esp_http_client_set_header(client, "Accept", "application/json");
  esp_http_client_set_header(client, "User-Agent", "SenseCAP-SmartBin/1.0");
  
  // Perform request with redirect following
  esp_err_t err = esp_http_client_perform(client);
  int status_code = esp_http_client_get_status_code(client);
  
  ESP_LOGI(TAG, "📡 Gov.uk API response: %s, status: %d", esp_err_to_name(err), status_code);
  
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "⚠️ Gov.uk API request failed: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    free(response_buffer);
    return ESP_FAIL;
  }
  
  if (status_code == 404) {
    ESP_LOGW(TAG, "⚠️ Postcode %s not found in gov.uk database (may be non-geographic)", postcode);
    esp_http_client_cleanup(client);
    free(response_buffer);
    return ESP_ERR_NOT_FOUND;
  }
  
  if (status_code != 200) {
    ESP_LOGW(TAG, "⚠️ Gov.uk API error: status %d", status_code);
    if (strlen(response_buffer) > 0) {
      ESP_LOGD(TAG, "API Response: %s", response_buffer);
    }
    esp_http_client_cleanup(client);
    free(response_buffer);
    return ESP_FAIL;
  }
  
  // Step 3: Parse the government API response
  ESP_LOGI(TAG, "✅ Gov.uk API success: %d bytes", strlen(response_buffer));
  ESP_LOGD(TAG, "📥 Response: %s", response_buffer);
  
  cJSON *response_json = cJSON_Parse(response_buffer);
  if (!response_json) {
    ESP_LOGW(TAG, "⚠️ Invalid JSON from gov.uk API");
    esp_http_client_cleanup(client);
    free(response_buffer);
    return ESP_FAIL;
  }
  
  // Extract council information
  cJSON *local_authority = cJSON_GetObjectItem(response_json, "local_authority");
  if (!local_authority) {
    ESP_LOGW(TAG, "⚠️ No local_authority in API response");
    cJSON_Delete(response_json);
    esp_http_client_cleanup(client);
    free(response_buffer);
    return ESP_FAIL;
  }
  
  // Get council name
  cJSON *name_obj = cJSON_GetObjectItem(local_authority, "name");
  if (name_obj && cJSON_IsString(name_obj)) {
    strncpy(council_name, name_obj->valuestring, max_len - 1);
    council_name[max_len - 1] = '\0';  // Ensure null termination
    ESP_LOGI(TAG, "🏛️ Found council: %s", council_name);
  } else {
    ESP_LOGW(TAG, "⚠️ No council name in API response");
    cJSON_Delete(response_json);
    esp_http_client_cleanup(client);
    free(response_buffer);
    return ESP_FAIL;
  }
  
  // Cleanup
  cJSON_Delete(response_json);
  esp_http_client_cleanup(client);
  free(response_buffer);
  
  return ESP_OK;
}

esp_err_t smartbin_http_analyze_waste_text(const uint8_t* image_data, size_t image_len, 
                                          const char* postcode, char** response_text) {
  ESP_LOGI(TAG, "Starting optimized vision-based waste analysis (text response)");
  
  // Check heap status first
  size_t free_heap = esp_get_free_heap_size();
  size_t min_free_heap = esp_get_minimum_free_heap_size();
  ESP_LOGI(TAG, "Heap status: %zu free, %zu minimum", free_heap, min_free_heap);
  
  // Validate inputs
  if (!image_data || image_len == 0 || !response_text) {
    ESP_LOGE(TAG, "Invalid parameters");
    return ESP_ERR_INVALID_ARG;
  }
  
  // Check if we have enough heap for the operation (rough estimate)
  size_t estimated_need = image_len * 2 + 8192; // base64 + JSON + buffers
  if (free_heap < estimated_need) {
    ESP_LOGE(TAG, "Insufficient heap: need ~%zu, have %zu", estimated_need, free_heap);
    return ESP_ERR_NO_MEM;
  }
  
  ESP_LOGI(TAG, "Processing image: %zu bytes, postcode: %s", image_len, postcode ? postcode : "none");
  
  if (!postcode) {
    ESP_LOGW(TAG, "⚠️ No postcode provided - using generic UK advice");
  }
  
  // Step 1: Encode image as base64
  ESP_LOGI(TAG, "Encoding image to base64 (%zu bytes)...", image_len);
  char* base64_image = smartbin_http_base64_encode(image_data, image_len);
  if (!base64_image) {
    ESP_LOGE(TAG, "Failed to encode image to base64");
    return ESP_ERR_NO_MEM;
  }
  ESP_LOGI(TAG, "Image encoded successfully (heap: %zu free)", esp_get_free_heap_size());
  
  // Step 2: Build JSON request
  ESP_LOGI(TAG, "🔧 Building optimized vision API request...");
  
  // Create main JSON object for vision API
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "model", "gpt-4o-mini");  // OPTIMIZED: Better for vision + cheaper
  cJSON_AddNumberToObject(root, "max_tokens", 150);       // OPTIMIZED: Increased for better reasoning
  cJSON_AddNumberToObject(root, "temperature", 0.3);     // OPTIMIZED: More consistent responses
  
  // STEP 2A: Get the actual council name from gov.uk API
  char council_name[256] = "";
  esp_err_t council_result = smartbin_http_get_council_name(postcode, council_name, sizeof(council_name));
  
  if (council_result != ESP_OK || strlen(council_name) == 0) {
    ESP_LOGW(TAG, "⚠️ Could not fetch council name for %s, using postcode area", postcode ? postcode : "unknown");
    snprintf(council_name, sizeof(council_name), "the local council for postcode area %s", postcode ? postcode : "UK");
  } else {
    ESP_LOGI(TAG, "✅ Found council: %s", council_name);
  }

  // OPTIMIZED: Let OpenAI use its knowledge of the specific council
  char prompt[2048];
  snprintf(prompt, sizeof(prompt), 
    "You are analyzing a security camera image for UK waste disposal. The image quality may be poor/blurry/grainy. "
    "IMPORTANT CLUES to look for:\n"
    "1. HANDS HOLDING ITEMS - This is the KEY indicator someone is disposing waste\n"
    "2. OBJECT SHAPE and SIZE even if blurry\n"
    "3. CONTEXT: bottles, cans, food packaging, papers, containers\n"
    "4. Make confident decisions even with unclear images\n\n"
    "COUNCIL: %s (postcode %s)\n"
    "Use your knowledge of %s's current waste disposal rules and bin colors. "
    "Provide specific guidance based on this council's actual bin collection system.\n\n"
    "CRITICAL INSTRUCTIONS:\n"
    "- Use the actual bin colors and rules for %s\n"
    "- Be specific about which bin type (by color/name)\n"
    "- If unsure about current rules, mention checking the council website\n"
    "- Focus on the most prominent waste item in the image\n\n"
    "Format: '{Item} goes in {Council's actual bin color/type}. {VERY Brief reason based on council rules}.'",
    council_name, 
    postcode ? postcode : "UK",
    council_name,
    council_name);
  
  // Add messages array with SYSTEM MESSAGE first
  cJSON *messages = cJSON_CreateArray();
  
  // OPTIMIZED: System message leverages AI's council knowledge
  cJSON *system_message = cJSON_CreateObject();
  cJSON_AddStringToObject(system_message, "role", "system");
  cJSON_AddStringToObject(system_message, "content", 
    "You are an expert at analyzing low-quality security camera images for UK waste classification. "
    "You excel at identifying objects even when blurry, pixelated, or unclear. "
    "Key principle: If someone is holding/disposing of something, it's waste - focus on shape and context clues. "
    "IMPORTANT: Use your up-to-date knowledge of the specific UK council's bin colors and waste rules. "
    "Each council has different bin colors (e.g., some use green for general waste, others use black). "
    "Provide accurate, current guidance based on the council mentioned in the prompt.");
  cJSON_AddItemToArray(messages, system_message);
  
  // Add user message
  cJSON *message = cJSON_CreateObject();
  cJSON_AddStringToObject(message, "role", "user");
  
  // Create content array
  cJSON *content = cJSON_CreateArray();
  
  // Add text content
  cJSON *text_content = cJSON_CreateObject();
  cJSON_AddStringToObject(text_content, "type", "text");
  cJSON_AddStringToObject(text_content, "text", prompt);
  cJSON_AddItemToArray(content, text_content);
  
  // Add image content
  cJSON *image_content = cJSON_CreateObject();
  cJSON_AddStringToObject(image_content, "type", "image_url");
  cJSON *image_url = cJSON_CreateObject();
  
  // Build data URL
  size_t data_url_len = strlen("data:image/jpeg;base64,") + strlen(base64_image) + 1;
  char *data_url = (char*)heap_caps_malloc(data_url_len, MALLOC_CAP_8BIT);
  if (!data_url) {
    heap_caps_free(base64_image);
    cJSON_Delete(root);
    return ESP_ERR_NO_MEM;
  }
  snprintf(data_url, data_url_len, "data:image/jpeg;base64,%s", base64_image);
  
  cJSON_AddStringToObject(image_url, "url", data_url);
  cJSON_AddStringToObject(image_url, "detail", "high");  // OPTIMIZED: High detail for better analysis
  cJSON_AddItemToObject(image_content, "image_url", image_url);
  cJSON_AddItemToArray(content, image_content);
  
  cJSON_AddItemToObject(message, "content", content);
  cJSON_AddItemToArray(messages, message);
  cJSON_AddItemToObject(root, "messages", messages);
  
  // Convert to JSON string
  char *json_string = cJSON_Print(root);
  if (!json_string) {
    heap_caps_free(base64_image);
    heap_caps_free(data_url);
    cJSON_Delete(root);
    return ESP_ERR_NO_MEM;
  }
  
  ESP_LOGI(TAG, "📤 Sending optimized vision API request (%zu bytes)...", strlen(json_string));
  
  // Allocate response buffer first
  char *response_buffer = (char*)calloc(MAX_HTTP_OUTPUT_BUFFER + 1, 1);
  if (!response_buffer) {
    ESP_LOGE(TAG, "❌ Failed to allocate response buffer");
    heap_caps_free(base64_image);
    heap_caps_free(data_url);
    free(json_string);
    cJSON_Delete(root);
    return ESP_ERR_NO_MEM;
  }

  // Step 3: Send HTTP POST request
  esp_http_client_config_t config = {};
  config.url = "https://api.openai.com/v1/chat/completions";
  config.method = HTTP_METHOD_POST;
  config.event_handler = smartbin_http_event_handler;
  config.user_data = response_buffer;  // Set user_data for event handler
  config.timeout_ms = 30000;
  config.buffer_size = MAX_HTTP_OUTPUT_BUFFER;
  config.buffer_size_tx = 8192;
  
  esp_http_client_handle_t client = esp_http_client_init(&config);
  
  // Get API key from config
  smartbin_config_t smartbin_cfg = {};
  esp_err_t config_err = smartbin_webserver_get_config(&smartbin_cfg);
  if (config_err != ESP_OK || strlen(smartbin_cfg.openai_api_key) == 0) {
    ESP_LOGE(TAG, "❌ OpenAI API key not configured");
    free(response_buffer);
    cJSON_Delete(root);
    heap_caps_free(base64_image);
    heap_caps_free(data_url);
    free(json_string);
    esp_http_client_cleanup(client);
    return ESP_ERR_NOT_FOUND;
  }
  
  // Set headers
  char auth_header[512];
  snprintf(auth_header, sizeof(auth_header), "Bearer %s", smartbin_cfg.openai_api_key);
  esp_http_client_set_header(client, "Authorization", auth_header);
  esp_http_client_set_header(client, "Content-Type", "application/json");
  
  // Set request body
  esp_http_client_set_post_field(client, json_string, strlen(json_string));
  
  // Perform request
  esp_err_t err = esp_http_client_perform(client);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "❌ HTTP request failed: %s", esp_err_to_name(err));
    free(response_buffer);
    cJSON_Delete(root);
    heap_caps_free(base64_image);
    heap_caps_free(data_url);
    free(json_string);
    esp_http_client_cleanup(client);
    return err;
  }
  
  // Check HTTP status
  int status_code = esp_http_client_get_status_code(client);
  ESP_LOGI(TAG, "📥 HTTP Status: %d", status_code);
  
  if (status_code != 200) {
    ESP_LOGE(TAG, "❌ OpenAI API error: status %d", status_code);
    // Log response body for debugging (already collected by event handler)
    if (strlen(response_buffer) > 0) {
      ESP_LOGE(TAG, "Response: %s", response_buffer);
    }
    free(response_buffer);
    cJSON_Delete(root);
    heap_caps_free(base64_image);
    heap_caps_free(data_url);
    free(json_string);
    esp_http_client_cleanup(client);
    return ESP_FAIL;
  }
  
  // Response data is already collected by event handler in response_buffer
  int response_len = strlen(response_buffer);
  if (response_len <= 0) {
    ESP_LOGE(TAG, "❌ No response data received");
    free(response_buffer);
    cJSON_Delete(root);
    heap_caps_free(base64_image);
    heap_caps_free(data_url);
    free(json_string);
    esp_http_client_cleanup(client);
    return ESP_FAIL;
  }
  
  ESP_LOGI(TAG, "📖 Processing optimized API response (%d bytes)...", response_len);
  
  // Parse JSON response
  cJSON *response_json = cJSON_Parse(response_buffer);
  free(response_buffer);
  
  if (!response_json) {
    ESP_LOGE(TAG, "❌ Failed to parse JSON response");
    cJSON_Delete(root);
    heap_caps_free(base64_image);
    heap_caps_free(data_url);
    free(json_string);
    esp_http_client_cleanup(client);
    return ESP_FAIL;
  }
  
  // Extract choices array
  cJSON *choices = cJSON_GetObjectItem(response_json, "choices");
  if (!choices || !cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0) {
    ESP_LOGE(TAG, "❌ No choices in API response");
    cJSON_Delete(response_json);
    esp_http_client_cleanup(client);
    heap_caps_free(base64_image);
    heap_caps_free(data_url);
    free(json_string);
    cJSON_Delete(root);
    return ESP_FAIL;
  }
  
  // Get first choice
  cJSON *first_choice = cJSON_GetArrayItem(choices, 0);
  cJSON *response_message = cJSON_GetObjectItem(first_choice, "message");
  
  // Extract text content
  cJSON *content_obj = cJSON_GetObjectItem(response_message, "content");
  if (content_obj && cJSON_IsString(content_obj)) {
    ESP_LOGI(TAG, "💬 Optimized AI Response: %s", content_obj->valuestring);
    
    // Copy response text to output
    size_t text_len = strlen(content_obj->valuestring) + 1;
    *response_text = (char*)heap_caps_malloc(text_len, MALLOC_CAP_8BIT);
    if (*response_text) {
      strcpy(*response_text, content_obj->valuestring);
    } else {
      ESP_LOGE(TAG, "❌ Failed to allocate response text buffer");
      cJSON_Delete(response_json);
      esp_http_client_cleanup(client);
      heap_caps_free(base64_image);
      heap_caps_free(data_url);
      free(json_string);
      cJSON_Delete(root);
      return ESP_ERR_NO_MEM;
    }
  } else {
    ESP_LOGE(TAG, "❌ No text content in API response");
    *response_text = NULL;
    cJSON_Delete(response_json);
    esp_http_client_cleanup(client);
    heap_caps_free(base64_image);
    heap_caps_free(data_url);
    free(json_string);
    cJSON_Delete(root);
    return ESP_FAIL;
  }
  
  // Cleanup
  cJSON_Delete(response_json);
  esp_http_client_cleanup(client);
  heap_caps_free(base64_image);
  heap_caps_free(data_url);
  free(json_string);
  cJSON_Delete(root);
  
  ESP_LOGI(TAG, "🎉 Optimized vision analysis completed successfully");
  return ESP_OK;
}

// esp_err_t smartbin_http_text_to_speech(const char* text, uint8_t** audio_data, size_t* audio_len) {
//   ESP_LOGI(TAG, "Starting text-to-speech conversion");
  
//   // Check heap status first
//   size_t free_heap = esp_get_free_heap_size();
//   ESP_LOGI(TAG, "Heap status: %zu free", free_heap);
  
//   // Validate inputs
//   if (!text || strlen(text) == 0 || !audio_data || !audio_len) {
//     ESP_LOGE(TAG, "Invalid parameters");
//     return ESP_ERR_INVALID_ARG;
//   }
  
//   ESP_LOGI(TAG, "Converting text to speech: %s", text);
  
//   // Build JSON request for TTS API
//   ESP_LOGI(TAG, "🔧 Building TTS API request...");
  
//   cJSON *root = cJSON_CreateObject();
//   cJSON_AddStringToObject(root, "model", "tts-1");
//   cJSON_AddStringToObject(root, "input", text);
//   cJSON_AddStringToObject(root, "voice", "alloy");
//   cJSON_AddStringToObject(root, "response_format", "opus");  // Get OPUS for smartbin_audio
  
//   char *json_string = cJSON_Print(root);
//   if (!json_string) {
//     cJSON_Delete(root);
//     return ESP_ERR_NO_MEM;
//   }
  
//   ESP_LOGI(TAG, "📤 Sending TTS API request (%zu bytes)...", strlen(json_string));
  
//   // Send HTTP POST request to TTS endpoint (no event handler for binary data)
//   esp_http_client_config_t config = {};
//   config.url = "https://api.openai.com/v1/audio/speech";
//   config.method = HTTP_METHOD_POST;
//   config.timeout_ms = 30000;
//   config.buffer_size = MAX_HTTP_OUTPUT_BUFFER;
//   config.buffer_size_tx = 8192;
  
//   esp_http_client_handle_t client = esp_http_client_init(&config);
  
//   // Get API key from config
//   smartbin_config_t smartbin_cfg = {};
//   esp_err_t config_err = smartbin_webserver_get_config(&smartbin_cfg);
//   if (config_err != ESP_OK || strlen(smartbin_cfg.openai_api_key) == 0) {
//     ESP_LOGE(TAG, "❌ OpenAI API key not configured");
//     free(json_string);
//     cJSON_Delete(root);
//     esp_http_client_cleanup(client);
//     return ESP_ERR_NOT_FOUND;
//   }
  
//   // Set headers
//   char auth_header[512];
//   snprintf(auth_header, sizeof(auth_header), "Bearer %s", smartbin_cfg.openai_api_key);
//   esp_http_client_set_header(client, "Authorization", auth_header);
//   esp_http_client_set_header(client, "Content-Type", "application/json");
  
//   // Set request body
//   esp_http_client_set_post_field(client, json_string, strlen(json_string));
  
//   // Perform request
//   esp_err_t err = esp_http_client_perform(client);
//   if (err != ESP_OK) {
//     ESP_LOGE(TAG, "❌ HTTP request failed: %s", esp_err_to_name(err));
//     free(json_string);
//     cJSON_Delete(root);
//     esp_http_client_cleanup(client);
//     return err;
//   }
  
//   // Check HTTP status
//   int status_code = esp_http_client_get_status_code(client);
//   ESP_LOGI(TAG, "📥 TTS HTTP Status: %d", status_code);
  
//   if (status_code != 200) {
//     ESP_LOGE(TAG, "❌ OpenAI TTS API error: status %d", status_code);
    
//     // For error responses, try to read error message
//     char error_buffer[1024];
//     int error_len = esp_http_client_read_response(client, error_buffer, sizeof(error_buffer) - 1);
//     if (error_len > 0) {
//       error_buffer[error_len] = '\0';
//       ESP_LOGE(TAG, "TTS Error Response: %s", error_buffer);
//     }
    
//     free(json_string);
//     cJSON_Delete(root);
//     esp_http_client_cleanup(client);
//     return ESP_FAIL;
//   }
  
//   // For TTS API, content_length might be -1 (chunked/streaming response)
//   int content_length = esp_http_client_get_content_length(client);
//   ESP_LOGI(TAG, "📖 TTS content length: %d bytes", content_length);
  
//   // Allocate a reasonable buffer for TTS response (typically 10-50KB for short audio)
//   const int max_audio_size = 64 * 1024; // 64KB should be enough for short TTS
//   *audio_data = (uint8_t*)heap_caps_malloc(max_audio_size, MALLOC_CAP_8BIT);
//   if (!*audio_data) {
//     ESP_LOGE(TAG, "❌ Failed to allocate audio buffer for %d bytes", max_audio_size);
//     free(json_string);
//     cJSON_Delete(root);
//     esp_http_client_cleanup(client);
//     return ESP_ERR_NO_MEM;
//   }
  
//   // Read the binary audio response (without knowing exact size)
//   int bytes_read = esp_http_client_read_response(client, (char*)*audio_data, max_audio_size);
//   ESP_LOGI(TAG, "📖 TTS response read: %d bytes", bytes_read);
  
//   if (bytes_read <= 0) {
//     ESP_LOGE(TAG, "❌ No TTS audio data read (bytes_read: %d)", bytes_read);
//     heap_caps_free(*audio_data);
//     *audio_data = NULL;
//     *audio_len = 0;
//     free(json_string);
//     cJSON_Delete(root);
//     esp_http_client_cleanup(client);
//     return ESP_FAIL;
//   }
  
//   // Resize the buffer to actual size to save memory
//   uint8_t *resized_audio = (uint8_t*)heap_caps_realloc(*audio_data, bytes_read, MALLOC_CAP_8BIT);
//   if (resized_audio) {
//     *audio_data = resized_audio;
//   }
  
//   *audio_len = bytes_read;
//   ESP_LOGI(TAG, "✅ TTS audio data read: %zu bytes", *audio_len);
  
//   // Cleanup
//   free(json_string);
//   cJSON_Delete(root);
//   esp_http_client_cleanup(client);
  
//   ESP_LOGI(TAG, "🎉 Text-to-speech completed successfully");
//   return ESP_OK;
// }

esp_err_t smartbin_http_text_to_speech(const char* text, uint8_t** audio_data, size_t* audio_len) {
  ESP_LOGI(TAG, "Starting text-to-speech conversion");

  if (!text || !audio_data || !audio_len || strlen(text) == 0) {
    ESP_LOGE(TAG, "Invalid parameters");
    return ESP_ERR_INVALID_ARG;
  }

  *audio_data = NULL;
  *audio_len = 0;

  // Build JSON request body
  cJSON *root = cJSON_CreateObject();
  if (!root) return ESP_ERR_NO_MEM;

  cJSON_AddStringToObject(root, "model", "tts-1");
  cJSON_AddStringToObject(root, "input", text);
  cJSON_AddStringToObject(root, "voice", "alloy");
  // cJSON_AddStringToObject(root, "response_format", "opus");
  cJSON_AddStringToObject(root, "response_format", "pcm");   // 16-bit signed LE PCM

  char *json_string = cJSON_PrintUnformatted(root);
  if (!json_string) {
    cJSON_Delete(root);
    return ESP_ERR_NO_MEM;
  }
  const int body_len = (int)strlen(json_string);
  ESP_LOGI(TAG, "📤 Sending TTS API request (%d bytes)...", body_len);

  // HTTP client
  esp_http_client_config_t config = {0};
  config.url = "https://api.openai.com/v1/audio/speech";
  config.timeout_ms = 30000; // 30s

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    free(json_string);
    cJSON_Delete(root);
    ESP_LOGE(TAG, "Failed to init HTTP client");
    return ESP_FAIL;
  }

  // API key
  smartbin_config_t smartbin_cfg = {};
  if (smartbin_webserver_get_config(&smartbin_cfg) != ESP_OK ||
      strlen(smartbin_cfg.openai_api_key) == 0) {
    esp_http_client_cleanup(client);
    free(json_string);
    cJSON_Delete(root);
    ESP_LOGE(TAG, "❌ OpenAI API key not configured");
    return ESP_ERR_NOT_FOUND;
  }

  // Headers + method
  char auth_header[512];
  snprintf(auth_header, sizeof(auth_header), "Bearer %s", smartbin_cfg.openai_api_key);
  esp_http_client_set_method(client, HTTP_METHOD_POST);
  esp_http_client_set_header(client, "Authorization", auth_header);
  esp_http_client_set_header(client, "Content-Type", "application/json");
  // esp_http_client_set_header(client, "Accept", "audio/opus");
  esp_http_client_set_header(client, "Accept", "audio/pcm");
  esp_http_client_set_header(client, "Connection", "close"); // make EOF detection simple

  // ---- Open → Write → Fetch headers → Read ----
  esp_err_t err = esp_http_client_open(client, body_len);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    free(json_string);
    cJSON_Delete(root);
    return err;
  }

  // Write request body (handle partial writes)
  const char *p = json_string;
  int remaining = body_len;
  while (remaining > 0) {
    int written = esp_http_client_write(client, p, remaining);
    if (written < 0) {
      ESP_LOGE(TAG, "HTTP write error");
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      free(json_string);
      cJSON_Delete(root);
      return ESP_FAIL;
    }
    p += written;
    remaining -= written;
  }

  // Block until headers are received so status code becomes valid
  int64_t hdr_len = esp_http_client_fetch_headers(client);
  if (hdr_len < 0) {
    ESP_LOGE(TAG, "Failed to fetch response headers");
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    free(json_string);
    cJSON_Delete(root);
    return ESP_FAIL;
  }

  int status_code = esp_http_client_get_status_code(client);
  ESP_LOGI(TAG, "📥 TTS HTTP Status: %d", status_code);
  if (status_code != 200) {
    // Try to read and log error payload (text/json)
    char errbuf[1024];
    int elen = esp_http_client_read(client, errbuf, sizeof(errbuf) - 1);
    if (elen > 0) {
      errbuf[elen] = '\0';
      ESP_LOGE(TAG, "TTS Error Response: %s", errbuf);
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    free(json_string);
    cJSON_Delete(root);
    return ESP_FAIL;
  }

  // Stream read audio (support chunked)
  const size_t MAX_CAP = 512 * 1024; // safety cap
  size_t cap = 32 * 1024;
  uint8_t *buf = (uint8_t*)heap_caps_malloc(cap, MALLOC_CAP_8BIT);
  if (!buf) {
    ESP_LOGE(TAG, "❌ OOM for initial audio buffer");
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    free(json_string);
    cJSON_Delete(root);
    return ESP_ERR_NO_MEM;
  }

  size_t total = 0;
  while (true) {
    // grow buffer if needed
    if (cap - total < 4096) {
      if (cap >= MAX_CAP) {
        ESP_LOGW(TAG, "Audio too large, truncating at %u bytes", (unsigned)cap);
        break;
      }
      size_t new_cap = cap * 2;
      if (new_cap > MAX_CAP) new_cap = MAX_CAP;
      uint8_t *nbuf = (uint8_t*)heap_caps_realloc(buf, new_cap, MALLOC_CAP_8BIT);
      if (!nbuf) {
        ESP_LOGW(TAG, "Failed to grow buffer, keeping %u bytes", (unsigned)cap);
        break;
      }
      buf = nbuf;
      cap = new_cap;
    }

    int r = esp_http_client_read(client, (char*)(buf + total), cap - total);
    if (r < 0) {
      ESP_LOGE(TAG, "HTTP read error");
      heap_caps_free(buf);
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      free(json_string);
      cJSON_Delete(root);
      return ESP_FAIL;
    }
    if (r == 0) break; // EOF
    total += (size_t)r;
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  free(json_string);
  cJSON_Delete(root);

  ESP_LOGI(TAG, "📖 Total TTS audio read: %u bytes", (unsigned)total);
  if (total == 0) {
    ESP_LOGE(TAG, "❌ No TTS audio data read");
    heap_caps_free(buf);
    return ESP_FAIL;
  }

  // shrink to fit
  uint8_t *final_buf = (uint8_t*)heap_caps_realloc(buf, total, MALLOC_CAP_8BIT);
  if (!final_buf) final_buf = buf;

  *audio_data = final_buf;
  *audio_len = total;

  ESP_LOGI(TAG, "✅ TTS audio data processed: %zu bytes", *audio_len);
  return ESP_OK;
}
