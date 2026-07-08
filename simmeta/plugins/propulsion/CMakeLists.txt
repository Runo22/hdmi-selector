# Propulsion module: a dynamically loaded Qt plugin whose reflection
# boilerplate is produced by the SimMeta pipeline.

add_library(propulsion_module MODULE
    src/PropulsionModule.cpp
    src/PropulsionModule.hpp
    include/propulsion/TurbofanComponents.hpp)

set_target_properties(propulsion_module PROPERTIES
    AUTOMOC ON
    PREFIX ""                     # propulsion_module.so / .dll, no 'lib'
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/modules")

target_include_directories(propulsion_module PRIVATE include src)

target_link_libraries(propulsion_module PRIVATE
    simmeta::runtime
    simmeta::core
    Qt5::Core
    RTTR::Core          # must be the shared RTTR library (plugin registry)
    flecs::flecs)       # must be the shared flecs library (one world)

# The reflection pipeline. Each header listed here becomes one generated
# <stem>.simmeta.cpp compiled into this plugin. ANCHOR may be any ordinary
# source of this target; its compile_commands.json entry supplies include
# paths and defines to the parser.
simmeta_attach_codegen(
    TARGET       propulsion_module
    ANCHOR       "${CMAKE_CURRENT_SOURCE_DIR}/src/PropulsionModule.cpp"
    INCLUDE_BASE "${CMAKE_CURRENT_SOURCE_DIR}/include"
    HEADERS      "${CMAKE_CURRENT_SOURCE_DIR}/include/propulsion/TurbofanComponents.hpp")
