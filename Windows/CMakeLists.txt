cmake_minimum_required(VERSION 3.15)

set(PRO_SUP ${CMAKE_CURRENT_LIST_DIR}/../)
set(CMAKE_EXE_LINKER_FLAGS
    "${CMAKE_EXE_LINKER_FLAGS} /level='requireAdministrator' /uiAccess='false'")
set(CMAKE_CXX_STANDARD 17)

string(REPLACE "/O2" "/Od" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELEASE}")
#set /MP
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /MP /Zi")

project(ClientDevLib)
add_definitions(-DUNICODE -D_UNICODE)
include(${PRO_SUP}/Features.cmake)


if(CMAKE_GENERATOR_PLATFORM STREQUAL "Win32")
  set(PLATFORM_DIR x86)
  message("Platform: x86")
else()
  set(PLATFORM_DIR x64)
  message("Platform: x64")
endif()

if(BUILD_TYPE STREQUAL "Release")
  set(CMAKE_CONFIGURATION_TYPES
      "RelWithDebInfo"
      CACHE STRING "" FORCE)
  set(OutputDirectory ${PRO_SUP}/Windows/build/${PLATFORM_DIR}/Release)
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
  set(OutputDirectory ${PRO_SUP}/Windows/build/${PLATFORM_DIR}/Debug)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${OutputDirectory}/bin)
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG ${OutputDirectory}/bin)
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${OutputDirectory}/bin)
  set(CMAKE_PDB_OUTPUT_DIRECTORY_DEBUG ${OutputDirectory}/archive)

  # set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -ggdb")
endif()

GenerateUtil(FEATURE_ANTIDEBUG FEATURE_D3D9)

file(GLOB ProjectUtil test/util/*.cpp test/util/*.h)
add_subdirectory(${PRO_SUP}/General/ThirdParty/GHInjector/src ${OutputDirectory})

add_library(${PROJECT_NAME} STATIC ${WINDOWS_UTIL_FILES} ${GENERAL_UTIL_FILES})
add_executable(${PROJECT_NAME}_test test/test.cpp)
add_executable(${PROJECT_NAME}_testWindow test/testWindow.cpp ${ProjectUtil})
add_library(${PROJECT_NAME}_testdll SHARED test/testdll.cpp)


target_include_directories(${PROJECT_NAME} PRIVATE ${WINDOWS_UTIL_INCLUDE_DIRS})

target_link_libraries(${PROJECT_NAME}_test ${PROJECT_NAME} ${WINDOWS_UTIL_LIB_FILES})
target_include_directories(${PROJECT_NAME}_test PRIVATE ${WINDOWS_UTIL_INCLUDE_DIRS})
target_link_options(${PROJECT_NAME}_test PRIVATE "/SAFESEH:NO")

target_link_libraries(${PROJECT_NAME}_testWindow 
${PROJECT_NAME}
${WINDOWS_UTIL_LIB_FILES}
)
target_include_directories(${PROJECT_NAME}_testWindow PRIVATE ${WINDOWS_UTIL_INCLUDE_DIRS})
set_target_properties(${PROJECT_NAME}_testWindow
                      PROPERTIES LINK_FLAGS "/SUBSYSTEM:WINDOWS")


target_link_libraries(${PROJECT_NAME}_testdll ${PROJECT_NAME} ${WINDOWS_UTIL_LIB_FILES})
target_include_directories(${PROJECT_NAME}_testdll PRIVATE ${WINDOWS_UTIL_INCLUDE_DIRS})


set_property(
  TARGET ${PROJECT_NAME}_test ${PROJECT_NAME}
  PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set_property(
  TARGET ${PROJECT_NAME}_testdll ${PROJECT_NAME}
  PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set_property(
TARGET ${PROJECT_NAME}_testWindow ${PROJECT_NAME}
PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

#iterate the file in ${WINDOWS_UTIL_DLL_FILES} glob, and copy them to the CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO
foreach(dll ${WINDOWS_UTIL_DLL_FILES})
  file(COPY ${dll} DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO})
endforeach()

file(COPY test/resource/kappa50.png DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO})
file(COPY test/resource/cube.png DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO})
file(COPY test/resource/nano.gltf DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO})
file(COPY test/resource/suzanne.obj DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO})
# copy test/resource/nano_textured dir to ${CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO}
file(COPY test/resource/nano_textured DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO})