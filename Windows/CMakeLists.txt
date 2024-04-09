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

GenerateUtil(FEATURE_ANTIDEBUG FEATURE_D3D9 FEATURE_GHINJECTOR FEATURE_IMGUI_WIN32_DIRECTX11 FEATURE_PYBIND11 FEATURE_IMGUI_WIN32_DIRECTX9 FEATURE_PYBIND11)


add_library(${PROJECT_NAME} STATIC ${WINDOWS_UTIL_FILES} ${GENERAL_UTIL_FILES})
target_link_libraries(${PROJECT_NAME}
${PRO_SUP}/General/ThirdParty/boost_1_75_0/lib/win/x86/static/libboost_locale-vc142-mt-s-x32-1_75.lib
)
target_include_directories(${PROJECT_NAME} PRIVATE ${WINDOWS_UTIL_INCLUDE_DIRS})

add_executable(autologin test/example/so3dFullCheat/starter/autologin.cpp)
target_link_libraries(autologin
${PROJECT_NAME} 
${WINDOWS_UTIL_LIB_FILES} 
)
target_include_directories(autologin PRIVATE ${WINDOWS_UTIL_INCLUDE_DIRS})
target_link_options(autologin PRIVATE "/SAFESEH:NO")

add_executable(${PROJECT_NAME}_test 
test/example/so3dFullCheat/starter/starter.cpp
)
target_link_libraries(${PROJECT_NAME}_test
${PROJECT_NAME} 
${WINDOWS_UTIL_LIB_FILES} 
)
target_include_directories(${PROJECT_NAME}_test PRIVATE ${WINDOWS_UTIL_INCLUDE_DIRS})
target_link_options(${PROJECT_NAME}_test PRIVATE "/SAFESEH:NO")


add_library(${PROJECT_NAME}_testdll SHARED 
test/example/so3dFullCheat/testdll.cpp 
test/example/so3dFullCheat/GameManager.h
test/example/so3dFullCheat/GameManager.cpp
test/example/so3dFullCheat/GameSetting.h
test/example/so3dFullCheat/GameSetting.cpp
test/example/so3dFullCheat/SpeedHack.h
test/example/so3dFullCheat/SpeedHack.cpp
test/example/so3dFullCheat/Messager.h
test/example/so3dFullCheat/Messager.cpp
)
target_link_libraries(${PROJECT_NAME}_testdll ${PROJECT_NAME} ${WINDOWS_UTIL_LIB_FILES})
target_include_directories(${PROJECT_NAME}_testdll PRIVATE ${WINDOWS_UTIL_INCLUDE_DIRS})


add_library(ccxx SHARED 
test/example/so3dFullCheat/multiclient/ccxx.cpp
)
target_link_libraries(ccxx ${PROJECT_NAME} ${WINDOWS_UTIL_LIB_FILES})
target_include_directories(ccxx PRIVATE ${WINDOWS_UTIL_INCLUDE_DIRS})

#add_library(testdll SHARED
#test/example/so3dFullCheat/testdll.cpp
#test/example/so3dFullCheat/GameManager.h
#test/example/so3dFullCheat/GameManager.cpp
#test/example/so3dFullCheat/SpeedHack.h
#test/example/so3dFullCheat/SpeedHack.cpp
#)
#target_link_libraries(testdll ${PROJECT_NAME} ${WINDOWS_UTIL_LIB_FILES})
#target_include_directories(testdll PRIVATE ${WINDOWS_UTIL_INCLUDE_DIRS})


set_property(
  TARGET autologin ${PROJECT_NAME} ${PROJECT_NAME}_testdll ccxx ${PROJECT_NAME}_test
  PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

#iterate the file in ${WINDOWS_UTIL_DLL_FILES} glob, and copy them to the CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO

ClientDevLibFinalize()
