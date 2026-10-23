file(GLOB _ai_agent_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)

add_library(ai_agent STATIC)
target_sources(ai_agent PRIVATE ${_ai_agent_srcs})
target_include_directories(ai_agent PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_link_libraries(ai_agent PRIVATE tuyaopen-headers libcjson core_system utility ai_skills tuya_cloud_service tuya_ai_service)
# Downlink TTS playback orchestration (ai_audio_play_tts_stream / ai_audio_player_stop):
# the ai_audio target exists only when ENABLE_COMP_AI_AUDIO is on (off by default, not linked).
if(TARGET ai_audio)
    target_link_libraries(ai_agent PRIVATE ai_audio)
endif()
unset(_ai_agent_srcs)
