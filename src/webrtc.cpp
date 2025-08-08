#ifndef LINUX_BUILD
#include <driver/i2s.h>
#include <opus.h>
#endif

#include <esp_event.h>
#include <esp_log.h>
#include <string.h>
#include <peer.h>

#include "main.h"

#define TICK_INTERVAL 15

static const char *TAG = "SMARTBIN_WEBRTC";

PeerConnection *peer_connection = NULL;

#ifndef LINUX_BUILD
StaticTask_t task_buffer;

void oai_send_audio_task(void *user_data) {
  oai_init_audio_encoder();

  while (1) {
    oai_send_audio(peer_connection);
    vTaskDelay(pdMS_TO_TICKS(TICK_INTERVAL));
  }
}
#endif

static void oai_onconnectionstatechange_task(PeerConnectionState state, void *user_data) {
  ESP_LOGI(TAG, "PeerConnectionState: %s", peer_connection_state_to_string(state));

  if (state == PEER_CONNECTION_DISCONNECTED || state == PEER_CONNECTION_CLOSED) {
#ifndef LINUX_BUILD
    ESP_LOGW(TAG, "WebRTC connection lost - restarting system");
    esp_restart();
#endif
  } else if (state == PEER_CONNECTION_CONNECTED) {
    ESP_LOGI(TAG, "WebRTC connection established - starting audio streaming");
#ifndef LINUX_BUILD
    // Create audio streaming task with increased memory for 16K sampling rate
    StackType_t *stack_memory = (StackType_t *)heap_caps_malloc(
        40000 * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
    if (stack_memory == NULL) {
      ESP_LOGE(TAG, "Failed to allocate memory for audio task");
      return;
    }
    xTaskCreateStaticPinnedToCore(oai_send_audio_task, "audio_publisher", 40000,
                                  NULL, 7, stack_memory, &task_buffer, 0);
#endif
  }
}

static void oai_on_icecandidate_task(char *description, void *user_data) {
  ESP_LOGI(TAG, "ICE candidate generated, sending to OpenAI");
  char local_buffer[MAX_HTTP_OUTPUT_BUFFER + 1] = {0};
  oai_http_request(description, local_buffer);
  peer_connection_set_remote_description(peer_connection, local_buffer);
}

extern "C" void oai_webrtc() {
  ESP_LOGI(TAG, "Initializing WebRTC for OpenAI Realtime API");
  
  PeerConfiguration peer_connection_config = {
      .ice_servers = {},
      .audio_codec = CODEC_OPUS,
      .video_codec = CODEC_NONE,
      .datachannel = DATA_CHANNEL_NONE,
      .onaudiotrack = [](uint8_t *data, size_t size, void *userdata) -> void {
#ifndef LINUX_BUILD
        // Decode received audio from OpenAI and play through speaker
        oai_audio_decode(data, size);
#endif
      },
      .onvideotrack = NULL,
      .on_request_keyframe = NULL,
      .user_data = NULL,
  };

  peer_connection = peer_connection_create(&peer_connection_config);
  if (peer_connection == NULL) {
    ESP_LOGE(TAG, "Failed to create WebRTC peer connection");
#ifndef LINUX_BUILD
    esp_restart();
#endif
    return;
  }

  ESP_LOGI(TAG, "WebRTC peer connection created successfully");
  peer_connection_oniceconnectionstatechange(peer_connection, oai_onconnectionstatechange_task);
  peer_connection_onicecandidate(peer_connection, oai_on_icecandidate_task);
  peer_connection_create_offer(peer_connection);
  for (;;) {
    if (peer_connection != NULL) {
      peer_connection_loop(peer_connection);
    }
    vTaskDelay(pdMS_TO_TICKS(TICK_INTERVAL));
  }
}