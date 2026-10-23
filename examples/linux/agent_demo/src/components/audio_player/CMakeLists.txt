# AI player (platform-independent core).
# Platform audio output is injected by the port through AI_PLAYER_CONSUMER_T
# (empty implementation by default in core), so this module has zero tkl/tdl/tdd
# dependencies; the URL source uses the tuya_transport HTTP session and the
# file source uses core_fs (the platform must provide the core_fs_ops card, see
# datasink_file.c).
if(NOT CONFIG_ENABLE_AI_PLAYER)
    return()
endif()

# Decoders: MP3 (minimp3) + WAV. OPUS / OGGOPUS are not implemented (this tree
# has no libopus / libogg or the corresponding decoder implementations) and
# error out when enabled, avoiding a silent half-finished configuration
# (both switches default to n in Kconfig).
if(CONFIG_ENABLE_AI_PLAYER_DECODER_OPUS OR CONFIG_ENABLE_AI_PLAYER_DECODER_OGGOPUS)
    message(FATAL_ERROR
        "CONFIG_ENABLE_AI_PLAYER_DECODER_OPUS / OGGOPUS is enabled, but the "
        "OPUS/OGGOPUS decoder implementations are not available: please "
        "disable these two switches (see Decoder Options in "
        "src/components/audio_player/Kconfig).")
endif()

# The file source (datasink_file.c) reads files through core_fs and needs the
# platform to provide the core_fs_ops card. The linux port filesystem adaptation
# file is currently gated by CONFIG_ENABLE_TUYA_P2P: with P2P off this module
# still references core_fopen and core_fs_ops_platform is missing at link time
# (the port gate is to be relaxed there; projects not using AI_PLAYER_SRC_FILE
# can ignore this warning).
if(NOT CONFIG_ENABLE_TUYA_P2P)
    message(WARNING "audio_player: CONFIG_ENABLE_TUYA_P2P=OFF, the platform filesystem adaptation (core_fs_ops card) is not built — the file source will lack core_fs_ops_platform at link time")
endif()

file(GLOB _audio_player_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/consumer/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/datasink/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/decoder/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/mixer/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/resample/*.c"
)

add_library(audio_player STATIC)
target_sources(audio_player PRIVATE ${_audio_player_srcs})
target_include_directories(audio_player PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
# tuya_ai_service: base_event.h (ty_publish_event publishes playback state events)
# tuya_transport: http_session.h (URL source) + core_security.h reached through core_api.h
target_link_libraries(audio_player PRIVATE
    tuyaopen-headers
    core_system
    tuya_ai_service
    tuya_transport
)

# Resource parameters: values from .config (or host -D) are injected as compile
# macros; svc_ai_player.h has same-named #ifndef fallbacks (matching Kconfig),
# so behavior is unchanged when not configured.
foreach(_ap_param IN ITEMS
    AI_PLAYER_STACK_SIZE
    AI_PLAYER_FRAMEBUF_SIZE
    AI_PLAYER_DECODEBUF_SIZE
    AI_PLAYER_RINGBUF_SIZE
    AI_PLAYER_HTTP_YIELD_MS
    AI_PLAYER_HTTP_TIMEOUT_S
)
    if(DEFINED CONFIG_${_ap_param} AND NOT CONFIG_${_ap_param} STREQUAL "")
        target_compile_definitions(audio_player PRIVATE ${_ap_param}=${CONFIG_${_ap_param}})
    endif()
endforeach()
unset(_ap_param)

unset(_audio_player_srcs)
