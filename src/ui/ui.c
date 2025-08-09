#include "ui.h"
#include "esp_lvgl_port.h"

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
static lv_timer_t *timer1 = NULL;
static lv_timer_t *timer2 = NULL;

static void timer2_callback(lv_timer_t *timer)
{
    const lv_img_dsc_t **images = is_speaking ? speaking_images : listening_images;
    current_image_index = (current_image_index + 1) % (sizeof(speaking_images) / sizeof(speaking_images[0]));
    lv_img_set_src(img, images[current_image_index]);
}

static void timer1_callback(lv_timer_t *timer)
{
    is_speaking = false;
    lv_timer_reset(timer2);
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
        if (timer1) {
            lv_timer_reset(timer1);
        } else {
            timer1 = lv_timer_create(timer1_callback, 1000, NULL);
        }
    } else {
        if (timer1) {
            lv_timer_reset(timer1);
        }
    }
    lvgl_port_unlock();
}

void ui_listening(void)
{
    lvgl_port_lock(0);
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


