#include "smartbin_audio.h"

#include <driver/i2s_std.h>
#include <opus.h>
#include <esp_log.h>
#include <esp_codec_dev.h>
#include <esp_timer.h>
#include <peer.h>
#include <string.h>
#include <sensecap-watcher.h>

// Audio configuration constants
#define OPUS_OUT_BUFFER_SIZE 1276  // 1276 bytes is recommended by opus_encode
#define SAMPLE_RATE  16000
#define CHANNELS     1

#define BUFFER_SAMPLES (640)
#define BUFFER_SAMPLES_CNT (BUFFER_SAMPLES/2)

#define OPUS_ENCODER_BITRATE 24000        // Optimized for voice (24 kbps vs 30 kbps)
#define OPUS_ENCODER_COMPLEXITY 5         // Higher complexity for better quality (0-10 scale)

// Voice Activity Detection (VAD) Configuration
#define VAD_THRESHOLD_VOICE 300           // Volume threshold to detect voice
#define SESSION_TIMEOUT_MS 30000          // Restart session if no response for 30 seconds

static const char *TAG = "SMARTBIN_AUDIO";

// BSP codec handles for SenseCAP hardware
static esp_codec_dev_handle_t play_dev_handle;
static esp_codec_dev_handle_t record_dev_handle;

// OPUS decoder variables
static opus_int16 *output_buffer = NULL;
static OpusDecoder *opus_decoder = NULL;

// OPUS encoder variables  
static OpusEncoder *opus_encoder = NULL;
static opus_int16 *encoder_input_buffer = NULL;
static uint8_t *encoder_output_buffer = NULL;

// Session health tracking
static volatile uint64_t g_last_response_time = 0;
static volatile uint64_t g_last_user_transmission_time = 0;
static volatile bool g_waiting_for_response = false;
static volatile bool g_openai_currently_speaking = false;
static volatile uint64_t g_last_openai_audio_time = 0;

// void smartbin_audio_init_capture(void) {
//   // Initialize SenseCAP codec with optimized settings
//   bsp_codec_mute_set(true);
//   bsp_codec_mute_set(false);
  
//   // Set optimal volume levels for better audio quality
//   bsp_codec_volume_set(85, NULL);  // Slightly lower speaker volume to prevent feedback
  
//   play_dev_handle = bsp_codec_speaker_get();
//   record_dev_handle = bsp_codec_microphone_get();
  
//   ESP_LOGI(TAG, "Audio capture initialized with optimized settings");
// }

void smartbin_audio_set_out_sr(uint32_t sr_hz) {
  if (sr_hz == 0) return;

  // Ensure speaker handle exists
  if (play_dev_handle == NULL) {
    play_dev_handle = bsp_codec_speaker_get();
  }

  // Avoid pop while reconfiguring clocks
  bsp_codec_mute_set(true);

  // Configure I2S/codec clocks via SenseCAP Watcher BSP
  // 16-bit mono matches your PCM path
  esp_err_t err = bsp_codec_set_fs(sr_hz, 16, I2S_SLOT_MODE_MONO);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "bsp_codec_set_fs(%lu Hz, 16-bit, MONO) failed: %s",
             (unsigned long)sr_hz, esp_err_to_name(err));
  }

  bsp_codec_mute_set(false);
}

void smartbin_audio_init_capture(void) {
  bsp_codec_mute_set(true);
  bsp_codec_mute_set(false);
  bsp_codec_volume_set(85, NULL);

  play_dev_handle   = bsp_codec_speaker_get();
  record_dev_handle = bsp_codec_microphone_get();

  // Default to 24 kHz to match TTS PCM
  smartbin_audio_set_out_sr(24000);

  ESP_LOGI(TAG, "Audio capture initialized with optimized settings");
}


void smartbin_audio_init_decoder(void) {
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

void smartbin_audio_init_encoder(void) {
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

  // Configure OPUS encoder for optimal voice communication
  opus_encoder_ctl(opus_encoder, OPUS_SET_BITRATE(OPUS_ENCODER_BITRATE));
  opus_encoder_ctl(opus_encoder, OPUS_SET_COMPLEXITY(OPUS_ENCODER_COMPLEXITY));
  opus_encoder_ctl(opus_encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
  
  // Additional voice optimizations
  opus_encoder_ctl(opus_encoder, OPUS_SET_VBR(1));                    // Variable bitrate for better quality
  opus_encoder_ctl(opus_encoder, OPUS_SET_VBR_CONSTRAINT(1));         // Constrained VBR
  opus_encoder_ctl(opus_encoder, OPUS_SET_DTX(1));                    // Discontinuous transmission (silence detection)
  opus_encoder_ctl(opus_encoder, OPUS_SET_BANDWIDTH(OPUS_BANDWIDTH_WIDEBAND)); // 8kHz bandwidth for voice
  opus_encoder_ctl(opus_encoder, OPUS_SET_PREDICTION_DISABLED(0));    // Enable prediction for better compression
  
  encoder_input_buffer = (opus_int16 *)malloc(BUFFER_SAMPLES * sizeof(opus_int16));
  encoder_output_buffer = (uint8_t *)malloc(OPUS_OUT_BUFFER_SIZE);
  
  if (encoder_input_buffer == NULL || encoder_output_buffer == NULL) {
    ESP_LOGE(TAG, "Failed to allocate encoder buffers");
    return;
  }
  
  ESP_LOGI(TAG, "OPUS encoder initialized successfully");
}

void smartbin_audio_send(PeerConnection *peer_connection) {
  static int call_count = 0;
  static uint64_t last_log_time = 0;
  // Simple VAD with feedback prevention
  static bool is_transmitting = false;
  static int voice_frames = 0;
  static int silence_frames = 0;
  static int send_count = 0;
  static uint64_t total_sent = 0;
  
  if (opus_encoder == NULL || encoder_input_buffer == NULL || encoder_output_buffer == NULL) {
    ESP_LOGW(TAG, "OPUS encoder not initialized");
    return;
  }
  
  if (peer_connection == NULL) {
    ESP_LOGW(TAG, "peer_connection is NULL");
    return;
  }
  
  if (record_dev_handle == NULL) {
    ESP_LOGW(TAG, "record_dev_handle is NULL - audio capture not initialized");
    return;
  }
  
  // Calculate volume level
  int32_t volume_sum = 0;
  for (int i = 0; i < BUFFER_SAMPLES_CNT; i++) {
    volume_sum += abs(encoder_input_buffer[i]);
  }
  int avg_volume = volume_sum / BUFFER_SAMPLES_CNT;
  
  uint64_t current_time = esp_timer_get_time() / 1000; // Convert to milliseconds
  
  // Skip everything while OpenAI is speaking to prevent feedback
  if (g_openai_currently_speaking) {
    if (is_transmitting) {
      is_transmitting = false;
      ESP_LOGI(TAG, "🔇 STOP: Local transmission (OpenAI speaking - feedback prevention)");
    }
    return;
  }

  // Only read mic when we’re actually listening
  esp_codec_dev_read(record_dev_handle, encoder_input_buffer, BUFFER_SAMPLES);
  
  // Basic VAD logic
  bool voice_detected = avg_volume > VAD_THRESHOLD_VOICE;
  
  if (voice_detected) {
    voice_frames++;
    silence_frames = 0;
    
    // Start transmission after consistent voice (3 frames = 45ms)
    if (!is_transmitting && voice_frames >= 3) {
      is_transmitting = true;
      ESP_LOGI(TAG, "🎤 START: User speaking - transmitting to OpenAI (vol:%d)", avg_volume);
    }
  } else {
    silence_frames++;
    voice_frames = 0;
    
    // Stop transmission after silence (60 frames = 900ms)
    if (is_transmitting && silence_frames >= 60) {
      is_transmitting = false;
      g_waiting_for_response = true;  // We're now waiting for OpenAI response
      g_last_user_transmission_time = current_time;
      ESP_LOGI(TAG, "🔇 STOP: User finished speaking - waiting for OpenAI response (sent %d packets)", send_count);
    }
  }
  
  // Always encode and send when transmitting
  if (is_transmitting) {
    // Clear any potential stale data in buffers
    memset(encoder_output_buffer, 0, OPUS_OUT_BUFFER_SIZE);
    
    opus_int32 encoded_size = opus_encode(opus_encoder, encoder_input_buffer, BUFFER_SAMPLES_CNT,
                                  encoder_output_buffer, OPUS_OUT_BUFFER_SIZE);

    if (encoded_size > 0) {
      peer_connection_send_audio(peer_connection, encoder_output_buffer, encoded_size);
      send_count++;
      total_sent += encoded_size;
    } else if (encoded_size < 0) {
      ESP_LOGW(TAG, "OPUS encoding failed: %ld", encoded_size);
    }
  }
  
  // Log periodically to debug
  call_count++;
  uint64_t current_log_time = esp_timer_get_time() / 1000000; // seconds
  if (current_log_time - last_log_time >= 5) { // Log every 5 seconds
    uint64_t time_since_openai = g_last_response_time > 0 ? (current_time - g_last_response_time) : 0;
    ESP_LOGI(TAG, "📊 Audio: vol:%d, transmitting:%s, sent:%d pkts (%llu bytes), waiting_response:%s, last_response:%llums ago", 
             avg_volume, is_transmitting ? "YES" : "NO", send_count, total_sent,
             g_waiting_for_response ? "YES" : "NO", time_since_openai);
    last_log_time = current_log_time;
    call_count = 0;
  }
}

// void smartbin_audio_decode_and_play(uint8_t *data, size_t size) {
//   if (opus_decoder == NULL || output_buffer == NULL) {
//     ESP_LOGW(TAG, "OPUS decoder not initialized");
//     return;
//   }
  
//   // Enhanced debug logging for OpenAI audio responses with speaking state management
//   static int packet_count = 0;
//   static uint64_t total_bytes = 0;
//   static uint64_t last_substantial_audio_time = 0;
//   static bool currently_playing_speech = false;
//   static int consecutive_silence_packets = 0;
  
//   packet_count++;
//   total_bytes += size;
//   uint64_t current_time = esp_timer_get_time() / 1000; // milliseconds
  
//   int decoded_size = opus_decode(opus_decoder, data, size, output_buffer, BUFFER_SAMPLES_CNT, 0);
  
//   // Only treat packets > 100 bytes as substantial speech content
//   // 72-byte packets are typically silence/comfort noise
//   if (size > 100) {
//     // Log substantial speech less frequently to reduce spam
//     if (packet_count % 50 == 0 || !currently_playing_speech) {
//       ESP_LOGI(TAG, "🗣️ OpenAI Speech #%d: %d bytes → %d samples (Total: %llu bytes)", 
//                packet_count, size, decoded_size, total_bytes);
//     }
    
//     // Update global state for VAD coordination
//     g_openai_currently_speaking = true;
//     g_last_openai_audio_time = current_time;
    
//     // Track when we received a response from OpenAI
//     g_last_response_time = current_time;
//     g_waiting_for_response = false;  // We got a response
    
//     // Reset silence counter when we get substantial audio
//     consecutive_silence_packets = 0;
    
//     if (!currently_playing_speech) {
//       currently_playing_speech = true;
//       ESP_LOGI(TAG, "🎤 Started OpenAI speech playback - VAD DISABLED to prevent feedback");
//     }
//     last_substantial_audio_time = current_time;
//   } else {
//     // Track consecutive silence packets to detect true end of speech
//     consecutive_silence_packets++;
    
//     // Log substantial audio periodically to avoid spam
//     if (packet_count % 100 == 0) {
//       ESP_LOGI(TAG, "🔇 Silence packet #%d: %d bytes (Total: %llu bytes, consecutive silence: %d)", 
//                packet_count, size, total_bytes, consecutive_silence_packets);
//     }
//   }
  
//   // Switch back to listening mode based on both time and consecutive silence packets
//   bool enough_time_passed = (current_time - last_substantial_audio_time) > 2000; // 2 seconds for buffering
//   bool enough_silence_packets = consecutive_silence_packets >= 25; // ~25 packets = ~500ms of confirmed silence
  
//   if (currently_playing_speech && enough_time_passed && enough_silence_packets) {
//     uint64_t silence_duration = current_time - last_substantial_audio_time;
//     int silence_count = consecutive_silence_packets;
    
//     currently_playing_speech = false;
//     g_openai_currently_speaking = false;
//     consecutive_silence_packets = 0; // Reset counter
//     ESP_LOGI(TAG, "🎧 Returned to listening mode after %llums + %d silence packets - audio truly finished", 
//              silence_duration, silence_count);
//   }
  
//   if (decoded_size > 0) {
//     esp_codec_dev_write(play_dev_handle, output_buffer, BUFFER_SAMPLES_CNT * sizeof(opus_int16));
//   } else {
//     ESP_LOGW(TAG, "OPUS decode failed: %d", decoded_size);
//   }
// }

void smartbin_audio_decode_and_play(uint8_t *data, size_t size) {
  if (play_dev_handle == NULL) smartbin_audio_init_capture();

  if (!data || size == 0) {
    ESP_LOGW(TAG, "No audio data to play");
    return;
  }

  // If someone accidentally sent Ogg Opus, bail clearly.
  if (size >= 4 && memcmp(data, "OggS", 4) == 0) {
    ESP_LOGW(TAG, "Received Ogg Opus container — set TTS response_format=pcm");
    return;
  }

  g_openai_currently_speaking = true;
  esp_err_t w = esp_codec_dev_write(play_dev_handle, data, size);
  g_openai_currently_speaking = false;

  if (w != ESP_OK) {
    ESP_LOGW(TAG, "PCM write failed: %s", esp_err_to_name(w));
  } else {
    // duration = (bytes / 2) samples / 24000 Hz
    uint32_t ms = (uint32_t)((size / 2) * 1000 / 24000);
    ESP_LOGI(TAG, "🔊 Played PCM: %u bytes (~%u ms at 24 kHz)", (unsigned)size, ms);
  }
}



uint64_t smartbin_audio_get_last_response_time(void) {
    return g_last_response_time;
}

bool smartbin_audio_is_session_healthy(void) {
    uint64_t current_time = esp_timer_get_time() / 1000; // milliseconds
    
    // If we're waiting for a response and it's been too long, session is unhealthy
    if (g_waiting_for_response && g_last_user_transmission_time > 0) {
        uint64_t time_since_last_transmission = current_time - g_last_user_transmission_time;
        if (time_since_last_transmission > SESSION_TIMEOUT_MS) {
            ESP_LOGW(TAG, "🚨 Session unhealthy: No response for %llums", time_since_last_transmission);
            return false;
        }
    }
    
    return true;
}