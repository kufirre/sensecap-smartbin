# Wrapper component for libpeer to avoid modifying library source files
# This is the standard ESP-IDF approach for integrating third-party libraries

set(LIBPEER_PATH "../../deps/libpeer")

# Get all source files except peer_signaling.c (requires HTTP libs we don't have)
file(GLOB LIBPEER_SOURCES "${LIBPEER_PATH}/src/*.c")
list(REMOVE_ITEM LIBPEER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${LIBPEER_PATH}/src/peer_signaling.c")

idf_component_register(
    SRCS ${LIBPEER_SOURCES}
    INCLUDE_DIRS "${LIBPEER_PATH}/src"
    REQUIRES mbedtls srtp json esp_netif
)

# Override library configuration using compiler definitions (no source modification!)
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    # Core ESP32 configuration
    CONFIG_USE_LWIP=1
    CONFIG_USE_USRSCTP=0
    __BYTE_ORDER=__LITTLE_ENDIAN
    ESP32
    
    # Buffer sizes optimized for audio streaming
    CONFIG_AUDIO_BUFFER_SIZE=262144
    CONFIG_DATA_BUFFER_SIZE=204800
    
    # Disable components we don't need
    HTTP_DO_NOT_USE_CUSTOM_CONFIG
    MQTT_DO_NOT_USE_CUSTOM_CONFIG
    DISABLE_PEER_SIGNALING=1
    
    # Use ECDSA instead of RSA to prevent interrupt allocation error
    CONFIG_DTLS_USE_ECDSA=1
    
    # Disable features that cause issues
    KEEPALIVE_CONNCHECK=0
)

# Suppress warnings from third-party code
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=restrict
    -Wno-error=stringop-truncation
    -Wno-error=incompatible-pointer-types
    -Wno-unused-variable
    -Wno-sign-compare
)
