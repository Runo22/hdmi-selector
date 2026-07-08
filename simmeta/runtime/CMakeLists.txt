# Header-only authoring runtime: annotation macros and metadata key names.
# Deliberately dependency-free so any header may include it.

add_library(simmeta_runtime INTERFACE)
add_library(simmeta::runtime ALIAS simmeta_runtime)

target_include_directories(simmeta_runtime INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_compile_features(simmeta_runtime INTERFACE cxx_std_17)
