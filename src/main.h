// Replicate example header layout
#pragma once

#include <peer.h>
#include "sensecap-watcher.h"
#include "ui/ui.h"

#define LOG_TAG "realtimeapi-sdk"
#define MAX_HTTP_OUTPUT_BUFFER 2048

extern "C" void oai_wifi(void);
extern "C" void oai_wifi_init(void);
extern "C" void oai_init_audio_capture(void);
extern "C" void oai_init_audio_decoder(void);
extern "C" void oai_init_audio_encoder();
extern "C" void oai_send_audio(PeerConnection *peer_connection);
extern "C" void oai_audio_decode(uint8_t *data, size_t size);
extern "C" void oai_webrtc();
extern "C" void oai_http_request(char *offer, char *answer);

extern "C" int cmd_init(void);

// Voice Activity Detection (VAD) Configuration
#define VAD_THRESHOLD_VOICE 150          // Volume threshold to detect voice
#define VAD_SILENCE_TIMEOUT_MS 2000      // Stop sending after 2 seconds of silence
#define VAD_MIN_VOICE_DURATION_MS 100    // Minimum voice duration to start sending

// Provided by SenseCAP SDK example; declare here for use in app_main
extern "C" void board_init(void);
extern "C" void long_press_event_cb(void);