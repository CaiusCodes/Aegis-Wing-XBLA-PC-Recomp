# aegis_wing - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(aegis_wing LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Shared builds must not carry the developer's folder layout (and with it the
# Windows user name): source paths recorded by __FILE__ in log and assert
# messages are rewritten to short project-relative names. Set before the SDK
# is added so its targets inherit it too.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    cmake_path(CONVERT "${CMAKE_SOURCE_DIR}" TO_CMAKE_PATH_LIST _aw_source_root NORMALIZE)
    add_compile_options("-ffile-prefix-map=${_aw_source_root}=aegis-wing")
    if(REXSDK_DIR)
        cmake_path(CONVERT "${REXSDK_DIR}" TO_CMAKE_PATH_LIST _aw_sdk_root NORMALIZE)
        add_compile_options("-ffile-prefix-map=${_aw_sdk_root}=rexglue-sdk")
    endif()
endif()

# The ReXGlue submodule is upstream v0.9.0; apply this project's SDK changes
# (patches/rexglue.patch) before the SDK is added. See cmake/RexgluePatch.cmake.
include(cmake/RexgluePatch.cmake)

include(generated/rexglue.cmake)

if(REXSDK_DIR AND TARGET rexruntime)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        foreach(_simd_target IN ITEMS
                rexcore rexfilesystem rexui rexinput rexaudio
                rexruntime rexgpu-xenos)
            if(TARGET ${_simd_target})
                target_compile_options(${_simd_target} PRIVATE -mssse3)
            endif()
        endforeach()
    endif()

    # Keep source-SDK products inside this project's build tree.
    foreach(_sdk_target IN ITEMS rexruntime rexgpu-xenos TracyClient)
        if(TARGET ${_sdk_target})
            set_target_properties(${_sdk_target} PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/sdk-bin"
                LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/sdk-bin"
                ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/sdk-lib")
        endif()
    endforeach()
endif()

# Sources
set(AEGISWING_SOURCES
    src/main.cpp
    src/aegis_wing_crash_report.cpp
    src/aegis_wing_pc_patches.cpp
    src/aegis_wing_pc_settings.cpp
    src/aegis_wing_menu_pointer.cpp
    src/aegis_wing_title_prompt.cpp
    src/aegis_wing_high_scores.cpp
    src/aegis_wing_score_capture.cpp
    src/aegis_wing_usbcam.cpp
)

if(WIN32)
    enable_language(RC)
    list(APPEND AEGISWING_SOURCES src/aegis_wing.rc)
    add_executable(aegis_wing WIN32 ${AEGISWING_SOURCES})
    set_target_properties(aegis_wing PROPERTIES
        OUTPUT_NAME "Aegis Wing")
    # "Setup Aegis Wing.exe" and the play launcher are built by
    # packaging/Build-Stubs.ps1 (called from tools/Make-Release.ps1): they
    # need an embedded asInvoker manifest, or Windows' installer detection
    # demands administrator rights for anything named "Setup".
else()
    add_executable(aegis_wing ${AEGISWING_SOURCES})
endif()

rexglue_setup_target(aegis_wing GPU_PLUGINS xenos)

# The release version, shown small on the main menu. VERSION is the single
# source of truth: Make-Release.ps1 reads the same file.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" AEGISWING_VERSION)
string(STRIP "${AEGISWING_VERSION}" AEGISWING_VERSION)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
target_compile_definitions(aegis_wing PRIVATE
    AEGISWING_VERSION="${AEGISWING_VERSION}")

# The player-name list Setup picks from also feeds the PC Settings Player Name
# row: packaging/player_names.txt is the one copy, compiled in as a header.
set(_aw_names_source "${CMAKE_CURRENT_SOURCE_DIR}/packaging/player_names.txt")
file(STRINGS "${_aw_names_source}" _aw_names_lines)
set(_aw_names_body "")
foreach(_aw_name IN LISTS _aw_names_lines)
    string(STRIP "${_aw_name}" _aw_name)
    if(_aw_name STREQUAL "" OR _aw_name MATCHES "^#")
        continue()
    endif()
    if(_aw_name MATCHES "[\"\\\\]")
        message(FATAL_ERROR "player_names.txt: '${_aw_name}' contains a quote or backslash")
    endif()
    string(APPEND _aw_names_body "    \"${_aw_name}\",\n")
endforeach()
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated_include/aegis_wing_player_names.h"
    CONTENT "// Generated from packaging/player_names.txt by CMakeLists.txt.\n#pragma once\n\n#include <string_view>\n\ninline constexpr std::string_view kAegisWingPlayerNames[] = {\n${_aw_names_body}};\n"
    @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_aw_names_source}")
target_include_directories(aegis_wing PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated_include")

if(WIN32)
    target_link_libraries(aegis_wing PRIVATE dbghelp)
endif()

if(REXSDK_DIR)
    target_include_directories(aegis_wing PRIVATE
        "${REXSDK_DIR}/thirdparty/imgui")
endif()

# Keep a runnable development layout beside the executable. The extracted
# retail data remains separate from the generated/recompiled host sources.
add_custom_command(TARGET aegis_wing POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/game"
        "$<TARGET_FILE_DIR:aegis_wing>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/aegis_wing.toml"
        "$<TARGET_FILE_DIR:aegis_wing>/aegis_wing.toml"
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_CURRENT_SOURCE_DIR}/tools/Patch-PcMenuText.ps1"
        -ArchivePath "$<TARGET_FILE_DIR:aegis_wing>/assets/Media/Wingmen.xzp"
    COMMENT "Staging Aegis Wing game data and portable defaults"
)
