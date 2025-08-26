idf_component_register(
    SRCS "smartbin_audio.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_codec_dev 78__esp-opus peer_wrapper esp_timer driver esp_lvgl_port esp_io_expander_pca95xx_16bit sscma_client sensecap-watcher
)