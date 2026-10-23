# AI audio orchestration (playback side).
# Contains only ai_audio_player.c: TTS stream / music playback orchestration,
# all on top of the svc_ai_player interface (audio_player component); platform
# audio output is injected by the port through tuya_ai_player_set_consumer().
# The demo uplink goes through the port contract.
if(NOT CONFIG_ENABLE_COMP_AI_AUDIO)
    return()
endif()

# ai_audio uses the svc_ai_player interface, so the player must be enabled too
# (Kconfig does ENABLE_COMP_AI_AUDIO select ENABLE_AI_PLAYER; this covers the
# direct -D configuration path).
if(NOT CONFIG_ENABLE_AI_PLAYER)
    message(FATAL_ERROR
        "CONFIG_ENABLE_COMP_AI_AUDIO=ON but CONFIG_ENABLE_AI_PLAYER=OFF: "
        "ai_audio depends on the audio_player svc_ai_player interface; please "
        "enable ENABLE_AI_PLAYER as well (see src/components/audio_player/Kconfig).")
endif()

file(GLOB _ai_audio_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)

add_library(ai_audio STATIC)
target_sources(ai_audio PRIVATE ${_ai_audio_srcs})
target_include_directories(ai_audio PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
# audio_player is linked PUBLIC: svc_ai_player.h appears in ai_audio_player.h's
# public interface (AI_AUDIO_CODEC_E / AI_PLAYER_* types), so consumers
# (ai_skills/ai_agent) get its include path through this target. core_api:
# core_system; ai_user_event.h: utility.
target_link_libraries(ai_audio PUBLIC audio_player)
target_link_libraries(ai_audio PRIVATE tuyaopen-headers core_system utility)
unset(_ai_audio_srcs)

# Alert source (Kconfig choice, default LOCAL): when going through Kconfig/.config
# the switch mapping in tuyaopen-core.cmake injects the same-named macro; for
# direct -D configuration (neither CONFIG_ set) fall back to the Kconfig default
# LOCAL so that ai_audio_player_alert() does not silently do nothing.
if(NOT CONFIG_AI_PLAYER_ALERT_SOURCE_LOCAL AND NOT CONFIG_AI_PLAYER_ALERT_SOURCE_CUSTOM)
    target_compile_definitions(ai_audio PRIVATE AI_PLAYER_ALERT_SOURCE_LOCAL=1)
endif()
