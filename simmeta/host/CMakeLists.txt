# Example host: loads simulation modules and exercises the reflected data.

add_executable(simmeta_host
    src/main.cpp)

target_link_libraries(simmeta_host PRIVATE
    simmeta::runtime
    simmeta::core
    Qt5::Core
    RTTR::Core
    flecs::flecs)

if(SIMMETA_BUILD_PLUGINS)
    # Build order only; the host discovers modules at runtime by path.
    add_dependencies(simmeta_host propulsion_module)
endif()
