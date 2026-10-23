file(GLOB _ai_skills_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)

add_library(ai_skills STATIC)
target_sources(ai_skills PRIVATE ${_ai_skills_srcs})
target_include_directories(ai_skills PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_link_libraries(ai_skills PRIVATE tuyaopen-headers libcjson core_system tuya_ai_service utility tuya_cloud_service)
# The music/story skill (skill_music_story.c) calls ai_audio playback interfaces:
# the ai_audio target exists only when ENABLE_COMP_AI_AUDIO is on (off by default, not linked).
if(TARGET ai_audio)
    target_link_libraries(ai_skills PRIVATE ai_audio)
endif()
unset(_ai_skills_srcs)
