#include <driver/i2s.h>
#include <opus.h>
#include <esp_log.h>
#include <esp_codec_dev.h>
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
  
  int decoded_size = opus_decode(opus_decoder, data, size, output_buffer, BUFFER_SAMPLES_CNT, 0);
  
  if (size > 26) {
    ESP_LOGD(TAG, "Decoded audio: input=%d bytes, output=%d samples", size, decoded_size);
    ui_switch_speaking();
  }
  
  if (decoded_size > 0) {
    esp_codec_dev_write(play_dev_handle, output_buffer, BUFFER_SAMPLES_CNT * sizeof(opus_int16));
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

  auto encoded_size = opus_encode(opus_encoder, encoder_input_buffer, BUFFER_SAMPLES_CNT,
                                encoder_output_buffer, OPUS_OUT_BUFFER_SIZE);

  if (encoded_size > 0) {
    peer_connection_send_audio(peer_connection, encoder_output_buffer, encoded_size);
    ESP_LOGV(TAG, "Sent %ld bytes of encoded audio", encoded_size);
  } else {
    ESP_LOGW(TAG, "OPUS encoding failed: %ld", encoded_size);
  }
}