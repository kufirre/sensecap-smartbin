#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize console command system
 * 
 * Sets up ESP console with various diagnostic and configuration commands:
 * - WiFi configuration commands
 * - OpenAI API key management
 * - System status commands
 * - Configuration management
 * 
 * @return 0 on success, non-zero on error
 */
int smartbin_cmd_init(void);

#ifdef __cplusplus
}
#endif