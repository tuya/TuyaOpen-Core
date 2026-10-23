if(NOT CONFIG_ENABLE_LOCAL_STORE)
    return()
endif()

file(GLOB _local_store_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

add_library(local_store STATIC)
target_sources(local_store PRIVATE ${_local_store_srcs})
target_include_directories(local_store
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_options(local_store PRIVATE ${_p2p_warn_opts})
target_link_libraries(local_store PRIVATE tuyaopen-headers core_system)
unset(_local_store_srcs)
