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

GenerateUtil(FEATURE_ANTIDEBUG FEATURE_D3D9 FEATURE_GHINJECTOR FEATURE_IMGUI_WIN32_DIRECTX11 FEATURE_IMGUI_WIN32_DIRECTX9 FEATURE_CURL FEATURE_CRYPTO)


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

add_executable(rawinput_demo WIN32
test/example/rawinput_demo/rawinput_demo.cpp
)
target_link_options(rawinput_demo PRIVATE "/SAFESEH:NO")

if(CMAKE_GENERATOR_PLATFORM STREQUAL "Win32")
  set(DETOURS_LIB_DIR ${PRO_SUP}/General/ThirdParty/Detours/build/lib.X86)
else()
  set(DETOURS_LIB_DIR ${PRO_SUP}/General/ThirdParty/Detours/build/lib.X64)
endif()
set(DETOURS_INCLUDE_DIR ${PRO_SUP}/General/ThirdParty/Detours/build/include)

# SDL3 预编译静态库路径配置
if(CMAKE_GENERATOR_PLATFORM STREQUAL "Win32")
  set(SDL3_LIB_DIR ${PRO_SUP}/General/ThirdParty/SDL/build/lib.X86)
else()
  set(SDL3_LIB_DIR ${PRO_SUP}/General/ThirdParty/SDL/build/lib.X64)
endif()
set(SDL3_INCLUDE_DIR ${PRO_SUP}/General/ThirdParty/SDL/build/include)

add_executable(rawinput_detour_demo WIN32
test/example/rawinput_demo/rawinput_detour_demo.cpp
)
target_include_directories(rawinput_detour_demo PRIVATE ${DETOURS_INCLUDE_DIR})
target_link_libraries(rawinput_detour_demo PRIVATE ${DETOURS_LIB_DIR}/detours.lib)
target_link_options(rawinput_detour_demo PRIVATE "/SAFESEH:NO")


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

# rawinput_hook_dll: 跨进程注入 DLL，hook GetRawInputData（只依赖 Detours）
add_library(rawinput_hook_dll SHARED
    test/example/rawinput_demo/rawinput_hook_dll.cpp
)
target_include_directories(rawinput_hook_dll PRIVATE
    ${PRO_SUP}/General/ThirdParty/Detours/build/include
)
if(CMAKE_GENERATOR_PLATFORM STREQUAL "Win32")
    target_link_libraries(rawinput_hook_dll
        ${PRO_SUP}/General/ThirdParty/Detours/build/lib.X86/detours.lib
    )
else()
    target_link_libraries(rawinput_hook_dll
        ${PRO_SUP}/General/ThirdParty/Detours/build/lib.X64/detours.lib
    )
endif()

# dinput_hook_dll: 跨进程注入 DLL，hook DirectInput COM 接口（IDirectInputDevice8::GetDeviceState）
add_library(dinput_hook_dll SHARED
    test/example/rawinput_demo/dinput_hook_dll.cpp
)
target_include_directories(dinput_hook_dll PRIVATE
    ${PRO_SUP}/General/ThirdParty/Detours/build/include
    ${PRO_SUP}/General/ThirdParty
)
if(CMAKE_GENERATOR_PLATFORM STREQUAL "Win32")
    target_link_libraries(dinput_hook_dll
        ${PRO_SUP}/General/ThirdParty/Detours/build/lib.X86/detours.lib
        dinput8.lib
        dxguid.lib
    )
else()
    target_link_libraries(dinput_hook_dll
        ${PRO_SUP}/General/ThirdParty/Detours/build/lib.X64/detours.lib
        dinput8.lib
        dxguid.lib
    )
endif()

# dinput_demo: 使用 DirectInput 的测试程序（用于测试 dinput_hook_dll）
add_executable(dinput_demo WIN32
    test/example/rawinput_demo/dinput_demo.cpp
)
target_link_libraries(dinput_demo
    dinput8.lib
    dxguid.lib
)

# list_sdl_exports: 列出 SDL3.dll 的所有导出函数
add_executable(list_sdl_exports
    test/example/rawinput_demo/list_sdl_exports.cpp
)

# sdl3_hook_dll: 跨进程注入 DLL，hook SDL3 的 SDL_PollEvent
add_library(sdl3_hook_dll SHARED
    test/example/rawinput_demo/sdl3_hook_dll.cpp
)
target_compile_definitions(sdl3_hook_dll PRIVATE SDL_NO_COMPAT)  # 禁用 SDL 旧名称兼容宏
if(MSVC)
  # /utf-8: 让 MSVC 按 UTF-8 读取源文件，避免 C4819
  # /FS: 解决并行编译时多个 cl.exe 同时写同一个 vcXXX.pdb 导致的 C1041
  target_compile_options(sdl3_hook_dll PRIVATE /utf-8 /FS)
endif()
target_include_directories(sdl3_hook_dll PRIVATE
    ${SDL3_INCLUDE_DIR}  # SDL3 预编译头文件
    ${PRO_SUP}/General/ThirdParty/Detours/build/include
    ${PRO_SUP}/General/ThirdParty  # spdlog 等头文件
)
if(CMAKE_GENERATOR_PLATFORM STREQUAL "Win32")
    target_link_libraries(sdl3_hook_dll
        ${PRO_SUP}/General/ThirdParty/Detours/build/lib.X86/detours.lib
        $<$<CONFIG:Debug>:${SDL3_LIB_DIR}/SDL3-staticd.lib>
        $<$<CONFIG:Release>:${SDL3_LIB_DIR}/SDL3-static.lib>
        $<$<CONFIG:RelWithDebInfo>:${SDL3_LIB_DIR}/SDL3-static.lib>
        setupapi.lib
        winmm.lib
        imm32.lib
        version.lib
    )
else()
    target_link_libraries(sdl3_hook_dll
        ${PRO_SUP}/General/ThirdParty/Detours/build/lib.X64/detours.lib
        $<$<CONFIG:Debug>:${SDL3_LIB_DIR}/SDL3-staticd.lib>
        $<$<CONFIG:Release>:${SDL3_LIB_DIR}/SDL3-static.lib>
        $<$<CONFIG:RelWithDebInfo>:${SDL3_LIB_DIR}/SDL3-static.lib>
        setupapi.lib
        winmm.lib
        imm32.lib
        version.lib
    )
endif()

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
  TARGET autologin ${PROJECT_NAME} ${PROJECT_NAME}_testdll ccxx ${PROJECT_NAME}_test rawinput_demo rawinput_detour_demo rawinput_hook_dll dinput_hook_dll dinput_demo sdl3_hook_dll list_sdl_exports
  PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

#iterate the file in ${WINDOWS_UTIL_DLL_FILES} glob, and copy them to the CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO

ClientDevLibFinalize()
