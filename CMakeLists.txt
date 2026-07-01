cmake_minimum_required(VERSION 3.16)
project(hdmi_selector LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(HDMI_BUILD_GUI "Build the wxWidgets desktop GUI" ON)
option(HDMI_BUILD_TESTS "Build the unit tests" ON)

find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Core library: platform-independent logic + REST server + backends.
# ---------------------------------------------------------------------------
set(CORE_SOURCES
    src/core/DisplayManager.cpp
    src/core/RestServer.cpp
    src/backends/MockBackend.cpp
    src/backends/backend_factory.cpp
    src/platform/Autostart.cpp
)
if(WIN32)
    list(APPEND CORE_SOURCES src/backends/WindowsBackend.cpp)
endif()

add_library(hdmi_core STATIC ${CORE_SOURCES})
target_include_directories(hdmi_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(hdmi_core PUBLIC Threads::Threads)
if(WIN32)
    # CCD display APIs live in user32/gdi32; ws2_32 for httplib sockets.
    target_link_libraries(hdmi_core PUBLIC user32 gdi32 ws2_32)
endif()

# ---------------------------------------------------------------------------
# Headless server executable (works on every platform).
# ---------------------------------------------------------------------------
add_executable(hdmi_server src/server_main.cpp)
target_link_libraries(hdmi_server PRIVATE hdmi_core)

# ---------------------------------------------------------------------------
# Unit tests.
# ---------------------------------------------------------------------------
if(HDMI_BUILD_TESTS)
    enable_testing()
    add_executable(hdmi_tests tests/test_core.cpp)
    target_link_libraries(hdmi_tests PRIVATE hdmi_core)
    add_test(NAME core_tests COMMAND hdmi_tests)
endif()

# ---------------------------------------------------------------------------
# wxWidgets desktop GUI (optional; requires wxWidgets to be installed).
# ---------------------------------------------------------------------------
if(HDMI_BUILD_GUI)
    # 'adv' provides wxTaskBarIcon (the system-tray icon).
    find_package(wxWidgets COMPONENTS core base adv QUIET)
    if(wxWidgets_FOUND)
        include(${wxWidgets_USE_FILE})

        set(GUI_SOURCES src/gui/App.cpp src/gui/MainFrame.cpp)
        if(WIN32)
            enable_language(RC)
            list(APPEND GUI_SOURCES resources/app.rc)  # embeds the .exe icon
        endif()

        add_executable(hdmi_gui WIN32 ${GUI_SOURCES})
        target_link_libraries(hdmi_gui PRIVATE hdmi_core ${wxWidgets_LIBRARIES})
        message(STATUS "wxWidgets found - building hdmi_gui")

        install(TARGETS hdmi_gui RUNTIME DESTINATION bin)
    else()
        message(STATUS "wxWidgets NOT found - skipping hdmi_gui "
                       "(install wxWidgets and re-run CMake to enable the GUI)")
    endif()
endif()

# ---------------------------------------------------------------------------
# Install rules.
# ---------------------------------------------------------------------------
install(TARGETS hdmi_server RUNTIME DESTINATION bin)
install(FILES README.md DESTINATION .)
