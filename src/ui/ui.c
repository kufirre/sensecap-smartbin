#include "ui.h"
#include "esp_lvgl_port.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sensecap-watcher.h"
#include "iot_button.h"
#include "wifi.h"

// Assume images are provided by lvgl assets
extern const lv_img_dsc_t speaking_A;
extern const lv_img_dsc_t speaking_B;
extern const lv_img_dsc_t speaking_C;
extern const lv_img_dsc_t speaking_D;
extern const lv_img_dsc_t speaking_E;

extern const lv_img_dsc_t listening_A;
extern const lv_img_dsc_t listening_B;
extern const lv_img_dsc_t listening_C;
extern const lv_img_dsc_t listening_D;
extern const lv_img_dsc_t listening_E;

static const lv_img_dsc_t *speaking_images[] = {
    &speaking_A,
    &speaking_B,
    &speaking_C,
    &speaking_D,
    &speaking_E
};

static const lv_img_dsc_t *listening_images[] = {
    &listening_A,
    &listening_B,
    &listening_C,
    &listening_D,
    &listening_E
};

static lv_obj_t *label;
static lv_obj_t *img;
static uint8_t current_image_index = 0;
static bool is_speaking = false;
static lv_timer_t *timer2 = NULL;  // Animation timer for frames

// Button and LED state variables
static const char *UI_TAG = "ui";
static bool rgb_initialized = false;
static button_handle_t button_handle = NULL;
static void (*short_press_callback)(void) = NULL;
static void (*long_press_callback)(void) = NULL;

static void timer2_callback(lv_timer_t *timer)
{
    const lv_img_dsc_t **images = is_speaking ? speaking_images : listening_images;
    current_image_index = (current_image_index + 1) % (sizeof(speaking_images) / sizeof(speaking_images[0]));
    lv_img_set_src(img, images[current_image_index]);
}

void ui_switch_speaking(void)
{
    lvgl_port_lock(0);
    // Hide label when showing animation
    if (label) {
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    }
    if (!is_speaking) {
        is_speaking = true;
        current_image_index = 0;
        if (img) {
            lv_obj_clear_flag(img, LV_OBJ_FLAG_HIDDEN);
            lv_img_set_src(img, speaking_images[current_image_index]);
            lv_obj_align(img, LV_ALIGN_CENTER, 0, 0); // Re-center after setting source
        }
        // No need for timer1 - the media layer handles speaking duration
        // based on actual audio stream timing (3-second silence detection)
    }
    lvgl_port_unlock();
}

void ui_listening(void)
{
    lvgl_port_lock(0);
    // Reset speaking state when switching to listening
    is_speaking = false;
    // Hide label when showing animation
    if (label) {
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    }
    if (!img) {
        img = lv_img_create(lv_scr_act());
        lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
    }
    lv_obj_clear_flag(img, LV_OBJ_FLAG_HIDDEN);
    lv_img_set_src(img, listening_images[current_image_index]);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0); // Re-center after setting source
    if (!timer2) {
        timer2 = lv_timer_create(timer2_callback, 300, NULL);
        lv_timer_set_repeat_count(timer2, -1);
    } else {
        // Resume the timer if it exists
        lv_timer_resume(timer2);
    }
    lvgl_port_unlock();
}

void ui_wifi_connecting(void)
{
    lvgl_port_lock(0);
    // Hide image when showing text
    if (img) {
        lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
    }
    if (!label) {
        label = lv_label_create(lv_scr_act());
        lv_obj_set_width(label, LV_SIZE_CONTENT); // Auto-size to content
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        // Make text larger and centered
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    }
    lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(label, "Wi-Fi Connecting...");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    lvgl_port_unlock();
}

void ui_show_status(const char* status_text)
{
    lvgl_port_lock(0);
    // Hide image when showing text
    if (img) {
        lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
    }
    if (!label) {
        label = lv_label_create(lv_scr_act());
        lv_obj_set_width(label, LV_SIZE_CONTENT); // Auto-size to content
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        // Make text larger and centered
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    }
    lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(label, status_text);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    lvgl_port_unlock();
}

void ui_init(void)
{
    lvgl_port_lock(0);
    if (!label) {
        label = lv_label_create(lv_scr_act());
        lv_obj_set_width(label, LV_SIZE_CONTENT); // Auto-size to content
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        // Make text larger and centered
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    }
    lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(label, "Configure Wifi and OpenAI key via serial port.");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    if (!img) {
        img = lv_img_create(lv_scr_act());
        lv_obj_align(img, LV_ALIGN_CENTER, 0, 30);
    }
    // Hide image initially
    lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
    lvgl_port_unlock();
}

// Button and LED management functions

// Button event callbacks
static void button_short_press_cb(void *arg, void *data)
{
    ESP_LOGI(UI_TAG, "Button short press detected");
    if (short_press_callback != NULL) {
        short_press_callback();
    }
}

static void button_long_press_cb(void *arg, void *data)
{
    ESP_LOGI(UI_TAG, "Button long press detected");
    if (long_press_callback != NULL) {
        long_press_callback();
    }
}

esp_err_t ui_button_init(void (*short_press_cb)(void), void (*long_press_cb)(void))
{
    ESP_LOGI(UI_TAG, "Initializing button with short and long press callbacks...");
    
    // Store callback functions
    short_press_callback = short_press_cb;
    long_press_callback = long_press_cb;
    
    // Create button configuration using custom button (BSP functions)
    button_config_t btn_cfg = {
        .type = BUTTON_TYPE_CUSTOM,
        .long_press_time = 2000,  // 2 seconds for long press
        .short_press_time = 200,  // 200ms for short press detection
        .custom_button_config = {
            .active_level = 0,
            .button_custom_init = bsp_knob_btn_init,
            .button_custom_deinit = bsp_knob_btn_deinit,
            .button_custom_get_key_value = bsp_knob_btn_get_key_value,
        },
    };
    
    // Create button handle
    button_handle = iot_button_create(&btn_cfg);
    if (button_handle == NULL) {
        ESP_LOGE(UI_TAG, "Failed to create button handle");
        return ESP_FAIL;
    }
    
    // Register short press callback (single click)
    if (short_press_cb != NULL) {
        esp_err_t ret = iot_button_register_cb(button_handle, BUTTON_SINGLE_CLICK, button_short_press_cb, NULL);
        if (ret != ESP_OK) {
            ESP_LOGE(UI_TAG, "Failed to register short press callback");
            return ESP_FAIL;
        }
        ESP_LOGI(UI_TAG, "Short press callback registered");
    }
    
    // Register long press callback
    if (long_press_cb != NULL) {
        esp_err_t ret = iot_button_register_cb(button_handle, BUTTON_LONG_PRESS_START, button_long_press_cb, NULL);
        if (ret != ESP_OK) {
            ESP_LOGE(UI_TAG, "Failed to register long press callback");
            return ESP_FAIL;
        }
        ESP_LOGI(UI_TAG, "Long press callback registered");
    }
    
    ESP_LOGI(UI_TAG, "Button initialized successfully");
    return ESP_OK;
}

esp_err_t ui_camera_flash(uint32_t duration_ms)
{
    ESP_LOGI(UI_TAG, "Camera flash for %ld ms", duration_ms);
    
    // Ensure RGB is initialized
    if (!rgb_initialized) {
        if (bsp_rgb_init() != ESP_OK) {
            ESP_LOGE(UI_TAG, "Failed to initialize RGB LED");
            return ESP_FAIL;
        }
        rgb_initialized = true;
    }
    
    // Turn on white LED for flash at 5% brightness to avoid lens glare
    esp_err_t ret = bsp_rgb_set(13, 13, 13);  // 5% of 255 = ~13
    if (ret != ESP_OK) {
        ESP_LOGE(UI_TAG, "Failed to turn on camera flash");
        return ret;
    }
    
    // Wait for specified duration
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    
    // Turn off LED
    ret = bsp_rgb_set(0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(UI_TAG, "Failed to turn off camera flash");
        return ret;
    }
    
    ESP_LOGI(UI_TAG, "Camera flash completed");
    return ESP_OK;
}

esp_err_t ui_set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    // Ensure RGB is initialized
    if (!rgb_initialized) {
        if (bsp_rgb_init() != ESP_OK) {
            ESP_LOGE(UI_TAG, "Failed to initialize RGB LED");
            return ESP_FAIL;
        }
        rgb_initialized = true;
    }
    
    return bsp_rgb_set(r, g, b);
}

esp_err_t ui_rgb_off(void)
{
    return ui_set_rgb(0, 0, 0);
}

void ui_wifi_connected(void)
{
    if (lvgl_port_lock(0)) {
        if (timer2) {
            lv_timer_del(timer2);
            timer2 = NULL;
        }
        lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
        
        // Show both IP addresses when connected
        char* sta_ip = smartbin_wifi_get_ip_address();
        char* ap_ip = smartbin_wifi_get_ap_ip_address();
        char status_text[256];
        snprintf(status_text, sizeof(status_text), 
                 LV_SYMBOL_WIFI " WiFi Connected\n"
                 "Device: %s\nConfig: %s", 
                 sta_ip, ap_ip);
        lv_label_set_text(label, status_text);
        
        lvgl_port_unlock();
    }
    ESP_LOGI(UI_TAG, "UI: WiFi Connected - STA IP: %s, AP IP: %s", 
             smartbin_wifi_get_ip_address(), smartbin_wifi_get_ap_ip_address());
}

void ui_wifi_config_mode(void)
{
    if (lvgl_port_lock(0)) {
        if (timer2) {
            lv_timer_del(timer2);
            timer2 = NULL;
        }
        lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
        
        // Get actual AP IP address
        char* ap_ip = smartbin_wifi_get_ap_ip_address();
        char config_text[128];
        snprintf(config_text, sizeof(config_text), 
                 LV_SYMBOL_SETTINGS " Configuration Mode\n" LV_SYMBOL_WIFI " IP: %s", ap_ip);
        lv_label_set_text(label, config_text);
        
        lvgl_port_unlock();
    }
    ESP_LOGI(UI_TAG, "UI: Configuration Mode");
}

bool ui_has_display(void)
{
    // SenseCAP Watcher has a display
    return true;
}

