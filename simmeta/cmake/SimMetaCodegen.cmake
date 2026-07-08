# ============================================================================
# SimMetaCodegen.cmake — build-system half of the SimMeta reflection pipeline.
#
# Provides:
#
#   simmeta_attach_codegen(
#       TARGET       <existing target>
#       ANCHOR       <source file belonging to TARGET>
#       INCLUDE_BASE <include root>          # optional; controls the path in
#                                            # the generated #include
#       HEADERS      <annotated header>...
#       EXTRA_ARGS   <extra metagen.py arg>...)  # optional
#
# For every header this registers an add_custom_command producing
#   ${CMAKE_CURRENT_BINARY_DIR}/<target>.simmeta/<stem>.simmeta.cpp
# and adds it to TARGET's sources. The command re-runs only when the header
# or the generator script changes — ordinary incremental-build semantics.
#
# ANCHOR is how the generator obtains real compile flags without hardcoding
# anything: metagen.py looks the anchor up in compile_commands.json and
# reuses that translation unit's include paths, defines, and -std flag.
# Any ordinary .cpp of the same target works, since CMake gives every source
# in a target the same usage requirements.
#
# Requires CMAKE_EXPORT_COMPILE_COMMANDS=ON (enforced below) and a generator
# that honours it (Ninja or Makefiles; on Windows use Ninja + clang-cl or
# MSVC with Ninja, not the Visual Studio generator).
# ============================================================================

include_guard(GLOBAL)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

get_filename_component(_SIMMETA_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(SIMMETA_GENERATOR_SCRIPT "${_SIMMETA_ROOT}/tools/metagen/metagen.py"
    CACHE FILEPATH "Path to the SimMeta generator script")

function(simmeta_attach_codegen)
    set(_options)
    set(_one_value TARGET ANCHOR INCLUDE_BASE)
    set(_multi_value HEADERS EXTRA_ARGS)
    cmake_parse_arguments(ARG "${_options}" "${_one_value}" "${_multi_value}"
                          ${ARGN})

    if(NOT ARG_TARGET OR NOT TARGET "${ARG_TARGET}")
        message(FATAL_ERROR
            "simmeta_attach_codegen: TARGET must name an existing target")
    endif()
    if(NOT ARG_ANCHOR)
        message(FATAL_ERROR
            "simmeta_attach_codegen: ANCHOR (a source file of TARGET) is "
            "required — its compile_commands.json entry supplies the flags")
    endif()
    if(NOT ARG_HEADERS)
        message(FATAL_ERROR
            "simmeta_attach_codegen: at least one HEADERS entry is required")
    endif()
    if(NOT CMAKE_EXPORT_COMPILE_COMMANDS)
        message(FATAL_ERROR
            "simmeta_attach_codegen requires CMAKE_EXPORT_COMPILE_COMMANDS=ON "
            "(set it in the top-level CMakeLists.txt before any target)")
    endif()

    get_filename_component(_anchor_abs "${ARG_ANCHOR}" ABSOLUTE)
    set(_gen_dir "${CMAKE_CURRENT_BINARY_DIR}/${ARG_TARGET}.simmeta")
    file(MAKE_DIRECTORY "${_gen_dir}")

    set(_generated_sources)
    foreach(_header IN LISTS ARG_HEADERS)
        get_filename_component(_header_abs "${_header}" ABSOLUTE)
        get_filename_component(_stem "${_header_abs}" NAME_WE)
        set(_out "${_gen_dir}/${_stem}.simmeta.cpp")

        set(_include_arg)
        if(ARG_INCLUDE_BASE)
            get_filename_component(_base_abs "${ARG_INCLUDE_BASE}" ABSOLUTE)
            file(RELATIVE_PATH _rel_include "${_base_abs}" "${_header_abs}")
            set(_include_arg --header-include "${_rel_include}")
        endif()

        add_custom_command(
            OUTPUT "${_out}"
            COMMAND Python3::Interpreter "${SIMMETA_GENERATOR_SCRIPT}"
                    "${_header_abs}"
                    --compile-commands "${CMAKE_BINARY_DIR}"
                    --anchor "${_anchor_abs}"
                    --output "${_out}"
                    ${_include_arg}
                    ${ARG_EXTRA_ARGS}
            DEPENDS "${_header_abs}" "${SIMMETA_GENERATOR_SCRIPT}"
            COMMENT "SimMeta: generating reflection for ${_header}"
            VERBATIM)

        list(APPEND _generated_sources "${_out}")
    endforeach()

    target_sources(${ARG_TARGET} PRIVATE ${_generated_sources})
    set_source_files_properties(${_generated_sources} PROPERTIES
        GENERATED TRUE
        SKIP_AUTOMOC ON
        SKIP_AUTOUIC ON)
endfunction()
