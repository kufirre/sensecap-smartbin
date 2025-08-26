idf_component_register(SRCS "smartbin_button.c"
                       INCLUDE_DIRS "include" "../../src/ui"
                       REQUIRES freertos esp_common sensecap-watcher smartbin_http smartbin_webserver smartbin_system)