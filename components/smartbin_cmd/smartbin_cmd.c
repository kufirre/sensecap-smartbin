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
#include "smartbin_cmd.h"
#include "smartbin_webserver.h"
#include "smartbin_config.h"

static const char *TAG = "SMARTBIN_CMD";

#define PROMPT_STR "SenseCAP-SmartBin"

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
    
    if (openai_api_key_args.key->count) {
        const char* api_key = openai_api_key_args.key->sval[0];
        
        if (!api_key || strlen(api_key) == 0) {
            ESP_LOGE(TAG, "API key cannot be empty");
            return -1;
        }
        
        if (strlen(api_key) >= SMARTBIN_CONFIG_OPENAI_API_KEY_MAX_LEN) {
            ESP_LOGE(TAG, "API key too long (max %d chars)", SMARTBIN_CONFIG_OPENAI_API_KEY_MAX_LEN - 1);
            return -1;
        }

        ESP_LOGI(TAG, "Storing OpenAI API key securely...");
        esp_err_t ret = smartbin_config_set_openai_key(api_key);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to store API key: %s", esp_err_to_name(ret));
            return -1;
        }
        
        ESP_LOGI(TAG, "✅ API key stored successfully");
    } else {
        // Show current API key status
        if (smartbin_config_has_openai_key()) {
            ESP_LOGI(TAG, "OpenAI API Key: sk-**** (configured)");
        } else {
            ESP_LOGI(TAG, "OpenAI API Key: (not set)");
        }
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

/** config show command **/
static int config_show(int argc, char **argv)
{
    smartbin_config_t config = {0};
    esp_err_t ret = smartbin_webserver_get_config(&config);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Current Configuration:");
        ESP_LOGI(TAG, "  WiFi SSID: %s", config.wifi_ssid[0] ? config.wifi_ssid : "(not set)");
        ESP_LOGI(TAG, "  WiFi Password: %s", config.wifi_password[0] ? "****" : "(not set)");
        ESP_LOGI(TAG, "  OpenAI API Key: %s", config.openai_api_key[0] ? "sk-****" : "(not set)");
        ESP_LOGI(TAG, "  Post Code: %s", config.post_code[0] ? config.post_code : "(not set)");
        ESP_LOGI(TAG, "  Bin Color: %s", config.bin_color[0] ? config.bin_color : "(not set)");
    } else {
        ESP_LOGE(TAG, "No configuration found");
    }
    return 0;
}

static void register_config_show(void)
{
    const esp_console_cmd_t cmd = {
        .command = "config",
        .help = "Show current configuration",
        .hint = NULL,
        .func = &config_show,
        .argtable = NULL
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/** post code set command **/
static struct {
    struct arg_str *code;
    struct arg_end *end;
} postcode_args;

static int postcode_set(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &postcode_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, postcode_args.end, argv[0]);
        return 1;
    }
    
    smartbin_config_t config = {0};
    smartbin_webserver_get_config(&config);  // Get existing config
    
    if (postcode_args.code->count) {
        int len = strlen(postcode_args.code->sval[0]);
        if (len >= sizeof(config.post_code)) {
            ESP_LOGE(TAG, "Post code too long (max 15 bytes): %s", postcode_args.code->sval[0]);
            return -1;
        }
        strncpy(config.post_code, postcode_args.code->sval[0], sizeof(config.post_code) - 1);
        config.post_code[sizeof(config.post_code) - 1] = '\0';
        
        esp_err_t ret = smartbin_webserver_set_config(&config);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Post code set to: %s", config.post_code);
        } else {
            ESP_LOGE(TAG, "Failed to save post code");
        }
    } else {
        ESP_LOGI(TAG, "Current post code: %s", config.post_code[0] ? config.post_code : "(not set)");
    }
    return 0;
}

static void register_postcode(void)
{
    postcode_args.code = arg_str0("c", NULL, "<code>", "Post code (e.g., SW1A 1AA)");
    postcode_args.end = arg_end(1);

    const esp_console_cmd_t cmd = {
        .command = "postcode",
        .help = "Set or view post code",
        .hint = NULL,
        .func = &postcode_set,
        .argtable = &postcode_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/** bin color set command **/
static struct {
    struct arg_str *color;
    struct arg_end *end;
} bincolor_args;

static int bincolor_set(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &bincolor_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, bincolor_args.end, argv[0]);
        return 1;
    }
    
    smartbin_config_t config = {0};
    smartbin_webserver_get_config(&config);  // Get existing config
    
    if (bincolor_args.color->count) {
        const char* color = bincolor_args.color->sval[0];
        // Validate color
        if (strcmp(color, "red") != 0 && strcmp(color, "blue") != 0 && 
            strcmp(color, "green") != 0 && strcmp(color, "yellow") != 0 &&
            strcmp(color, "black") != 0 && strcmp(color, "grey") != 0 && 
            strcmp(color, "brown") != 0) {
            ESP_LOGE(TAG, "Invalid color. Valid colors: red, blue, green, yellow, black, grey, brown");
            return -1;
        }
        
        strncpy(config.bin_color, color, sizeof(config.bin_color) - 1);
        config.bin_color[sizeof(config.bin_color) - 1] = '\0';
        
        esp_err_t ret = smartbin_webserver_set_config(&config);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Bin color set to: %s", config.bin_color);
        } else {
            ESP_LOGE(TAG, "Failed to save bin color");
        }
    } else {
        ESP_LOGI(TAG, "Current bin color: %s", config.bin_color[0] ? config.bin_color : "(not set)");
        ESP_LOGI(TAG, "Valid colors: red, blue, green, yellow, black, grey, brown");
    }
    return 0;
}

static void register_bincolor(void)
{
    bincolor_args.color = arg_str0("c", NULL, "<color>", "Bin color (red, blue, green, yellow, black, grey, brown)");
    bincolor_args.end = arg_end(1);

    const esp_console_cmd_t cmd = {
        .command = "bincolor",
        .help = "Set or view bin color",
        .hint = NULL,
        .func = &bincolor_set,
        .argtable = &bincolor_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

int smartbin_cmd_init(void)
{
    esp_log_level_set(TAG, ESP_LOG_DEBUG);

    // Initialize secure configuration system
    esp_err_t ret = smartbin_config_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize secure config: %s", esp_err_to_name(ret));
        return -1;
    }

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = PROMPT_STR ">";
    repl_config.max_cmdline_length = 1024;

    register_cmd_wifi_sta();
    register_openai_api_key();
    register_postcode();
    register_bincolor();
    register_config_show();
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
    if (smartbin_config_has_openai_key()) {
        ESP_LOGI(TAG, "✅ OpenAI API key configured securely");
    } else {
        ESP_LOGW(TAG, "⚠️  OpenAI API key not configured");
        ESP_LOGI(TAG, "Use console commands to configure:");
        ESP_LOGI(TAG, "  wifi_sta -s <ssid> -p <password>");
        ESP_LOGI(TAG, "  openai_api -k <api-key>");
    }

    return ESP_OK;
}