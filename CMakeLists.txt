cmake_minimum_required(VERSION 3.5)

# Ensure compatibility for subprojects with older cmake_minimum_required
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# Add compile definitions for OpenAI API
add_compile_definitions(OPENAI_REALTIMEAPI="https://api.openai.com/v1/realtime?model=gpt-4o-mini-realtime-preview-2024-12-17")

# Include app, peer, and only the SenseCAP SDK components we use (read-only)
set(EXTRA_COMPONENT_DIRS
  "src"
  "components/peer"
  "components/sensecap_sdk/components/sensecap-watcher"
  "components/sensecap_sdk/components/esp_lvgl_port"
  "components/sensecap_sdk/components/esp_io_expander_pca95xx_16bit"
  "components/sensecap_sdk/components/sscma_client"
)

# Minimal compiler options - only essential ones
add_compile_options(-fdiagnostics-color=always -Wno-error=format= -Wno-format)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sensecap-smartbin)