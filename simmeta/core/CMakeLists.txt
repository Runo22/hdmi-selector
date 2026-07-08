# Module SDK: the plugin interface shared by the host and every plugin.

add_library(simmeta_core INTERFACE)
add_library(simmeta::core ALIAS simmeta_core)

target_include_directories(simmeta_core INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(simmeta_core INTERFACE
    Qt5::Core
    flecs::flecs)
