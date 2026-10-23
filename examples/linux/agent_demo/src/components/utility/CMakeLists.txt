file(GLOB _ai_utility_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)

add_library(utility STATIC)
target_sources(utility PRIVATE ${_ai_utility_srcs})
target_include_directories(utility PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_link_libraries(utility PRIVATE tuyaopen-headers core_system)
unset(_ai_utility_srcs)
