# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS 
    ${CMAKE_CURRENT_LIST_DIR}/components/sensecap_sdk/components
    ${CMAKE_CURRENT_LIST_DIR}/components/sensecap_sdk/examples/openai-realtime/components/srtp
    ${CMAKE_CURRENT_LIST_DIR}/components/sensecap_sdk/examples/openai-realtime/components/peer
    ${CMAKE_CURRENT_LIST_DIR}/components/sensecap_sdk/examples/openai-realtime/components/esp-libopus
    ${CMAKE_CURRENT_LIST_DIR}/components/sensecap_sdk/examples/openai-realtime/components/esp-protocols/components)

add_compile_options(-fdiagnostics-color=always -Wno-error=format= -Wno-format)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sensecap-smartbin)