idf_component_register(
    SRCS "smartbin_wifi.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_netif esp_event smartbin_webserver
)