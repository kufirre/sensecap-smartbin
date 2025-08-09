// UI API with proper C/C++ linkage guards
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_init(void);
void ui_switch_speaking(void);
void ui_listening(void);
void ui_wifi_connecting(void);
void ui_show_status(const char* status_text);

#ifdef __cplusplus
}
#endif


