idf_component_register(
    SRCS "smartbin_board.c" "smartbin_filesystem.c" "smartbin_camera.c"
    INCLUDE_DIRS "include"
    REQUIRES sscma_client sensecap-watcher joltwallet__littlefs driver esp_io_expander_pca95xx_16bit esp_lvgl_port vfs esp_jpeg_simd
)