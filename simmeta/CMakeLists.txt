cmake_minimum_required(VERSION 3.21)

project(SimMeta
    VERSION 0.1.0
    DESCRIPTION "Code-generation reflection pipeline (RTTR + Flecs) for plugin-based simulators"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Mandatory for the reflection pipeline: metagen.py resolves include paths
# and defines from the compilation database instead of hardcoding them.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Plugins and the host must agree on one RTTR registry and one set of Flecs
# component ids, which requires shared runtime libraries (see README,
# "Shared-library discipline").
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(SimMetaCodegen)

option(SIMMETA_BUILD_PLUGINS "Build the example simulation plugins" ON)
option(SIMMETA_BUILD_HOST "Build the example host application" ON)

add_subdirectory(runtime)

if(SIMMETA_BUILD_PLUGINS OR SIMMETA_BUILD_HOST)
    find_package(Qt5 5.15 REQUIRED COMPONENTS Core)
    find_package(RTTR CONFIG REQUIRED Core)
    find_package(flecs CONFIG REQUIRED)
    add_subdirectory(core)
endif()

if(SIMMETA_BUILD_PLUGINS)
    add_subdirectory(plugins/propulsion)
endif()

if(SIMMETA_BUILD_HOST)
    add_subdirectory(host)
endif()
