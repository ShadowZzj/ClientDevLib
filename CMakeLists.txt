cmake_minimum_required(VERSION 3.5.1)

set(PRO_SUP ${CMAKE_CURRENT_LIST_DIR})
set(CMAKE_EXE_LINKER_FLAGS
    "${CMAKE_EXE_LINKER_FLAGS} /level='requireAdministrator' /uiAccess='false'")
set(CMAKE_CXX_STANDARD 17)
project(ClientDevLib)
add_definitions(-DUNICODE -D_UNICODE)

include(${PRO_SUP}/General/ThirdParty/sqlite3pp/export.cmake)
include(${PRO_SUP}/General/util/ConfigManager/export.cmake)
include(${PRO_SUP}/General/util/Lua/export.cmake)
include_directories(${PRO_SUP}/General/ThirdParty ${PRO_SUP} ${PRO_SUP}/General/ThirdParty/lua/win/include ${PRO_SUP}/General/ThirdParty/boost_1_75_0)
link_directories(${PRO_SUP}/General/ThirdParty/boost_1_75_0/lib)
if(BUILD_TYPE STREQUAL "Release")
    set(CMAKE_CONFIGURATION_TYPES
        "RelWithDebInfo"
        CACHE STRING "" FORCE)
    set(OutputDirectory ${PRO_SUP}/Windows/build/Release)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${OutputDirectory}/bin)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO ${OutputDirectory}/bin)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO ${OutputDirectory}/bin)
    set(CMAKE_PDB_OUTPUT_DIRECTORY_RELWITHDEBINFO ${OutputDirectory}/archive)

# set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -O0
# -ggdb")
else()
    set(CMAKE_CONFIGURATION_TYPES
        "Debug"
        CACHE STRING "" FORCE)
    set(OutputDirectory ${PRO_SUP}/Windows/build/Debug)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${OutputDirectory}/bin)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG ${OutputDirectory}/bin)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${OutputDirectory}/bin)
    set(CMAKE_PDB_OUTPUT_DIRECTORY_DEBUG ${OutputDirectory}/archive)

    # set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -ggdb")
endif()

file(
    GLOB_RECURSE source_list
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "Windows/util/*.h"
    "Windows/util/*.cpp"
    "Windows/util/*.hpp"
    "General/util/*.h"
    "General/util/*.cpp"
    "General/util/*.hpp"
)

foreach(source IN LISTS source_list)
    get_filename_component(source_path "${source}" PATH)
    string(REPLACE "/" "\\" source_path_msvc "${source_path}")
    source_group("${source_path_msvc}" FILES "${source}")
endforeach()

add_library(
    ${PROJECT_NAME}
    STATIC
    ${source_list}
)


