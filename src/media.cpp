#include <driver/i2s.h>
#include <opus.h>
#include <esp_log.h>
#include <esp_codec_dev.h>
#include <esp_timer.h>
#include <peer.h>
#include <cstring>

#include "main.h"

#define OPUS_OUT_BUFFER_SIZE 1276  // 1276 bytes is recommended by opus_encode
#define SAMPLE_RATE  16000
#define CHANNELS     1

#define BUFFER_SAMPLES (640)
#define BUFFER_SAMPLES_CNT (BUFFER_SAMPLES/2)

#define OPUS_ENCODER_BITRATE 30000
#define OPUS_ENCODER_COMPLEXITY 0

static const char *TAG = "SMARTBIN_MEDIA";

// We'll implement BSP codec abstraction for SenseCap hardware
static esp_codec_dev_handle_t play_dev_handle;
static esp_codec_dev_handle_t record_dev_handle;

// OPUS decoder variables
opus_int16 *output_buffer = NULL;
OpusDecoder *opus_decoder = NULL;

// OPUS encoder variables  
OpusEncoder *opus_encoder = NULL;
opus_int16 *encoder_input_buffer = NULL;
uint8_t *encoder_output_buffer = NULL;

void oai_init_audio_capture() {
  // Initialize SenseCAP codec as in example
  bsp_codec_mute_set(true);
  bsp_codec_mute_set(false);
  bsp_codec_volume_set(100, NULL);
  play_dev_handle = bsp_codec_speaker_get();
  record_dev_handle = bsp_codec_microphone_get();
}

void oai_init_audio_decoder() {
  ESP_LOGI(TAG, "Initializing OPUS audio decoder");
  
  int decoder_error = 0;
  opus_decoder = opus_decoder_create(SAMPLE_RATE, CHANNELS, &decoder_error);
  if (decoder_error != OPUS_OK) {
    ESP_LOGE(TAG, "Failed to create OPUS decoder: %d", decoder_error);
    return;
  }
  
  output_buffer = (opus_int16 *)malloc(BUFFER_SAMPLES_CNT * sizeof(opus_int16));
  if (output_buffer == NULL) {
    ESP_LOGE(TAG, "Failed to allocate decoder output buffer");
    return;
  }
  
  ESP_LOGI(TAG, "OPUS decoder initialized successfully");
}

void oai_audio_decode(uint8_t *data, size_t size) {
  if (opus_decoder == NULL || output_buffer == NULL) {
    ESP_LOGW(TAG, "OPUS decoder not initialized");
    return;
  }
  
  // Enhanced debug logging for OpenAI audio responses
  static int packet_count = 0;
  static uint64_t total_bytes = 0;
  packet_count++;
  total_bytes += size;
  
  int decoded_size = opus_decode(opus_decoder, data, size, output_buffer, BUFFER_SAMPLES_CNT, 0);
  
  if (size > 26) {
    ESP_LOGI(TAG, "🔊 OpenAI Response #%d: %d bytes → %d samples (Total: %llu bytes)", 
             packet_count, size, decoded_size, total_bytes);
    ui_switch_speaking();
  } else {
    ESP_LOGD(TAG, "Small audio packet: %d bytes (likely silence/comfort noise)", size);
  }
  
  if (decoded_size > 0) {
    esp_codec_dev_write(play_dev_handle, output_buffer, BUFFER_SAMPLES_CNT * sizeof(opus_int16));
  } else {
    ESP_LOGW(TAG, "OPUS decode failed: %d", decoded_size);
  }
}

void oai_init_audio_encoder() {
  ESP_LOGI(TAG, "Initializing OPUS audio encoder");
  
  int encoder_error;
  opus_encoder = opus_encoder_create(SAMPLE_RATE, CHANNELS, OPUS_APPLICATION_VOIP, &encoder_error);
  if (encoder_error != OPUS_OK) {
    ESP_LOGE(TAG, "Failed to create OPUS encoder: %d", encoder_error);
    return;
  }

  if (opus_encoder_init(opus_encoder, SAMPLE_RATE, CHANNELS, OPUS_APPLICATION_VOIP) != OPUS_OK) {
    ESP_LOGE(TAG, "Failed to initialize OPUS encoder");
    return;
  }

  // Configure OPUS encoder for voice communication
  opus_encoder_ctl(opus_encoder, OPUS_SET_BITRATE(OPUS_ENCODER_BITRATE));
  opus_encoder_ctl(opus_encoder, OPUS_SET_COMPLEXITY(OPUS_ENCODER_COMPLEXITY));
  opus_encoder_ctl(opus_encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
  
  encoder_input_buffer = (opus_int16 *)malloc(BUFFER_SAMPLES * sizeof(opus_int16));
  encoder_output_buffer = (uint8_t *)malloc(OPUS_OUT_BUFFER_SIZE);
  
  if (encoder_input_buffer == NULL || encoder_output_buffer == NULL) {
    ESP_LOGE(TAG, "Failed to allocate encoder buffers");
    return;
  }
  
  ESP_LOGI(TAG, "OPUS encoder initialized successfully");
}

void oai_send_audio(PeerConnection *peer_connection) {
  if (opus_encoder == NULL || encoder_input_buffer == NULL || encoder_output_buffer == NULL) {
    ESP_LOGW(TAG, "OPUS encoder not initialized");
    return;
  }
  
  if (peer_connection == NULL) {
    return;
  }
  
  esp_codec_dev_read(record_dev_handle, encoder_input_buffer, BUFFER_SAMPLES);

  // Voice Activity Detection (VAD) state tracking
  static bool g_is_sending_audio = false;
  static uint64_t g_last_voice_activity_time = 0;
  static uint64_t g_voice_start_time = 0;
  static int send_count = 0;
  static int silent_count = 0;
  static uint64_t total_sent = 0;
  
  // Calculate volume level for VAD
  int32_t volume_sum = 0;
  for (int i = 0; i < BUFFER_SAMPLES_CNT; i++) {
    volume_sum += abs(encoder_input_buffer[i]);
  }
  int avg_volume = volume_sum / BUFFER_SAMPLES_CNT;
  
  uint64_t current_time = esp_timer_get_time() / 1000; // Convert to milliseconds
  bool voice_detected = avg_volume > VAD_THRESHOLD_VOICE;
  
  // VAD State Machine
  if (voice_detected) {
    g_last_voice_activity_time = current_time;
    if (!g_is_sending_audio) {
      g_voice_start_time = current_time;
    }
    // Only start sending after minimum voice duration to avoid false positives
    if ((current_time - g_voice_start_time) >= VAD_MIN_VOICE_DURATION_MS) {
      if (!g_is_sending_audio) {
        ESP_LOGI(TAG, "🎤 VAD: Voice detected - starting audio transmission (vol:%d)", avg_volume);
        g_is_sending_audio = true;
        ui_listening(); // Show listening animation
      }
    }
    silent_count = 0;
  } else {
    silent_count++;
    // Stop sending if silence timeout reached
    if (g_is_sending_audio && (current_time - g_last_voice_activity_time) >= VAD_SILENCE_TIMEOUT_MS) {
      ESP_LOGI(TAG, "🔇 VAD: Silence timeout - stopping audio transmission (silent for %llu ms)", 
               current_time - g_last_voice_activity_time);
      g_is_sending_audio = false;
    }
  }
  
  // Only encode and send audio if VAD indicates we should be sending
  if (g_is_sending_audio) {
    auto encoded_size = opus_encode(opus_encoder, encoder_input_buffer, BUFFER_SAMPLES_CNT,
                                  encoder_output_buffer, OPUS_OUT_BUFFER_SIZE);

    if (encoded_size > 0) {
      peer_connection_send_audio(peer_connection, encoder_output_buffer, encoded_size);
      send_count++;
      total_sent += encoded_size;
      
      if (voice_detected) {
        ESP_LOGD(TAG, "🎤 Voice #%d: %ld bytes (vol:%d, total:%llu)", 
                 send_count, encoded_size, avg_volume, total_sent);
      } else {
        ESP_LOGD(TAG, "🔇 Silence #%d: %ld bytes (vol:%d, timeout in %llu ms)", 
                 send_count, encoded_size, avg_volume, 
                 VAD_SILENCE_TIMEOUT_MS - (current_time - g_last_voice_activity_time));
      }
    } else {
      ESP_LOGW(TAG, "OPUS encoding failed: %ld", encoded_size);
    }
  } else {
    // Not sending audio - just log occasionally for debug
    if (silent_count % 200 == 0) { // Log every ~3 seconds when not sending
      ESP_LOGD(TAG, "🔇 VAD: Not sending (vol:%d, silent_count:%d)", avg_volume, silent_count);
    }
  }
}