idf_component_register(
    SRCS "smartbin_webrtc.c"
    INCLUDE_DIRS "include"
    REQUIRES peer_wrapper smartbin_http smartbin_audio
)