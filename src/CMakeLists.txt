idf_component_register(
  SRCS "main.cpp" "camera.cpp" "api.cpp" "wifi.cpp" "council.cpp" "cmd.cpp" "webrtc.cpp" "media.cpp" "http.cpp" "ui/ui.c" "ui/assets_dummy.c" "board.c"
  REQUIRES driver esp_wifi nvs_flash esp_http_client json console libpeer esp_codec_dev 78__esp-opus sensecap-watcher esp_lvgl_port esp_io_expander_pca95xx_16bit sscma_client
  INCLUDE_DIRS "." "ui"
)

# Apply minimal targeted compiler warnings for audio components
idf_component_get_property(lib libpeer COMPONENT_LIB)
if(lib)
  target_compile_options(${lib} PRIVATE -Wno-error=restrict -Wno-error=stringop-truncation)
endif()

idf_component_get_property(lib sepfy__srtp COMPONENT_LIB)
if(lib)
  target_compile_options(${lib} PRIVATE -Wno-error=incompatible-pointer-types -Wno-incompatible-pointer-types)
endif()

idf_component_get_property(lib 78__esp-opus COMPONENT_LIB)
if(lib)
  target_compile_options(${lib} PRIVATE -Wno-error=maybe-uninitialized -Wno-error=stringop-overread)
endif()

# Relax rlottie warnings from third-party to avoid build breaks on uninitialized detection
if(TARGET rlottie)
  target_compile_options(rlottie PRIVATE -Wno-error=uninitialized -Wno-error=maybe-uninitialized)
endif()