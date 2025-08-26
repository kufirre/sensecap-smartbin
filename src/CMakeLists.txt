idf_component_register(
  SRCS "main.cpp" "ui/ui.c" "ui/listening_A.c" "ui/listening_B.c" "ui/listening_C.c" "ui/listening_D.c" "ui/listening_E.c" "ui/speaking_A.c" "ui/speaking_B.c" "ui/speaking_C.c" "ui/speaking_D.c" "ui/speaking_E.c"
  REQUIRES driver esp_wifi nvs_flash esp_http_client esp_http_server json console peer_wrapper esp_codec_dev 78__esp-opus sensecap-watcher esp_lvgl_port esp_io_expander_pca95xx_16bit sscma_client joltwallet__littlefs mdns smartbin_config smartbin_audio smartbin_http smartbin_webrtc smartbin_webserver smartbin_council smartbin_system smartbin_wifi smartbin_cmd smartbin_button
  INCLUDE_DIRS "." "ui"
)

# Apply minimal targeted compiler warnings for third-party audio components
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

# Build LittleFS image with web assets using ESP-IDF's littlefs_create_partition_image
set(WEB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/web")

if(EXISTS "${WEB_DIR}")
  # Remove system files that might cause issues
  file(REMOVE "${WEB_DIR}/.DS_Store")
  
  # Get list of web files to track dependencies
  file(GLOB_RECURSE WEB_FILES "${WEB_DIR}/*")
  
  # Use ESP-IDF's littlefs_create_partition_image function
  littlefs_create_partition_image(storage ${WEB_DIR} FLASH_IN_PROJECT)
  
  message(STATUS "LittleFS partition 'storage' will be created from: ${WEB_DIR}")
  message(STATUS "Files will be automatically flashed with: idf.py flash")
else()
  message(WARNING "Web directory not found: ${WEB_DIR}")
  message(WARNING "LittleFS partition will be empty - create ${WEB_DIR} with your web assets")
endif()