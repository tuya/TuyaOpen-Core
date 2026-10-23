# esp-idf component entry only: the component is registered from the repository
# root, both for a checkout pulled through idf_component.yml and for a local
# checkout added with EXTRA_COMPONENT_DIRS. Plain CMake hosts should
# include(tuyaopen-core.cmake) and must not use this file
# (idf_component_register is only available in esp-idf builds).
#
# REQUIRES mbedtls: the core modules link the bare name `mbedtls` in
# target_link_libraries; as an esp-idf component this must resolve to esp-idf's
# mbedtls component target, which requires that component to be processed first
# (an explicit dependency edge -- alphabetical processing order is not
# guaranteed). REQUIRES lwip: core_network.c includes the port's
# core_network_platform.h, which pulls in lwip/sockets.h. This root file is only
# used by esp-idf builds; host CMake builds include tuyaopen-core.cmake directly
# and are unaffected.
idf_component_register(SRCS "" INCLUDE_DIRS "" REQUIRES mbedtls lwip)

include(${CMAKE_CURRENT_LIST_DIR}/tuyaopen-core.cmake)

target_link_libraries(${COMPONENT_LIB} INTERFACE tuyaopen-core)

# The ESP32 port is a separate esp-idf component: port/esp-idf (esp-idf names
# components after their directories). It depends on this component through
# REQUIRES, which fixes the component processing/link order (the port must be
# scanned before its esp-idf dependencies such as bt/NimBLE); the application
# adds both directories with EXTRA_COMPONENT_DIRS.
