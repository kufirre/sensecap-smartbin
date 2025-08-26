#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

/**
 * @brief Initialize and start WebRTC connection to OpenAI Realtime API
 * 
 * Establishes a WebRTC connection for real-time bidirectional audio
 * communication with OpenAI's realtime API.
 */
void smartbin_webrtc_start(void);

/**
 * @brief Restart WebRTC session
 * 
 * Tears down current session and establishes a new one. Used when
 * session becomes unhealthy or times out.
 */
void smartbin_webrtc_restart_session(void);

#ifdef __cplusplus
}
#endif