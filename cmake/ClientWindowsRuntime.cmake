include_guard(GLOBAL)

# Keep runtime discovery in CMake's toolchain-aware module. This helper only
# deploys release CRT libraries next to an executable, without global install rules.
function(clientdev_deploy_windows_runtime target)
    if(NOT WIN32 OR NOT MSVC)
        return()
    endif()
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
    set(CMAKE_INSTALL_DEBUG_LIBRARIES FALSE)
    set(CMAKE_INSTALL_DEBUG_LIBRARIES_ONLY FALSE)
    include(InstallRequiredSystemLibraries)
    if(NOT CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
        message(FATAL_ERROR "No MSVC redistributable runtime found for ${target}")
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} "$<TARGET_FILE_DIR:${target}>"
        COMMENT "Deploying app-local MSVC runtime for ${target}"
        COMMAND_EXPAND_LISTS
        VERBATIM)
endfunction()
