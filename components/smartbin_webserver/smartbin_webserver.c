#include "smartbin_webserver.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_crc.h"

#include "smartbin_http.h"  // For smartbin_http_analyze_waste declaration

// Include ESP-IDF mDNS explicitly to avoid conflict with libpeer's mdns.h  
#include "mdns.h"

static const char *TAG = "SMARTBIN_WEBSERVER";
static httpd_handle_t s_server = NULL;
static bool s_mdns_started = false;

#define STORAGE_NAMESPACE "SenseCAP"
#define CONFIG_KEY        "smartbin_config"

/* Static asset management */
typedef struct {
    const char *uri;        // "/style.css"
    const char *path_gz;    // "/littlefs/style.css.gz"
    const char *ctype;      // "text/css; charset=utf-8"
    char etag[40];          // computed at boot: e.g., "crc32-deadbeef-12345"
    size_t size;            // gz file size
} static_asset_t;

static static_asset_t g_assets[] = {
    { "/",          "/littlefs/index.html.gz", "text/html; charset=utf-8", "", 0 },
    { "/index.html","/littlefs/index.html.gz", "text/html; charset=utf-8", "", 0 },
    { "/style.css", "/littlefs/style.css.gz",  "text/css; charset=utf-8",  "", 0 },
    { "/app.js",    "/littlefs/app.js.gz",     "application/javascript; charset=utf-8", "", 0 },
};

/* ========================== NVS config storage ========================== */

static esp_err_t config_write(const smartbin_config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(handle, CONFIG_KEY, config, sizeof(*config));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

static esp_err_t config_read(smartbin_config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    memset(config, 0, sizeof(*config));
    nvs_handle_t handle;
    esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;
    size_t sz = sizeof(*config);
    err = nvs_get_blob(handle, CONFIG_KEY, config, &sz);
    nvs_close(handle);
    return err;
}

/* ============================ Fallback HTML ============================= */

static const char *fallback_html =
"<!doctype html><html><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>SenseCAP SmartBin</title>"
"<style>body{font-family:system-ui,Segoe UI,Roboto,Helvetica,Arial;"
"background:#0f172a;color:#e5e7eb;display:grid;place-items:center;min-height:100vh;margin:0}"
".card{max-width:640px;padding:24px;border-radius:12px;background:#111827;border:1px solid #374151}"
"h1{margin:0 0 8px}code{background:#111827;border:1px solid #374151;padding:2px 6px;border-radius:6px}</style>"
"</head><body><div class='card'>"
"<h1>SmartBin UI not found</h1>"
"<p>Couldn't find <code>" WEB_FS_BASE_PATH WEB_FS_INDEX_PATH "</code> on the device filesystem.</p>"
"<p>Make sure your <code>/web</code> folder was packed into the LittleFS partition "
"(label <code>storage</code>) and flashed. Then reload this page.</p>"
"</div></body></html>";

/* ============================== Utilities ============================== */

static esp_err_t send_file_chunked(httpd_req_t *req, FILE *f) {
    char buf[1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) {
            httpd_resp_sendstr_chunk(req, NULL);
            return ESP_FAIL;
        }
    }
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

/* ========================== Static Asset Management ========================== */

// Compute CRC32 of a file (gz bytes), store size and a stable ETag string.
static bool compute_etag_and_size(const char *path, char out_etag[40], size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint32_t crc = 0;
    size_t total = 0;
    uint8_t buf[2048];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        crc   = esp_crc32_le(crc, buf, n);
        total += n;
    }
    fclose(f);
    if (out_size) *out_size = total;
    // Quote the ETag per RFC. Include size so the tag changes if only size changes.
    snprintf(out_etag, 40, "\"crc32-%08x-%u\"", crc, (unsigned)total);
    return true;
}

static void set_static_cache_headers(httpd_req_t *req, bool is_html) {
    if (is_html) {
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    } else {
        httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=31536000, immutable");
    }
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Vary", "Accept-Encoding");
}

// Honor If-None-Match against our precomputed ETag; return true if we replied 304.
static bool maybe_send_304(httpd_req_t *req, const char *etag) {
    if (!etag || !etag[0]) return false;
    
    size_t len = httpd_req_get_hdr_value_len(req, "If-None-Match");
    if (len > 0 && len < 128) {
        char buf[128];
        if (httpd_req_get_hdr_value_str(req, "If-None-Match", buf, sizeof(buf)) == ESP_OK) {
            if (strcmp(buf, etag) == 0) {
                httpd_resp_set_status(req, "304 Not Modified");
                httpd_resp_set_hdr(req, "ETag", etag);
                httpd_resp_send(req, NULL, 0);
                return true;
            }
        }
    }
    return false;
}

static esp_err_t send_file_gz(httpd_req_t *req, const static_asset_t *a, bool is_html) {
    // ETag / 304
    if (maybe_send_304(req, a->etag)) return ESP_OK;

    FILE *f = fopen(a->path_gz, "rb");
    if (!f) return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");

    httpd_resp_set_type(req, a->ctype);
    set_static_cache_headers(req, is_html);
    if (a->etag[0]) httpd_resp_set_hdr(req, "ETag", a->etag);

    esp_err_t result = send_file_chunked(req, f);
    fclose(f);
    return result;
}

static const static_asset_t* find_asset(const char *uri) {
    for (size_t i = 0; i < sizeof(g_assets)/sizeof(g_assets[0]); ++i) {
        if (strcmp(uri, g_assets[i].uri) == 0) return &g_assets[i];
    }
    return NULL;
}

static const char *mime_from_path(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return "text/plain";
    if (!strcmp(ext, ".html") || !strcmp(ext, ".htm")) return "text/html";
    if (!strcmp(ext, ".css"))  return "text/css";
    if (!strcmp(ext, ".js"))   return "text/javascript";
    if (!strcmp(ext, ".svg"))  return "image/svg+xml";
    if (!strcmp(ext, ".png"))  return "image/png";
    if (!strcmp(ext, ".jpg") || !strcmp(ext, ".jpeg")) return "image/jpeg";
    if (!strcmp(ext, ".gif"))  return "image/gif";
    if (!strcmp(ext, ".ico"))  return "image/x-icon";
    if (!strcmp(ext, ".json")) return "application/json";
    if (!strcmp(ext, ".gz"))   return "application/octet-stream";
    return "application/octet-stream";
}

static bool file_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0) && S_ISREG(st.st_mode);
}

/* ============================== API: /api/config ============================== */

static esp_err_t config_get_handler(httpd_req_t *req)
{
    smartbin_config_t cfg = {0};
    (void)config_read(&cfg); // if not present, we just return blanks

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "wifi_ssid", cfg.wifi_ssid);
    cJSON_AddStringToObject(root, "post_code", cfg.post_code);
    cJSON_AddStringToObject(root, "bin_color", cfg.bin_color);

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json ? json : "{}");

    if (json) free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t config_post_handler(httpd_req_t *req)
{
    const int total = req->content_len;
    if (total <= 0 || total > 2048) { // safety limit
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body");
        return ESP_OK;
    }

    char *body = (char *)malloc(total + 1);
    if (!body) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM"); return ESP_OK; }

    int recvd = 0;
    while (recvd < total) {
        int r = httpd_req_recv(req, body + recvd, total - recvd);
        if (r <= 0) { free(body); httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Recv error"); return ESP_OK; }
        recvd += r;
    }
    body[recvd] = 0;

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON"); return ESP_OK; }

    // Load existing configuration first to preserve current values
    smartbin_config_t cfg = {0};
    config_read(&cfg); // Load existing config, ignore errors if none exists

    // Only update fields that are provided and non-empty
    cJSON *v;
    if ((v = cJSON_GetObjectItem(root, "wifi_ssid")) && cJSON_IsString(v) && strlen(v->valuestring) > 0) {
        strncpy(cfg.wifi_ssid, v->valuestring, sizeof(cfg.wifi_ssid) - 1);
    }
    if ((v = cJSON_GetObjectItem(root, "wifi_password")) && cJSON_IsString(v) && strlen(v->valuestring) > 0) {
        strncpy(cfg.wifi_password, v->valuestring, sizeof(cfg.wifi_password) - 1);
    }
    if ((v = cJSON_GetObjectItem(root, "openai_api_key")) && cJSON_IsString(v) && strlen(v->valuestring) > 0) {
        strncpy(cfg.openai_api_key, v->valuestring, sizeof(cfg.openai_api_key) - 1);
    }
    if ((v = cJSON_GetObjectItem(root, "post_code")) && cJSON_IsString(v) && strlen(v->valuestring) > 0) {
        strncpy(cfg.post_code, v->valuestring, sizeof(cfg.post_code) - 1);
    }
    if ((v = cJSON_GetObjectItem(root, "bin_color")) && cJSON_IsString(v) && strlen(v->valuestring) > 0) {
        strncpy(cfg.bin_color, v->valuestring, sizeof(cfg.bin_color) - 1);
    }

    cJSON_Delete(root);

    esp_err_t err = config_write(&cfg);

    cJSON *resp = cJSON_CreateObject();
    if (err == ESP_OK) {
        cJSON_AddStringToObject(resp, "status", "success");
        cJSON_AddStringToObject(resp, "message", "Configuration saved");
    } else {
        cJSON_AddStringToObject(resp, "status", "error");
        cJSON_AddStringToObject(resp, "message", "Failed to save");
    }
    char *out = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, out ? out : "{\"status\":\"error\"}");
    if (out) free(out);
    cJSON_Delete(resp);

    // Switch modes after saving.
    if (err == ESP_OK && cfg.wifi_ssid[0] != '\0') {
        // let the response flush
        vTaskDelay(pdMS_TO_TICKS(1000));
        // WiFi mode switching handled by main application
    }

    return ESP_OK;
}

/* ============================== API: /api/test-analysis ============================== */
/* Test endpoint for multimodal waste analysis */

static esp_err_t test_analysis_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "🧪 Test analysis endpoint called");
    
    cJSON *resp = cJSON_CreateObject();
    
    // Get current config for postcode
    smartbin_config_t cfg = {0};
    esp_err_t err = smartbin_webserver_get_config(&cfg);
    
    if (err != ESP_OK || cfg.post_code[0] == '\0') {
        cJSON_AddStringToObject(resp, "status", "error");
        cJSON_AddStringToObject(resp, "message", "No postcode configured. Please set postcode in config first.");
        
        char *out = cJSON_PrintUnformatted(resp);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, out);
        
        if (out) free(out);
        cJSON_Delete(resp);
        return ESP_OK;
    }
    
    // Call the multimodal API test function
    uint8_t mock_image[] = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x01, 0x01};
    size_t mock_len = sizeof(mock_image);
    
    uint8_t* audio_data = NULL;
    size_t audio_len = 0;
    
    ESP_LOGI(TAG, "🔍 Testing multimodal analysis with postcode: %s", cfg.post_code);
    esp_err_t api_result = smartbin_http_analyze_waste(mock_image, mock_len, cfg.post_code, &audio_data, &audio_len);
    
    if (api_result == ESP_OK) {
        cJSON_AddStringToObject(resp, "status", "success");
        cJSON_AddStringToObject(resp, "message", "Multimodal analysis completed successfully");
        cJSON_AddStringToObject(resp, "postcode", cfg.post_code);
        cJSON_AddNumberToObject(resp, "audio_bytes", audio_len);
        
        if (audio_data) {
            // Clean up audio data
            free(audio_data);
        }
    } else {
        cJSON_AddStringToObject(resp, "status", "error");
        cJSON_AddStringToObject(resp, "message", "Multimodal analysis failed");
        cJSON_AddStringToObject(resp, "error_code", esp_err_to_name(api_result));
    }
    
    char *out = cJSON_PrintUnformatted(resp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, out);
    
    if (out) free(out);
    cJSON_Delete(resp);
    
    ESP_LOGI(TAG, "✅ Test analysis response sent");
    return ESP_OK;
}

/* ============================== API: /api/scan ============================== */
/* Scans ONLY when called. It does not run in the background. */

static const char *authmode_to_str(wifi_auth_mode_t m) {
    switch (m) {
        case WIFI_AUTH_OPEN:            return "OPEN";
        case WIFI_AUTH_WEP:             return "WEP";
        case WIFI_AUTH_WPA_PSK:         return "WPA-PSK";
        case WIFI_AUTH_WPA2_PSK:        return "WPA2-PSK";
        case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/WPA2-PSK";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-ENT";
        case WIFI_AUTH_WPA3_PSK:        return "WPA3-PSK";
        case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/WPA3-PSK";
        case WIFI_AUTH_WAPI_PSK:        return "WAPI-PSK";
        case WIFI_AUTH_OWE:             return "OWE";
        default:                        return "SECURED";
    }
}

static esp_err_t wifi_scan_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "WiFi scan requested");

    // Ensure Wi-Fi is initialized and started in a mode that can scan.
    bool did_init = false, did_start = false, mode_changed = false;
    wifi_mode_t prev_mode = WIFI_MODE_NULL;

    esp_err_t rc = esp_wifi_get_mode(&prev_mode);
    ESP_LOGI(TAG, "Current WiFi mode: %d, get_mode result: %s", prev_mode, esp_err_to_name(rc));
    
    if (rc == ESP_ERR_WIFI_NOT_INIT) {
        ESP_LOGI(TAG, "WiFi not initialized, initializing...");
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_init());
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_loop_create_default());
        wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
        if (esp_wifi_init(&wcfg) == ESP_OK) did_init = true;
        prev_mode = WIFI_MODE_NULL;
    }

    if (prev_mode == WIFI_MODE_AP) {
        ESP_LOGI(TAG, "Switching from AP to APSTA mode for scanning");
        rc = esp_wifi_set_mode(WIFI_MODE_APSTA);
        ESP_LOGI(TAG, "Mode switch result: %s", esp_err_to_name(rc));
        mode_changed = true;
    } else if (prev_mode != WIFI_MODE_STA && prev_mode != WIFI_MODE_APSTA) {
        ESP_LOGI(TAG, "Setting WiFi mode to STA for scanning");
        rc = esp_wifi_set_mode(WIFI_MODE_STA);
        ESP_LOGI(TAG, "Mode switch result: %s", esp_err_to_name(rc));
        mode_changed = true;
    }

    // Don't try to start WiFi if it's already running (we're serving this request!)
    if (prev_mode == WIFI_MODE_NULL) {
        rc = esp_wifi_start();
        if (rc == ESP_OK) did_start = true;
        else if (rc == ESP_ERR_WIFI_STATE) rc = ESP_OK; // already started
        ESP_LOGI(TAG, "WiFi start result: %s", esp_err_to_name(rc));
    }

    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "WiFi preparation failed: %s", esp_err_to_name(rc));
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"networks\":[],\"count\":0,\"error\":\"WiFi setup failed\"}");
        goto restore;
    }

    wifi_scan_config_t scan = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active = { .min = 100, .max = 300 }
    };

    ESP_LOGI(TAG, "Starting WiFi scan...");
    rc = esp_wifi_scan_start(&scan, true);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "scan_start error: %s", esp_err_to_name(rc));
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"networks\":[],\"count\":0,\"error\":\"scan failed\"}");
        goto restore;
    }

    uint16_t num = 0;
    rc = esp_wifi_scan_get_ap_num(&num);
    ESP_LOGI(TAG, "Scan completed. Found %d networks, get_ap_num result: %s", num, esp_err_to_name(rc));
    
    if (rc != ESP_OK || num == 0) {
        ESP_LOGW(TAG, "No networks found or error getting AP count");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"networks\":[],\"count\":0}");
        goto restore;
    }

    if (num > 32) num = 32; // cap

    wifi_ap_record_t *aps = (wifi_ap_record_t *)calloc(num, sizeof(*aps));
    if (!aps) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        goto restore;
    }
    esp_wifi_scan_get_ap_records(&num, aps);

    // Build JSON (streamed)
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr_chunk(req, "{\"networks\":[");
    
    uint16_t valid_count = 0;
    for (uint16_t i = 0; i < num; ++i) {
        // Skip empty SSIDs
        if (aps[i].ssid[0] == '\0') continue;

        // JSON-escape SSID (basic)
        char esc[128]; size_t w = 0;
        for (const unsigned char *p = aps[i].ssid; *p && w < sizeof(esc) - 1; ++p) {
            unsigned char c = *p;
            if (c == '\"' || c == '\\') { 
                if (w + 2 >= sizeof(esc)) break; // Need space for \\ and char
                esc[w++]='\\'; esc[w++]=c; 
            }
            else if (c < 0x20) { 
                if (w + 6 >= sizeof(esc)) break; // Need space for \\uXXXX
                int n = snprintf(esc+w, sizeof(esc)-w, "\\u%04x", c);
                if (n > 0 && w + n < sizeof(esc)) w += n;
                else break; // Prevent buffer overflow
            }
            else {
                if (w >= sizeof(esc) - 1) break; // Prevent overflow
                esc[w++]=c;
            }
        }
        esc[w]=0;

        char item[256];
        int n = snprintf(item, sizeof(item),
                         "%s{\"ssid\":\"%s\",\"rssi\":%d,\"authmode\":\"%s\"}",
                         (valid_count ? "," : ""), esc, (int)aps[i].rssi, authmode_to_str(aps[i].authmode));
        if (n < 0 || n >= (int)sizeof(item)) item[sizeof(item)-1] = 0;
        httpd_resp_sendstr_chunk(req, item);
        valid_count++;
    }
    
    char footer[64];
    snprintf(footer, sizeof(footer), "],\"count\":%d}", valid_count);
    httpd_resp_sendstr_chunk(req, footer);
    httpd_resp_sendstr_chunk(req, NULL);
    free(aps);

restore:
    // Restore previous mode if we changed it; optionally stop/deinit if we started/initialized.
    if (mode_changed) {
        if (prev_mode != WIFI_MODE_NULL)
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_mode(prev_mode));
    }
    if (did_start)  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_stop());
    if (did_init)   ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_deinit());
    return ESP_OK;
}

/* ============================== Static files ============================== */

static esp_err_t static_handler(httpd_req_t *req)
{
    // First try to serve from pre-defined gzipped assets
    const static_asset_t *asset = find_asset(req->uri);
    if (asset) {
        bool is_html = strstr(asset->ctype, "text/html") != NULL;
        return send_file_gz(req, asset, is_html);
    }
    
    // Fallback to original logic for other files
    char path[256];
    const char *uri = req->uri;
    if (strcmp(uri, "/") == 0) uri = WEB_FS_INDEX_PATH;

    int n = snprintf(path, sizeof(path), "%s%s", WEB_FS_BASE_PATH, uri);
    if (n <= 0 || n >= (int)sizeof(path)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Path too long");
        return ESP_OK;
    }

#if WEB_TRY_GZIP
    char gzpath[256];
    bool used_gzip = false;
    int m = snprintf(gzpath, sizeof(gzpath), "%s.gz", path);
    if (m > 0 && m < (int)sizeof(gzpath) && file_exists(gzpath)) {
        strcpy(path, gzpath);
        used_gzip = true;
    }
#endif

    if (!file_exists(path)) {
        // Fallback to minimal page if index is missing
        if (strcmp(uri, WEB_FS_INDEX_PATH) == 0) {
            httpd_resp_set_type(req, "text/html");
            httpd_resp_sendstr(req, fallback_html);
            return ESP_OK;
        }
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
        return ESP_OK;
    }

    // Set MIME type based on original URI, not gzipped path
#if WEB_TRY_GZIP
    const char *mime_path = used_gzip ? req->uri : path;
    if (strcmp(req->uri, "/") == 0) mime_path = WEB_FS_INDEX_PATH;
#else
    const char *mime_path = path;
#endif
    httpd_resp_set_type(req, mime_from_path(mime_path));
#if WEB_TRY_GZIP
    if (used_gzip) httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
#endif
#if WEB_CACHE_STATIC
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=31536000, immutable");
#endif

    FILE *f = fopen(path, "rb");
    if (!f) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Open failed"); return ESP_OK; }

    esp_err_t rc = send_file_chunked(req, f);
    fclose(f);
    return rc;
}

/* ============================= Captive portal ============================= */

static esp_err_t favicon_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t captive_redirect(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* ============================== Public API =============================== */

esp_err_t smartbin_webserver_init(void)
{
    if (s_server) {
        ESP_LOGW(TAG, "Web server already running");
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.stack_size = 8192;  // Increase from default 4096 to handle WiFi scanning

    esp_err_t rc = httpd_start(&s_server, &config);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(rc));
        return ESP_FAIL;
    }

    // API first
    httpd_uri_t cfg_get  = { .uri="/api/config", .method=HTTP_GET,  .handler=config_get_handler };
    httpd_uri_t cfg_post = { .uri="/api/config", .method=HTTP_POST, .handler=config_post_handler };
    httpd_uri_t scan_get = { .uri="/api/scan",   .method=HTTP_GET,  .handler=wifi_scan_handler  };
    httpd_uri_t test_get = { .uri="/api/test-analysis", .method=HTTP_GET, .handler=test_analysis_handler };
    httpd_register_uri_handler(s_server, &cfg_get);
    httpd_register_uri_handler(s_server, &cfg_post);
    httpd_register_uri_handler(s_server, &scan_get);
    httpd_register_uri_handler(s_server, &test_get);

    // Favicon + captive portal endpoints
    httpd_uri_t favicon        = { .uri="/favicon.ico",       .method=HTTP_GET, .handler=favicon_handler };
    httpd_uri_t captive_204    = { .uri="/generate_204",      .method=HTTP_GET, .handler=captive_redirect };
    httpd_uri_t captive_google = { .uri="/redirect",          .method=HTTP_GET, .handler=captive_redirect };
    httpd_uri_t captive_ios    = { .uri="/hotspot-detect.html", .method=HTTP_GET, .handler=captive_redirect };
    httpd_register_uri_handler(s_server, &favicon);
    httpd_register_uri_handler(s_server, &captive_204);
    httpd_register_uri_handler(s_server, &captive_google);
    httpd_register_uri_handler(s_server, &captive_ios);

    // Catch-all static (must be last)
    httpd_uri_t static_all = { .uri="/*", .method=HTTP_GET, .handler=static_handler };
    httpd_register_uri_handler(s_server, &static_all);

    // Initialize static assets with ETag computation
    smartbin_webserver_init_static_assets();
    
    ESP_LOGI(TAG, "Web server started. Serving %s%s", WEB_FS_BASE_PATH, WEB_FS_INDEX_PATH);
    return ESP_OK;
}

esp_err_t smartbin_webserver_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
        ESP_LOGI(TAG, "Web server stopped");
    }
    
    // Stop mDNS when webserver stops
    smartbin_webserver_stop_mdns();
    
    return ESP_OK;
}

esp_err_t smartbin_webserver_get_config(smartbin_config_t *config)
{
    return config_read(config);
}

esp_err_t smartbin_webserver_set_config(const smartbin_config_t *config)
{
    return config_write(config);
}

esp_err_t smartbin_webserver_init_static_assets(void)
{
    ESP_LOGI(TAG, "Initializing static assets with ETag computation...");
    for (size_t i = 0; i < sizeof(g_assets)/sizeof(g_assets[0]); ++i) {
        if (!compute_etag_and_size(g_assets[i].path_gz, g_assets[i].etag, &g_assets[i].size)) {
            ESP_LOGW(TAG, "Asset missing: %s", g_assets[i].path_gz);
            // Leave ETag empty; server will still try to open and send.
        } else {
            ESP_LOGI(TAG, "Asset loaded: %s [%s, %zu bytes]", g_assets[i].uri, g_assets[i].etag, g_assets[i].size);
        }
    }
    return ESP_OK;
}

esp_err_t smartbin_webserver_start_mdns(void)
{
    if (s_mdns_started) {
        ESP_LOGW(TAG, "mDNS already started");
        return ESP_OK;
    }
    
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mDNS init failed: %s", esp_err_to_name(err));
        return err;
    }
    
    err = mdns_hostname_set("smartbin");
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mDNS hostname set failed: %s", esp_err_to_name(err));
        mdns_free();
        return err;
    }
    
    err = mdns_instance_name_set("SmartBin Configuration Portal");
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mDNS instance name set failed: %s", esp_err_to_name(err));
        mdns_free();
        return err;
    }
    
    err = mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mDNS service add failed: %s", esp_err_to_name(err));
        mdns_free();
        return err;
    }
    
    s_mdns_started = true;
    ESP_LOGI(TAG, "mDNS started - device accessible at smartbin.local");
    return ESP_OK;
}

esp_err_t smartbin_webserver_stop_mdns(void)
{
    if (!s_mdns_started) {
        return ESP_OK;
    }
    
    mdns_free();
    s_mdns_started = false;
    ESP_LOGI(TAG, "mDNS stopped");
    return ESP_OK;
}
