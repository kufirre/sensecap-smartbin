idf_component_register(
    SRCS "smartbin_cmd.c"
    INCLUDE_DIRS "include"
    REQUIRES console nvs_flash esp_wifi smartbin_webserver smartbin_config
)