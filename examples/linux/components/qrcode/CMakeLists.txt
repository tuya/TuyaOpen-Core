if(NOT CONFIG_ENABLE_QRCODE)
    return()
endif()

file(GLOB _qrcode_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)

add_library(qrcode STATIC)
target_sources(qrcode PRIVATE ${_qrcode_srcs})
target_include_directories(qrcode PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_link_libraries(qrcode PRIVATE tuyaopen-headers)
unset(_qrcode_srcs)
