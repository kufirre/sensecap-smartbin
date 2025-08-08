#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

static const char *TAG = "cmd";

#define PROMPT_STR "SenseCAP-SmartBin"
#define STORAGE_NAMESPACE "SenseCAP"
#define OPENAI_API_KEY_STORAGE "openaikey"

char g_openai_api_key_buf[165] = {0,};

static esp_err_t storage_write(char *p_key, void *p_data, size_t len)
{
    nvs_handle_t my_handle;
    esp_err_t err;
    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return err;

    err = nvs_set_blob(my_handle, p_key, p_data, len);
    if (err != ESP_OK) {
        nvs_close(my_handle);
        return err;
    }
    err = nvs_commit(my_handle);
    if (err != ESP_OK) {
        nvs_close(my_handle);
        return err;
    }
    nvs_close(my_handle);
    return ESP_OK;
}

static esp_err_t storage_read(char *p_key, void *p_data, size_t *p_len)
{
    nvs_handle_t my_handle;
    esp_err_t err;

    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return err;

    err = nvs_get_blob(my_handle, p_key, p_data, p_len);
    if (err != ESP_OK) {
        nvs_close(my_handle);
        return err;
    }
    nvs_close(my_handle);
    return ESP_OK;
}

/** wifi set command **/
static struct {
    struct arg_str *ssid;
    struct arg_str *password;
    struct arg_end *end;
} wifi_cfg_args;

static int wifi_cfg_set(int argc, char **argv)
{
    bool have_password = false;
    char ssid[32] = {0};
    char password[64] = {0};
    wifi_config_t wifi_config = { 0 };

    int nerrors = arg_parse(argc, argv, (void **) &wifi_cfg_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, wifi_cfg_args.end, argv[0]);
        return 1;
    }

    if (wifi_cfg_args.ssid->count) {
        int len = strlen(wifi_cfg_args.ssid->sval[0]);
        if (len > (sizeof(ssid) - 1)) { 
            ESP_LOGE(TAG, "SSID too long (max 31 bytes): %s", wifi_cfg_args.ssid->sval[0]);
            return -1;
        }
        strncpy(ssid, wifi_cfg_args.ssid->sval[0], 31);
    } else {
        ESP_LOGE(TAG, "No SSID provided");
        return -1;
    }

    if (wifi_cfg_args.password->count) {
        int len = strlen(wifi_cfg_args.password->sval[0]);
        if (len > (sizeof(password) - 1)) { 
            ESP_LOGE(TAG, "Password too long (max 63 bytes): %s", wifi_cfg_args.password->sval[0]);
            return -1;
        }
        have_password = true;
        strncpy(password, wifi_cfg_args.password->sval[0], 63);
    }
    
    strlcpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));

    if (have_password) {
        strlcpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    
    esp_wifi_stop();
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "WiFi configured - SSID: %s", wifi_config.sta.ssid);
    return 0;
}

static void register_cmd_wifi_sta(void)
{
    wifi_cfg_args.ssid = arg_str0("s", NULL, "<ssid>", "SSID of AP");
    wifi_cfg_args.password = arg_str0("p", NULL, "<password>", "password of AP");
    wifi_cfg_args.end = arg_end(2);

    const esp_console_cmd_t cmd = {
        .command = "wifi_sta",
        .help = "Configure WiFi station mode to join specified AP",
        .hint = NULL,
        .func = &wifi_cfg_set,
        .argtable = &wifi_cfg_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/** reboot command **/
static int do_reboot(int argc, char **argv)
{
    ESP_LOGI(TAG, "Rebooting device...");
    esp_restart();
    return 0;
}

static void register_cmd_reboot(void)
{
    const esp_console_cmd_t cmd = {
        .command = "reboot",
        .help = "Restart the device",
        .hint = NULL,
        .func = &do_reboot,
        .argtable = NULL
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/** openai api key set command **/
static struct {
    struct arg_str *key;
    struct arg_end *end;
} openai_api_key_args;

static int openai_api_key_set(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &openai_api_key_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, openai_api_key_args.end, argv[0]);
        return 1;
    }
    
    char key_buf[165] = {0,};
    size_t len = 0;
    
    if (openai_api_key_args.key->count) {
        len = strlen(openai_api_key_args.key->sval[0]);
        if (len >= sizeof(key_buf)) {
            ESP_LOGE(TAG, "API key too long (max 164 bytes): %s", openai_api_key_args.key->sval[0]);
            return -1;
        }
        strncpy(key_buf, openai_api_key_args.key->sval[0], len);

        ESP_LOGI(TAG, "Storing OpenAI API key");
        storage_write(OPENAI_API_KEY_STORAGE, (void *)key_buf, sizeof(key_buf));
    }
   
    len = sizeof(key_buf);
    memset(key_buf, 0, sizeof(key_buf));
    esp_err_t ret = storage_read(OPENAI_API_KEY_STORAGE, (void *)key_buf, &len);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Current OpenAI API key stored successfully");
    } else {
        ESP_LOGE(TAG, "No OpenAI API key found in storage");
    }
    return 0;
}

static void register_openai_api_key(void)
{
    openai_api_key_args.key = arg_str0("k", NULL, "<key>", "OpenAI API key (e.g. sk-xxx...)");
    openai_api_key_args.end = arg_end(1);

    const esp_console_cmd_t cmd = {
        .command = "openai_api",
        .help = "Set OpenAI API key for realtime communication",
        .hint = NULL,
        .func = &openai_api_key_set,
        .argtable = &openai_api_key_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

extern "C" int cmd_init(void)
{
    esp_log_level_set(TAG, ESP_LOG_DEBUG);

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = PROMPT_STR ">";
    repl_config.max_cmdline_length = 1024;

    register_cmd_wifi_sta();
    register_openai_api_key();
    register_cmd_reboot();

#if defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) || defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
    esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&hw_config, &repl_config, &repl));
#elif defined(CONFIG_ESP_CONSOLE_USB_CDC)
    esp_console_dev_usb_cdc_config_t hw_config = ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_cdc(&hw_config, &repl_config, &repl));
#elif defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
    esp_console_dev_usb_serial_jtag_config_t hw_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&hw_config, &repl_config, &repl));
#else
    ESP_LOGW(TAG, "No console interface configured");
    return ESP_OK;
#endif

    ESP_ERROR_CHECK(esp_console_start_repl(repl));

    // Check OpenAI API key configuration
#ifndef OPENAI_API_KEY
    size_t len = sizeof(g_openai_api_key_buf);
    memset(g_openai_api_key_buf, 0, sizeof(g_openai_api_key_buf));
    esp_err_t ret = storage_read(OPENAI_API_KEY_STORAGE, (void *)g_openai_api_key_buf, &len);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OpenAI API key loaded from storage");
    } else {
        ESP_LOGE(TAG, "Please set OpenAI API key and WiFi credentials via console commands, then reboot!");
        ESP_LOGE(TAG, "Commands: 'wifi_sta -s <ssid> -p <password>' and 'openai_api -k <api-key>'");
    }
#else
    ESP_LOGI(TAG, "Using compile-time OpenAI API key");
    memset(g_openai_api_key_buf, 0, sizeof(g_openai_api_key_buf));
    strncpy(g_openai_api_key_buf, OPENAI_API_KEY, sizeof(g_openai_api_key_buf) - 1);
#endif

    return ESP_OK;
}