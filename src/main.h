// Replicate example header layout
#pragma once

#include <peer.h>
#include "sensecap-watcher.h"
#include "ui/ui.h"

#define LOG_TAG "realtimeapi-sdk"
#define MAX_HTTP_OUTPUT_BUFFER 2048

// Legacy function declarations removed - use component functions directly

// Use smartbin_cmd component functions instead

// Voice Activity Detection (VAD) Configuration
#define VAD_THRESHOLD_VOICE 300          // Volume threshold to detect voice (increased to prevent false triggers)
#define VAD_SILENCE_TIMEOUT_MS 2000      // Stop sending after 2 seconds of silence
#define VAD_MIN_VOICE_DURATION_MS 100    // Minimum voice duration to start sending

// Session Health Configuration
#define SESSION_TIMEOUT_MS 30000         // Restart session if no response for 30 seconds
#define SESSION_HEALTH_CHECK_INTERVAL_MS 5000  // Check session health every 5 seconds

// Use smartbin_system component functions instead