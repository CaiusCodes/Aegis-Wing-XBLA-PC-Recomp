# Aegis Wing builds against upstream ReXGlue v0.9.0 (the rexglue-sdk
# submodule, pinned to the commit below) plus this project's changes in
# patches/rexglue.patch: LAN play, keyboard/mouse menus, local leaderboards,
# graphics and window fixes. The patch is applied to the submodule's working
# tree at configure time, so nobody edits ReXGlue by hand; the submodule's
# commit stays the exact upstream one.
#
# A copy of the applied patch is kept in the submodule's git directory. When
# patches/rexglue.patch changes, the old copy is reversed and the new one
# applied, so pulling an updated patch needs nothing more than a reconfigure.
#
# To return the submodule to pristine upstream:
#   git -C rexglue-sdk checkout -- .  &&  git -C rexglue-sdk clean -fd

set(AEGISWING_REXGLUE_COMMIT 3eb9b511b4140d2769e27be63eae57d41bfa2afa)  # v0.9.0
set(AEGISWING_REXGLUE_PATCH "${CMAKE_CURRENT_LIST_DIR}/../patches/rexglue.patch")
cmake_path(NORMAL_PATH AEGISWING_REXGLUE_PATCH)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${AEGISWING_REXGLUE_PATCH}")

function(_aw_rexglue_git out_result out_output)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${REXSDK_DIR}" ${ARGN}
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _output
        ERROR_VARIABLE _output
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(${out_result} ${_result} PARENT_SCOPE)
    set(${out_output} "${_output}" PARENT_SCOPE)
endfunction()

function(aegis_wing_apply_rexglue_patch)
    if(NOT REXSDK_DIR OR NOT EXISTS "${REXSDK_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "ReXGlue SDK not found at '${REXSDK_DIR}'. Fetch it with:\n"
            "  git submodule update --init --recursive")
    endif()
    if(NOT EXISTS "${REXSDK_DIR}/thirdparty/fmt/CMakeLists.txt")
        message(FATAL_ERROR
            "ReXGlue's third-party libraries are missing. Fetch them with:\n"
            "  git -c core.longpaths=true submodule update --init --recursive")
    endif()
    find_package(Git REQUIRED)

    _aw_rexglue_git(_r _head rev-parse HEAD)
    if(NOT _r EQUAL 0)
        message(FATAL_ERROR "'${REXSDK_DIR}' is not a git checkout of ReXGlue: ${_head}")
    endif()
    if(NOT _head STREQUAL AEGISWING_REXGLUE_COMMIT)
        message(FATAL_ERROR
            "rexglue-sdk is at ${_head}, but Aegis Wing is built against ReXGlue v0.9.0 "
            "(${AEGISWING_REXGLUE_COMMIT}). Run:\n"
            "  git submodule update --init --recursive")
    endif()

    _aw_rexglue_git(_r _git_dir rev-parse --absolute-git-dir)
    set(_applied "${_git_dir}/aegis-wing-applied.patch")
    file(SHA256 "${AEGISWING_REXGLUE_PATCH}" _want)

    if(EXISTS "${_applied}")
        file(SHA256 "${_applied}" _have)
        if(_have STREQUAL _want)
            _aw_rexglue_git(_r _out apply --check --reverse "${_applied}")
            if(_r EQUAL 0)
                return()  # applied and unchanged
            endif()
            file(REMOVE "${_applied}")  # the tree was reset since; apply again
        else()
            # An older version of the patch is applied: take it back out first.
            _aw_rexglue_git(_r _out apply --reverse "${_applied}")
            if(NOT _r EQUAL 0)
                message(FATAL_ERROR
                    "Could not remove the previously applied ReXGlue patch:\n${_out}\n"
                    "Reset the SDK and configure again:\n"
                    "  git -C rexglue-sdk checkout -- .  &&  git -C rexglue-sdk clean -fd")
            endif()
            file(REMOVE "${_applied}")
            message(STATUS "Removed the previous Aegis Wing ReXGlue patch")
        endif()
    endif()

    _aw_rexglue_git(_r _out apply --check --reverse "${AEGISWING_REXGLUE_PATCH}")
    if(_r EQUAL 0)
        message(STATUS "Aegis Wing ReXGlue patch already present")
    else()
        _aw_rexglue_git(_r _out apply --whitespace=nowarn "${AEGISWING_REXGLUE_PATCH}")
        if(NOT _r EQUAL 0)
            message(FATAL_ERROR
                "patches/rexglue.patch does not apply to rexglue-sdk:\n${_out}\n"
                "The SDK has local edits. Reset it to upstream and configure again:\n"
                "  git -C rexglue-sdk checkout -- .  &&  git -C rexglue-sdk clean -fd")
        endif()
        message(STATUS "Applied the Aegis Wing ReXGlue patch to ${REXSDK_DIR}")
    endif()
    file(COPY_FILE "${AEGISWING_REXGLUE_PATCH}" "${_applied}")
endfunction()

aegis_wing_apply_rexglue_patch()
