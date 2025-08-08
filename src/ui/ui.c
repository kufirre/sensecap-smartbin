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
    if (!is_speaking) {
        is_speaking = true;
        current_image_index = 0;
        lv_img_set_src(img, speaking_images[current_image_index]);
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
    if (!img) {
        img = lv_img_create(lv_scr_act());
        lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
    }
    lv_img_set_src(img, listening_images[current_image_index]);
    if (!timer2) {
        timer2 = lv_timer_create(timer2_callback, 300, NULL);
        lv_timer_set_repeat_count(timer2, -1);
    }
    lvgl_port_unlock();
}

void ui_wifi_connecting(void)
{
    lvgl_port_lock(0);
    if (!label) {
        label = lv_label_create(lv_scr_act());
        lv_obj_set_width(label, LV_PCT(100));
        lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    }
    lv_label_set_text(label, "Wi-Fi Connecting...");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    lvgl_port_unlock();
}

void ui_init(void)
{
    lvgl_port_lock(0);
    if (!label) {
        label = lv_label_create(lv_scr_act());
        lv_obj_set_width(label, LV_PCT(100));
        lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    }
    lv_label_set_text(label, "Configure Wifi and OpenAI key via serial port.");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    if (!img) {
        img = lv_img_create(lv_scr_act());
        lv_obj_align(img, LV_ALIGN_CENTER, 0, 30);
    }
    lvgl_port_unlock();
}


