idf_component_register(
    SRCS "smartbin_http.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_client json nvs_flash smartbin_config smartbin_webserver smartbin_system mbedtls
)