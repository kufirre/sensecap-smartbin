idf_component_register(
    SRCS "smartbin_webserver.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server json nvs_flash esp_wifi esp_netif mdns smartbin_http
)