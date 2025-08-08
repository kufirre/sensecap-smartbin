#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <esp_err.h>

// Global OpenAI API key buffer
extern char g_openai_api_key_buf[165];

// Initialize console command system
int cmd_init(void);

#ifdef __cplusplus
}
#endif