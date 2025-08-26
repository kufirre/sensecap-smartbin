#include "smartbin_system.h"
#include "sensecap-watcher.h"
#include <assert.h>
#include <esp_log.h>

void smartbin_board_init(void)
{
  bsp_io_expander_init();
  lv_disp_t *lvgl_disp = bsp_lvgl_init();
  assert(lvgl_disp != NULL);
  bsp_rgb_init();
  
  // Set proper display brightness (80% for vibrant colors)
  bsp_lcd_brightness_set(80);
  
  bsp_codec_init();
  bsp_codec_volume_set(100, NULL);
}

void smartbin_board_long_press_event_cb(void)
{
  ESP_LOGI("", "long_press_event_cb");
  bsp_system_shutdown();
  bsp_lcd_brightness_set(0);
  bsp_codec_mute_set(true);
  vTaskDelay(pdMS_TO_TICKS(3000));
  esp_restart();
}


