cmake_minimum_required(VERSION 3.15)

set(PRO_SUP ${CMAKE_CURRENT_LIST_DIR}/../../)
set(CMAKE_EXE_LINKER_FLAGS
    "${CMAKE_EXE_LINKER_FLAGS} /level='requireAdministrator' /uiAccess='false'")
set(CMAKE_CXX_STANDARD 17)

string(REPLACE "/O2" "/Od" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELEASE}")
#set /MP
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /MP /Zi")

project(DriverInput)
add_definitions(-DUNICODE -D_UNICODE)
include(${PRO_SUP}/Features.cmake)


if(CMAKE_GENERATOR_PLATFORM STREQUAL "Win32")
  set(PLATFORM_DIR x86)
  message("Platform: x86")
else()
  set(PLATFORM_DIR x64)
  message("Platform: x64")
endif()

set(CMAKE_CONFIGURATION_TYPES
    "RelWithDebInfo"
    CACHE STRING "" FORCE)
set(OutputDirectory ${CMAKE_CURRENT_SOURCE_DIR}/build)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${OutputDirectory}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO ${OutputDirectory}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO ${OutputDirectory}/bin)
set(CMAKE_PDB_OUTPUT_DIRECTORY_RELWITHDEBINFO ${OutputDirectory}/archive)


GenerateUtil(FEATURE_INTERCEPTION)

add_executable(${PROJECT_NAME} main.cpp ${WINDOWS_UTIL_FILES} ${GENERAL_UTIL_FILES})

target_include_directories(${PROJECT_NAME} PRIVATE ${WINDOWS_UTIL_INCLUDE_DIRS})

target_link_libraries(${PROJECT_NAME}  ${WINDOWS_UTIL_LIB_FILES})
target_include_directories(${PROJECT_NAME} PRIVATE ${WINDOWS_UTIL_INCLUDE_DIRS})

set_property(
  TARGET ${PROJECT_NAME} ${PROJECT_NAME}
  PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

#iterate the file in ${WINDOWS_UTIL_DLL_FILES} glob, and copy them to the CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO
foreach(dll ${WINDOWS_UTIL_DLL_FILES})
  file(COPY ${dll} DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO})
endforeach()