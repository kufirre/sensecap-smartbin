#include "smartbin_webrtc.h"
#include "smartbin_audio.h"
#include "smartbin_http.h"

#ifndef LINUX_BUILD
#include <driver/i2s_std.h>
#include <opus.h>
#endif

#include <esp_event.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <string.h>
#include <peer.h>

#define TICK_INTERVAL 15

static const char *TAG = "SMARTBIN_WEBRTC";

static PeerConnection *peer_connection = NULL;

#ifndef LINUX_BUILD
static StaticTask_t task_buffer;

static void audio_send_task(void *user_data) {
  ESP_LOGI(TAG, "🎙️ Audio task started - initializing encoder");
  smartbin_audio_init_encoder();
  
  ESP_LOGI(TAG, "🎙️ Starting smart VAD audio streaming (15ms intervals)");
  ESP_LOGI(TAG, "🧠 Only sends audio when voice detected - saves API costs");
  while (1) {
    smartbin_audio_send(peer_connection);
    vTaskDelay(pdMS_TO_TICKS(TICK_INTERVAL));
  }
}

static void on_request_offer(char *offer) {
  char answer[2048] = {0};
  smartbin_http_send_webrtc_offer(offer, answer);
  
  if (strlen(answer) > 0) {
    ESP_LOGI(TAG, "✅ Setting remote description with OpenAI answer");
    peer_connection_set_remote_description(peer_connection, answer);
  } else {
    ESP_LOGE(TAG, "❌ Empty answer received from OpenAI");
  }
}

static void on_ice_candidate(char *description) {
  ESP_LOGI(TAG, "ICE candidate generated, sending to OpenAI");
  // Note: Current OpenAI Realtime API doesn't use ICE candidates in the traditional sense
  // The connection is established via SDP offer/answer exchange
}

static void on_connection_state_change(PeerConnectionState state) {
  static int connection_count = 0;
  static uint64_t connection_start_time = 0;
  
  if (state == PEER_CONNECTION_COMPLETED) {
    connection_count++;
    connection_start_time = esp_timer_get_time() / 1000000; // Convert to seconds
    ESP_LOGI(TAG, "✅ OpenAI Realtime session #%d established - starting audio streaming", connection_count);
    
    // Start the audio sending task
    TaskHandle_t task_handle = xTaskCreateStatic(
        audio_send_task,
        "audio_send",
        4096,
        NULL,
        5,
        (StackType_t *)malloc(4096 * sizeof(StackType_t)),
        &task_buffer
    );
    
    if (task_handle == NULL) {
      ESP_LOGE(TAG, "❌ Failed to create audio send task");
    }
  } else if (state == PEER_CONNECTION_DISCONNECTED || state == PEER_CONNECTION_FAILED) {
    if (connection_start_time > 0) {
      uint64_t current_time = esp_timer_get_time() / 1000000;
      uint64_t session_duration = current_time - connection_start_time;
      ESP_LOGW(TAG, "💔 OpenAI session ended after %llu seconds", session_duration);
      connection_start_time = 0;
    }
  } else if (state == PEER_CONNECTION_CONNECTED) {
    ESP_LOGI(TAG, "🔄 Connecting to OpenAI Realtime API...");
  }
}

static void on_message(char *message) {
  // Handle text messages if needed
  ESP_LOGD(TAG, "📨 Message received: %s", message);
}

static void on_data_channel_open() {
  ESP_LOGI(TAG, "🎯 OpenAI connection fully established and ready");
}

static void on_data_channel_message(char *message) {
  // Handle data channel messages
  ESP_LOGD(TAG, "📊 Data channel message: %s", message);
}

#endif // LINUX_BUILD

#ifndef LINUX_BUILD
static void on_audio_track_callback(uint8_t *data, size_t size, void *userdata) {
  // Decode received audio from OpenAI and play through speaker
  smartbin_audio_decode_and_play(data, size);
}

static void on_ice_connection_state_change_callback(PeerConnectionState state, void *user_data) {
  on_connection_state_change(state);
}

static void on_ice_candidate_callback(char *description, void *user_data) {
  on_ice_candidate(description);
}
#endif

void smartbin_webrtc_start(void) {
#ifdef LINUX_BUILD
  ESP_LOGW(TAG, "WebRTC not supported on Linux build");
  return;
#endif

  ESP_LOGI(TAG, "Initializing WebRTC for OpenAI Realtime API");
  
  // Initialize audio systems
  smartbin_audio_init_capture();
  smartbin_audio_init_decoder();
  
  // Use the simpler API structure that matches the current peer library
  PeerConfiguration config = {
    .ice_servers = {},
    .audio_codec = CODEC_OPUS,
    .video_codec = CODEC_NONE,
    .datachannel = DATA_CHANNEL_NONE,
    .onaudiotrack = on_audio_track_callback,
    .onvideotrack = NULL,
    .on_request_keyframe = NULL,
    .user_data = NULL,
  };

  peer_connection = peer_connection_create(&config);
  if (!peer_connection) {
    ESP_LOGE(TAG, "❌ Failed to create peer connection");
    return;
  }

  ESP_LOGI(TAG, "📞 WebRTC peer connection created successfully");
  
  // Use the callback-based API for connection state changes
  peer_connection_oniceconnectionstatechange(peer_connection, on_ice_connection_state_change_callback);
  
  peer_connection_onicecandidate(peer_connection, on_ice_candidate_callback);
  
  ESP_LOGI(TAG, "📞 Creating offer for OpenAI Realtime API...");
  peer_connection_create_offer(peer_connection);
}

void smartbin_webrtc_restart_session(void) {
  ESP_LOGW(TAG, "🔄 Restarting OpenAI session due to timeout");
  
  if (peer_connection) {
    peer_connection_destroy(peer_connection);
    peer_connection = NULL;
  }
  
  // Small delay before reconnecting
  vTaskDelay(pdMS_TO_TICKS(2000));
  
  // Start new session
  smartbin_webrtc_start();
}