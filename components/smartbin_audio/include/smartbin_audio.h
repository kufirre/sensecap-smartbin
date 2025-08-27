#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Forward declaration
typedef struct PeerConnection PeerConnection;

/**
 * Try to set the speaker output sample rate.
 * Tries BSP-provided setters first, then codec-dev (if present).
 *
 * @param sr_hz Target sample rate in Hz (e.g., 24000).
 */
void smartbin_audio_set_out_sr(uint32_t sr_hz);

/**
 * @brief Initialize audio capture system
 * 
 * Initializes the SenseCAP audio codec for microphone capture
 * with optimized settings for voice communication.
 */
void smartbin_audio_init_capture(void);

/**
 * @brief Initialize OPUS audio decoder
 * 
 * Sets up OPUS decoder for incoming audio streams with optimized
 * parameters for voice communication.
 */
void smartbin_audio_init_decoder(void);

/**
 * @brief Initialize OPUS audio encoder
 * 
 * Sets up OPUS encoder for outgoing audio streams with voice
 * optimization settings.
 */
void smartbin_audio_init_encoder(void);

/**
 * @brief Send audio data via peer connection
 * 
 * Captures audio from microphone, encodes with OPUS, and sends
 * via the provided peer connection with voice activity detection.
 * 
 * @param peer_connection WebRTC peer connection for audio transmission
 */
void smartbin_audio_send(PeerConnection *peer_connection);

/**
 * @brief Decode and play received audio data
 * 
 * Decodes OPUS audio data and plays it through the speaker system
 * with feedback prevention and session health monitoring.
 * 
 * @param data OPUS-encoded audio data
 * @param size Size of audio data in bytes
 */
void smartbin_audio_decode_and_play(uint8_t *data, size_t size);

/**
 * @brief Get timestamp of last received audio response
 * 
 * @return Timestamp in milliseconds of last audio response
 */
uint64_t smartbin_audio_get_last_response_time(void);

/**
 * @brief Check if audio session is healthy
 * 
 * @return true if session is healthy, false if timeout detected
 */
bool smartbin_audio_is_session_healthy(void);

#ifdef __cplusplus
}
#endif