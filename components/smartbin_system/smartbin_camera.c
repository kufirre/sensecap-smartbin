#include "smartbin_system.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "driver/uart.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "mbedtls/base64.h"

#include "esp_timer.h"
#include "esp_log.h"
#include "esp_check.h"

#include "sensecap-watcher.h"

#include "sscma_client_io.h"
#include "sscma_client_ops.h"

#include "esp_jpeg_dec.h"
#include <time.h>
#include <sys/time.h>

static const char *TAG = "camera";

// Flash control callbacks (set by main application)
static smartbin_camera_flash_enable_cb_t g_flash_enable_cb = NULL;
static smartbin_camera_flash_disable_cb_t g_flash_disable_cb = NULL;
static smartbin_camera_flash_pulse_cb_t g_flash_pulse_cb = NULL;

// Internal flash control functions
static esp_err_t camera_flash_enable_callback(void) {
    // Import UI function directly
    extern esp_err_t ui_set_rgb(uint8_t r, uint8_t g, uint8_t b);
    return ui_set_rgb(13, 13, 13);  // 5% brightness for continuous flash
}

static esp_err_t camera_flash_disable_callback(void) {
    // Import UI function directly  
    extern esp_err_t ui_rgb_off(void);
    return ui_rgb_off();
}

static esp_err_t camera_flash_pulse_callback(uint32_t duration_ms) {
    // Import UI function directly
    extern esp_err_t ui_camera_flash(uint32_t duration_ms);
    return ui_camera_flash(duration_ms);
}

void smartbin_camera_register_flash_callbacks(
    smartbin_camera_flash_enable_cb_t enable_cb,
    smartbin_camera_flash_disable_cb_t disable_cb,
    smartbin_camera_flash_pulse_cb_t pulse_cb
) {
    // Use internal callbacks if none provided
    g_flash_enable_cb = enable_cb ? enable_cb : camera_flash_enable_callback;
    g_flash_disable_cb = disable_cb ? disable_cb : camera_flash_disable_callback;
    g_flash_pulse_cb = pulse_cb ? pulse_cb : camera_flash_pulse_callback;
    ESP_LOGI(TAG, "Flash control callbacks registered");
}

// Module state
static esp_io_expander_handle_t io_expander = NULL;
static sscma_client_handle_t client = NULL;
static esp_lcd_panel_handle_t lcd_panel = NULL;
static lv_disp_t *lvgl_disp = NULL;
static lv_obj_t *image = NULL;
static bool smartbin_camera_initialized = false;
static bool smartbin_camera_streaming = false;
static bool sd_card_available = false;
static bool save_next_image_to_sd = false;
static size_t last_image_len = 0;  // Length of last captured JPEG image
static SemaphoreHandle_t image_ready_semaphore = NULL;  // Semaphore to signal image ready

#define EXAMPLE_SAVE_IMAGE_TO_SD 0

#define DECODED_STR_MAX_SIZE (48 * 1024)
static unsigned char decoded_str[DECODED_STR_MAX_SIZE];

#define IMG_WIDTH  640
#define IMG_HEIGHT 480
static lv_img_dsc_t img_dsc = {
    .header.always_zero = 0,
    .header.w = IMG_WIDTH,
    .header.h = IMG_HEIGHT,
    .data_size = IMG_WIDTH * IMG_HEIGHT * LV_COLOR_DEPTH / 8,
    .header.cf = LV_IMG_CF_TRUE_COLOR,
    .data = NULL,
};
// Function to save JPEG data to SD card with timestamp filename
static esp_err_t save_jpeg_to_sd(const uint8_t *jpeg_data, size_t jpeg_size);

static int esp_jpeg_decoder_one_picture(uint8_t *input_buf, int len, uint8_t *output_buf)
{
    esp_err_t ret = ESP_OK;
    // Generate default configuration
    jpeg_dec_config_t config = { .output_type = JPEG_RAW_TYPE_RGB565_BE, .rotate = JPEG_ROTATE_0D };

    // Empty handle to jpeg_decoder
    jpeg_dec_handle_t jpeg_dec = NULL;

    // Create jpeg_dec
    jpeg_dec = jpeg_dec_open(&config);

    // Create io_callback handle
    jpeg_dec_io_t *jpeg_io = calloc(1, sizeof(jpeg_dec_io_t));
    if (jpeg_io == NULL)
    {
        return ESP_FAIL;
    }

    // Create out_info handle
    jpeg_dec_header_info_t *out_info = calloc(1, sizeof(jpeg_dec_header_info_t));
    if (out_info == NULL)
    {
        return ESP_FAIL;
    }

    // Set input buffer and buffer len to io_callback
    jpeg_io->inbuf = input_buf;
    jpeg_io->inbuf_len = len;

    // Parse jpeg picture header and get picture for user and decoder
    ret = jpeg_dec_parse_header(jpeg_dec, jpeg_io, out_info);

    if (ret < 0)
    {
        goto _exit;
    }

    jpeg_io->outbuf = output_buf;
    int inbuf_consumed = jpeg_io->inbuf_len - jpeg_io->inbuf_remain;
    jpeg_io->inbuf = input_buf + inbuf_consumed;
    jpeg_io->inbuf_len = jpeg_io->inbuf_remain;

    // Start decode jpeg raw data
    ret = jpeg_dec_process(jpeg_dec, jpeg_io);
    if (ret < 0)
    {
        goto _exit;
    }

_exit:
    // Decoder deinitialize
    jpeg_dec_close(jpeg_dec);
    free(out_info);
    free(jpeg_io);
    return ret;
}

void display_one_image(lv_obj_t *image, const unsigned char *p_data)
{
    int64_t start = 0, end = 0;
    if (!p_data)
        return;

    size_t str_len = strlen((const char *)p_data);
    size_t output_len = 0;

    start = esp_timer_get_time();
    int decode_ret = mbedtls_base64_decode(decoded_str, DECODED_STR_MAX_SIZE, &output_len, p_data, str_len);
    end = esp_timer_get_time();
    ESP_LOGI(TAG, "mbedtls_base64_decode time:%lld ms", (end - start) / 1000);
    if (decode_ret == 0)
    {
        // Store the length of the last captured JPEG image
        last_image_len = output_len;
        
        // Signal that image is ready (from ISR context)
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        if (image_ready_semaphore != NULL) {
            xSemaphoreGiveFromISR(image_ready_semaphore, &xHigherPriorityTaskWoken);
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
        
        // Save JPEG data to SD card if requested
        if (save_next_image_to_sd && sd_card_available) {
            ESP_LOGI(TAG, "Saving JPEG image to SD card...");
            if (save_jpeg_to_sd(decoded_str, output_len) == ESP_OK) {
                ESP_LOGI(TAG, "✓ Image saved to SD card successfully");
            } else {
                ESP_LOGW(TAG, "Failed to save image to SD card");
            }
            save_next_image_to_sd = false;  // Reset flag
        }

        if (img_dsc.data == NULL)
        {
            img_dsc.data = heap_caps_aligned_alloc(16, img_dsc.data_size, MALLOC_CAP_SPIRAM);
        }
        start = esp_timer_get_time();
        int ret = esp_jpeg_decoder_one_picture(decoded_str, output_len, (uint8_t*)img_dsc.data);
        end = esp_timer_get_time();
        ESP_LOGI(TAG, "esp_jpeg_decoder_one_picture take:%lld ms", (end - start) / 1000);
        if (ret == ESP_OK)
        {
            lv_img_set_src(image, &img_dsc);

            // esp_lcd_panel_draw_bitmap(lcd_panel, 0, 0, IMG_HEIGHT, IMG_WIDTH, img_dsc.data);

#if EXAMPLE_SAVE_IMAGE_TO_SD
            static char file_name[50];
            start = esp_timer_get_time();
            sprintf(file_name, "/sdcard/save/_%lld.jpg", esp_timer_get_time());
            FILE *fp = fopen(file_name, "wb");
            if (fp != NULL)
            {
                fwrite(decoded_str, 1, output_len, fp);
                fclose(fp);
                end = esp_timer_get_time();
                ESP_LOGI(TAG, "Save image to %s take %lld ms", file_name, (end - start) / 1000);
            }
            else
            {
                ESP_LOGE(TAG, "Open file %s failed", file_name);
            }
#endif
        }
    }
    else if (decode_ret == MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL)
    {
        ESP_LOGE(TAG, "Buffer too small for decoding %d bytes %d output", str_len, output_len);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to decode Base64 string, error: %d", decode_ret);
    }
    return;
}

void on_event(sscma_client_handle_t client, const sscma_client_reply_t *reply, void *user_ctx)
{
    // Note: reply is automatically recycled after exiting the function.

    char *img = NULL;
    int img_size = 0;

    if (sscma_utils_fetch_image_from_reply(reply, &img, &img_size) == ESP_OK)
    {
        if (lvgl_port_lock(0))
        {
            ESP_LOGI(TAG, "Got a new image: %d bytes", img_size);
            display_one_image(image, (const unsigned char *)img);
            lvgl_port_unlock();
        }

        free(img);
    }
    sscma_client_box_t *boxes = NULL;
    int box_count = 0;
    if (sscma_utils_fetch_boxes_from_reply(reply, &boxes, &box_count) == ESP_OK)
    {
        if (box_count > 0)
        {
            for (int i = 0; i < box_count; i++)
            {
                printf("[box %d]: x=%d, y=%d, w=%d, h=%d, score=%d, target=%d\n", i, boxes[i].x, boxes[i].y, boxes[i].w, boxes[i].h, boxes[i].score, boxes[i].target);
            }
        }
        free(boxes);
    }

    sscma_client_class_t *classes = NULL;
    int class_count = 0;
    if (sscma_utils_fetch_classes_from_reply(reply, &classes, &class_count) == ESP_OK)
    {
        if (class_count > 0)
        {
            for (int i = 0; i < class_count; i++)
            {
                printf("[class %d]: target=%d, score=%d\n", i, classes[i].target, classes[i].score);
            }
        }
        free(classes);
    }

    sscma_client_point_t *points = NULL;
    int point_count = 0;
    if (sscma_utils_fetch_points_from_reply(reply, &points, &point_count) == ESP_OK)
    {
        if (point_count > 0)
        {
            for (int i = 0; i < point_count; i++)
            {
                printf("[point %d]: x=%d, y=%d, z=%d, score=%d, target=%d\n", i, points[i].x, points[i].y, points[i].z, points[i].score, points[i].target);
            }
        }
        free(points);
    }

    sscma_client_keypoint_t *keypoints = NULL;
    int keypoints_count = 0;
    if (sscma_utils_fetch_keypoints_from_reply(reply, &keypoints, &keypoints_count) == ESP_OK)
    {
        if (keypoints_count > 0)
        {
            for (int i = 0; i < keypoints_count; i++)
            {
                printf("[keypoint %d]: [x=%d, y=%d, w=%d, h=%d, score=%d, target=%d]\n", i, keypoints[i].box.x, keypoints[i].box.y, keypoints[i].box.w, keypoints[i].box.h, keypoints[i].box.score,
                    keypoints[i].box.target);
                for (int j = 0; j < keypoints[i].points_num; j++)
                {
                    printf("\t [point %d]: [x=%d, y=%d, z=%d, score=%d, target=%d]\n", j, keypoints[i].points[j].x, keypoints[i].points[j].y, keypoints[i].points[j].z, keypoints[i].points[j].score,
                        keypoints[i].points[j].target);
                }
            }
        }
        free(keypoints);
    }

    return;
}

void on_log(sscma_client_handle_t client, const sscma_client_reply_t *reply, void *user_ctx)
{
    if (reply->len >= 100)
    {
        strcpy(&reply->data[100 - 4], "...");
    }
    // Note: reply is automatically recycled after exiting the function.
    printf("log: %s\n", reply->data);
}

void on_connect(sscma_client_handle_t client, const sscma_client_reply_t *reply, void *user_ctx)
{
    printf("on_connect\n");
}

esp_err_t smartbin_camera_init(void)
{
    if (smartbin_camera_initialized) {
        ESP_LOGW(TAG, "Camera already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing camera module...");

    // Create semaphore for image ready signaling
    image_ready_semaphore = xSemaphoreCreateBinary();
    if (image_ready_semaphore == NULL) {
        ESP_LOGE(TAG, "Failed to create image ready semaphore");
        return ESP_FAIL;
    }

    // Initialize IO expander
    io_expander = bsp_io_expander_init();
    if (io_expander == NULL) {
        ESP_LOGE(TAG, "Failed to initialize IO expander");
        return ESP_FAIL;
    }

    // Get existing LVGL display (already initialized in board_init)
    lvgl_disp = bsp_lvgl_get_disp();
    if (lvgl_disp == NULL) {
        ESP_LOGE(TAG, "Failed to get LVGL display handle");
        return ESP_FAIL;
    }

    // Initialize SSCMA client
    client = bsp_sscma_client_init();
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize SSCMA client");
        return ESP_FAIL;
    }

#if EXAMPLE_SAVE_IMAGE_TO_SD
    bsp_sdcard_init_default();
    struct stat st;
    if (stat("/sdcard/save", &st) == -1) {
        mkdir("/sdcard/save", 0755);
    }
#endif

    // Get LCD panel handle
    lcd_panel = bsp_lcd_get_panel_handle();
    if (lcd_panel == NULL) {
        ESP_LOGE(TAG, "Failed to get LCD panel handle");
        return ESP_FAIL;
    }

    // Create LVGL image object
    image = lv_img_create(lv_scr_act());
    lv_obj_set_align(image, LV_ALIGN_CENTER);

    // Register SSCMA client callbacks
    const sscma_client_callback_t callback = {
        .on_connect = on_connect,
        .on_event = on_event,
        .on_log = on_log,
    };

    if (sscma_client_register_callback(client, &callback, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register SSCMA client callbacks");
        return ESP_FAIL;
    }

    // Initialize SSCMA client
    if (sscma_client_init(client) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SSCMA client");
        return ESP_FAIL;
    }

    // Set model (model ID 1)
    if (sscma_client_set_model(client, 1) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set SSCMA model");
        return ESP_FAIL;
    }

    // Try to initialize SD card (optional)
    ESP_LOGI(TAG, "Checking for SD card...");
    if (bsp_sdcard_is_inserted()) {
        ESP_LOGI(TAG, "SD card detected, initializing...");
        if (bsp_sdcard_init_default() == ESP_OK) {
            sd_card_available = true;
            ESP_LOGI(TAG, "✓ SD card initialized successfully");
        } else {
            ESP_LOGW(TAG, "Failed to initialize SD card");
            sd_card_available = false;
        }
    } else {
        ESP_LOGI(TAG, "No SD card detected");
        sd_card_available = false;
    }

    // Initialize flash callbacks with default implementations
    smartbin_camera_register_flash_callbacks(NULL, NULL, NULL);

    smartbin_camera_initialized = true;
    ESP_LOGI(TAG, "Camera module initialized successfully");
    return ESP_OK;
}

esp_err_t smartbin_camera_get_info(void)
{
    if (!smartbin_camera_initialized) {
        ESP_LOGE(TAG, "Camera not initialized");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Getting camera module information...");

    // Get device info
    sscma_client_info_t *info;
    if (sscma_client_get_info(client, &info, true) == ESP_OK) {
        ESP_LOGI(TAG, "Device ID: %s", (info->id != NULL) ? info->id : "NULL");
        ESP_LOGI(TAG, "Device Name: %s", (info->name != NULL) ? info->name : "NULL");
        ESP_LOGI(TAG, "Hardware Version: %s", (info->hw_ver != NULL) ? info->hw_ver : "NULL");
        ESP_LOGI(TAG, "Software Version: %s", (info->sw_ver != NULL) ? info->sw_ver : "NULL");
        ESP_LOGI(TAG, "Firmware Version: %s", (info->fw_ver != NULL) ? info->fw_ver : "NULL");
    } else {
        ESP_LOGE(TAG, "Failed to get device info");
        return ESP_FAIL;
    }

    // Get model info
    sscma_client_model_t *model;
    if (sscma_client_get_model(client, &model, true) == ESP_OK) {
        ESP_LOGI(TAG, "Model ID: %d", model->id ? model->id : -1);
        ESP_LOGI(TAG, "Model UUID: %s", model->uuid ? model->uuid : "N/A");
        ESP_LOGI(TAG, "Model Name: %s", model->name ? model->name : "N/A");
        ESP_LOGI(TAG, "Model Version: %s", model->ver ? model->ver : "N/A");
        ESP_LOGI(TAG, "Model URL: %s", model->url ? model->url : "N/A");
        ESP_LOGI(TAG, "Model Checksum: %s", model->checksum ? model->checksum : "N/A");
        ESP_LOGI(TAG, "Model Classes:");
        if (model->classes[0] != NULL) {
            for (int i = 0; model->classes[i] != NULL; i++) {
                ESP_LOGI(TAG, "  - %s", model->classes[i]);
            }
        } else {
            ESP_LOGI(TAG, "  N/A");
        }
    } else {
        ESP_LOGE(TAG, "Failed to get model info");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t smartbin_camera_start_streaming(bool enable_flash)
{
    if (!smartbin_camera_initialized) {
        ESP_LOGE(TAG, "Camera not initialized");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Starting camera streaming at %dx%d (flash: %s)...", IMG_WIDTH, IMG_HEIGHT, enable_flash ? "enabled" : "disabled");

    // Configure sensor (opt id 3 = 640x480 resolution - highest quality)
    // Available options:
    // 0: 240 x 240
    // 1: 416 x 416  
    // 2: 480 x 480
    // 3: 640 x 480 (VGA - highest quality)
    if (sscma_client_set_sensor(client, 1, 3, true) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set sensor configuration");
        return ESP_FAIL;
    }

    vTaskDelay(50 / portTICK_PERIOD_MS);

    // Enable flash LED for low-light streaming if requested
    if (enable_flash) {
        ESP_LOGI(TAG, "Enabling continuous flash for streaming...");
        if (g_flash_enable_cb) {
            if (g_flash_enable_cb() != ESP_OK) {
                ESP_LOGW(TAG, "Failed to enable flash, continuing without it");
            }
        }
    }

    // Start inference/streaming
    // if (sscma_client_invoke(client, -1, false, true) != ESP_OK) {
    //     ESP_LOGE(TAG, "Failed to start camera streaming");
    //     return ESP_FAIL;
    // }

    if (sscma_client_sample(client, -1) != ESP_OK)
    {
        printf("sample failed\n");
    }

    smartbin_camera_streaming = true;
    ESP_LOGI(TAG, "Camera streaming started successfully");
    return ESP_OK;
}

esp_err_t smartbin_smartbin_camera_stop_streaming(void)
{
    if (!smartbin_camera_initialized) {
        ESP_LOGE(TAG, "Camera not initialized");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Stopping camera streaming...");

    // Turn off flash LED if it was on
    if (g_flash_disable_cb) {
        if (g_flash_disable_cb() != ESP_OK) {
            ESP_LOGW(TAG, "Failed to turn off flash LED");
        }
    }

    // Stop inference/streaming
    if (sscma_client_invoke(client, 0, false, false) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop camera streaming");
        return ESP_FAIL;
    }

    smartbin_camera_streaming = false;
    ESP_LOGI(TAG, "Camera streaming stopped");
    return ESP_OK;
}


esp_err_t smartbin_camera_capture_picture(bool enable_flash, bool save_to_sd)
{
    if (!smartbin_camera_initialized) {
        ESP_LOGE(TAG, "Camera not initialized");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Capturing single picture at %dx%d (flash: %s, SD save: %s)...", 
             IMG_WIDTH, IMG_HEIGHT, 
             enable_flash ? "enabled" : "disabled",
             save_to_sd ? "enabled" : "disabled");

    // Configure sensor if not already done (opt id 3 = 640x480 resolution - highest quality)
    if (sscma_client_set_sensor(client, 1, 3, true) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set sensor configuration");
        return ESP_FAIL;
    }

    vTaskDelay(50 / portTICK_PERIOD_MS);

    // Set flag for SD save if requested
    save_next_image_to_sd = save_to_sd;

    // Flash LED for illumination before capture if enabled
    if (enable_flash) {
        ESP_LOGI(TAG, "Activating camera flash...");
        if (g_flash_pulse_cb) {
            if (g_flash_pulse_cb(500) != ESP_OK) {
                ESP_LOGW(TAG, "Camera flash failed, continuing with capture");
            }
        }
        
        // Small delay after flash before capture
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    // Capture single image (times = 1)
    if (sscma_client_sample(client, 1) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to capture picture");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Picture capture initiated - image will appear when received");
    return ESP_OK;
}

bool smartbin_camera_is_initialized(void)
{
    return smartbin_camera_initialized;
}

bool smartbin_camera_is_streaming(void)
{
    return smartbin_camera_streaming;
}

sscma_client_handle_t smartbin_camera_get_client(void)
{
    return client;
}

esp_err_t smartbin_camera_wait_for_image(uint32_t timeout_ms)
{
    if (!smartbin_camera_initialized) {
        ESP_LOGE(TAG, "Camera not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (image_ready_semaphore == NULL) {
        ESP_LOGE(TAG, "Image ready semaphore not created");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Wait for semaphore with timeout
    TickType_t timeout_ticks = timeout_ms / portTICK_PERIOD_MS;
    if (xSemaphoreTake(image_ready_semaphore, timeout_ticks) == pdTRUE) {
        ESP_LOGI(TAG, "Image capture completed successfully");
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "Image capture timeout after %lu ms", timeout_ms);
        return ESP_ERR_TIMEOUT;
    }
}

esp_err_t smartbin_camera_get_last_image(const uint8_t **image_data, size_t *image_len)
{
    if (!smartbin_camera_initialized) {
        ESP_LOGE(TAG, "Camera not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!image_data || !image_len) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (last_image_len == 0) {
        ESP_LOGW(TAG, "No image captured yet");
        return ESP_ERR_NOT_FOUND;
    }
    
    *image_data = decoded_str;
    *image_len = last_image_len;
    
    ESP_LOGI(TAG, "Returning last captured image: %zu bytes", last_image_len);
    return ESP_OK;
}

esp_err_t smartbin_camera_deinit(void)
{
    if (!smartbin_camera_initialized) {
        ESP_LOGW(TAG, "Camera not initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Deinitializing camera module...");

    // Stop streaming if active
    smartbin_camera_stop_streaming();

    // Clean up resources
    if (img_dsc.data) {
        free((void*)img_dsc.data);
        img_dsc.data = NULL;
    }

    // Reset image state
    last_image_len = 0;
    
    // Clean up semaphore
    if (image_ready_semaphore != NULL) {
        vSemaphoreDelete(image_ready_semaphore);
        image_ready_semaphore = NULL;
    }

    smartbin_camera_initialized = false;
    smartbin_camera_streaming = false;
    ESP_LOGI(TAG, "Camera module deinitialized");
    return ESP_OK;
}

// Keep the app_main function temporarily for standalone testing
// This will be removed once main.cpp integration is complete
// void app_main(void)
// {
//     ESP_LOGI(TAG, "Starting camera standalone test...");
    
//     // Initialize camera
//     if (camera_init() != ESP_OK) {
//         ESP_LOGE(TAG, "Camera initialization failed");
//         return;
//     }

//     // Get camera info
//     camera_get_info();

//     // Start streaming
//     if (camera_start_streaming() != ESP_OK) {
//         ESP_LOGE(TAG, "Failed to start camera streaming");
//         return;
//     }

//     // Main loop for standalone testing
//     while (1) {
//         ESP_LOGI(TAG, "Free heap size = %ld", esp_get_free_heap_size());
//         vTaskDelay(5000 / portTICK_PERIOD_MS);
//     }
// }

// Function implementation: save JPEG data to SD card with timestamp filename
static esp_err_t save_jpeg_to_sd(const uint8_t *jpeg_data, size_t jpeg_size)
{
    if (!sd_card_available) {
        ESP_LOGW(TAG, "SD card not available, skipping save");
        return ESP_FAIL;
    }

    // Get current time for filename
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    // Create filename with timestamp: YYYYMMDD_HHMMSS.jpg
    char filename[64];
    snprintf(filename, sizeof(filename), "/sdcard/%04d%02d%02d_%02d%02d%02d.jpg",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    
    ESP_LOGI(TAG, "Saving image to: %s", filename);
    
    FILE *file = fopen(filename, "wb");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", filename);
        return ESP_FAIL;
    }
    
    size_t written = fwrite(jpeg_data, 1, jpeg_size, file);
    fclose(file);
    
    if (written != jpeg_size) {
        ESP_LOGE(TAG, "Failed to write complete image (wrote %zu of %zu bytes)", written, jpeg_size);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "✓ Image saved successfully (%zu bytes)", jpeg_size);
    return ESP_OK;
}
