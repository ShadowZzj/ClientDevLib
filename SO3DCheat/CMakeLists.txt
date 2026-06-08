cmake_minimum_required(VERSION 3.15)

set(PRO_SUP ${CMAKE_CURRENT_LIST_DIR}/../)
set(CMAKE_CXX_STANDARD 17)

string(REPLACE "/O2" "/Od" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELEASE}")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /MP /Zi")

project(SO3DCheat)
add_definitions(-DUNICODE -D_UNICODE -DBOOST_ALL_NO_LIB)
add_compile_options(/utf-8)
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
  set(OutputDirectory ${CMAKE_CURRENT_LIST_DIR}/build/${PLATFORM_DIR}/Release)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${OutputDirectory}/bin)
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO ${OutputDirectory}/bin)
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO ${OutputDirectory}/bin)
  set(CMAKE_PDB_OUTPUT_DIRECTORY_RELWITHDEBINFO ${OutputDirectory}/archive)
else()
  set(CMAKE_CONFIGURATION_TYPES
      "Debug"
      CACHE STRING "" FORCE)
  set(OutputDirectory ${CMAKE_CURRENT_LIST_DIR}/build/${PLATFORM_DIR}/Debug)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${OutputDirectory}/bin)
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG ${OutputDirectory}/bin)
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${OutputDirectory}/bin)
  set(CMAKE_PDB_OUTPUT_DIRECTORY_DEBUG ${OutputDirectory}/archive)
endif()

GenerateUtil(FEATURE_ANTIDEBUG FEATURE_D3D9 FEATURE_GHINJECTOR FEATURE_IMGUI_WIN32_DIRECTX11 FEATURE_IMGUI_WIN32_DIRECTX9 FEATURE_CURL FEATURE_CRYPTO)

add_library(ClientDevLib STATIC ${WINDOWS_UTIL_FILES} ${GENERAL_UTIL_FILES})
target_link_libraries(ClientDevLib
    ${PRO_SUP}/General/ThirdParty/boost_1_75_0/lib/win/x86/static/libboost_locale-vc142-mt-s-x32-1_75.lib
)
target_include_directories(ClientDevLib PRIVATE ${WINDOWS_UTIL_INCLUDE_DIRS})

add_library(${PROJECT_NAME} SHARED
    dllmain.cpp
    CheatSetting.h
    CheatSetting.cpp
    IModule.h
    entity/CLocalPlayer.h
    entity/CLocalPlayer.cpp
    entity/CSkill.h
    entity/CSkill.cpp
    entity/CUser.h
    entity/CUser.cpp
    modules/DemoModule.h
    modules/GGBypassModule.h
    modules/MoveSpeedModule.h
    modules/AttackSpeedModule.h
    modules/SkillModule.h
    util/PatternResolver.h
    util/PatternResolver.cpp
    util/SpeedHack.h
    util/SpeedHack.cpp
    modules/SpeedHackModule.h
    modules/AutoSwitchModule.h
    modules/AutoReloginModule.h
    modules/NetworkMonitorModule.h
)
target_link_libraries(${PROJECT_NAME} ClientDevLib ${WINDOWS_UTIL_LIB_FILES} d3d9.lib)
target_include_directories(${PROJECT_NAME} PRIVATE ${WINDOWS_UTIL_INCLUDE_DIRS} ${CMAKE_CURRENT_LIST_DIR})
target_link_options(${PROJECT_NAME} PRIVATE "/SAFESEH:NO")

# Sibling DLL: detours CreateThread to swallow GameGuardDll.dll threads,
# plus a SO3DCheat-style skeleton (ImGui UI, spdlog, PatternResolver) that
# carries no game-specific scan data — to be filled per target.
add_library(GGThreadBlock SHARED
    GGThreadBlock/dllmain.cpp
    GGThreadBlock/GGThreadBlock.h
    GGThreadBlock/IModule.h
    GGThreadBlock/Setting.h
    GGThreadBlock/Setting.cpp
    GGThreadBlock/entity/CLocalPlayer.h
    GGThreadBlock/entity/CLocalPlayer.cpp
    GGThreadBlock/util/PatternResolver.h
    GGThreadBlock/util/PatternResolver.cpp
    GGThreadBlock/util/SpeedHack.h
    GGThreadBlock/util/SpeedHack.cpp
    GGThreadBlock/util/AutoLoginSignal.h
    GGThreadBlock/util/UserConfig.h
    GGThreadBlock/util/UserConfig.cpp
    GGThreadBlock/util/NetLog.h
    GGThreadBlock/util/NetLog.cpp
    GGThreadBlock/util/HwFpSpoof.h
    GGThreadBlock/util/HwFpSpoof.cpp
    GGThreadBlock/util/Stage1Trigger.h
    GGThreadBlock/util/Stage1Trigger.cpp
    GGThreadBlock/util/DisconnectWatchdog.h
    GGThreadBlock/util/DisconnectWatchdog.cpp
    GGThreadBlock/util/RemoteControl.h
    GGThreadBlock/util/RemoteControl.cpp
    GGThreadBlock/util/InputInjector.h
    GGThreadBlock/util/InputInjector.cpp
    GGThreadBlock/util/LoginBridge.h
    GGThreadBlock/util/LoginBridge.cpp
    GGThreadBlock/modules/StatusModule.h
    GGThreadBlock/modules/MoveSpeedModule.h
    GGThreadBlock/modules/AttackSpeedModule.h
    GGThreadBlock/modules/SkillSpeedModule.h
    GGThreadBlock/modules/SpeedHackModule.h
    GGThreadBlock/modules/ItemNoCDModule.h
    GGThreadBlock/modules/AttackRangeModule.h
    GGThreadBlock/modules/ActionMoveModule.h
    GGThreadBlock/modules/AutoPickupModule.h
    GGThreadBlock/modules/FireFullPowerModule.h
    GGThreadBlock/modules/AutoConfirmModule.h
    GGThreadBlock/modules/AutoDelegationModule.h
    GGThreadBlock/modules/AutoMailModule.h
    GGThreadBlock/modules/StationaryFarmModule.h
    GGThreadBlock/modules/BlockLevelUpModule.h
    GGThreadBlock/modules/NearbyPlayerGuardModule.h
    GGThreadBlock/modules/PlayerESPModule.h)
target_link_libraries(GGThreadBlock ClientDevLib ${WINDOWS_UTIL_LIB_FILES} d3d9.lib)
target_include_directories(GGThreadBlock PRIVATE
    ${WINDOWS_UTIL_INCLUDE_DIRS}
    ${PRO_SUP}/General/ThirdParty
    ${CMAKE_CURRENT_LIST_DIR}/GGThreadBlock)
target_link_options(GGThreadBlock PRIVATE "/SAFESEH:NO")

add_library(GGThreadBlockLoginBridgeProbe SHARED
    GGThreadBlock/LoginBridgeProbeDll.cpp
    GGThreadBlock/util/LoginBridge.h
    GGThreadBlock/util/LoginBridge.cpp)
target_link_libraries(GGThreadBlockLoginBridgeProbe ClientDevLib ${WINDOWS_UTIL_LIB_FILES})
target_include_directories(GGThreadBlockLoginBridgeProbe PRIVATE
    ${WINDOWS_UTIL_INCLUDE_DIRS}
    ${PRO_SUP}/General/ThirdParty
    ${CMAKE_CURRENT_LIST_DIR}/GGThreadBlock)
target_link_options(GGThreadBlockLoginBridgeProbe PRIVATE "/SAFESEH:NO")

add_library(GGThreadBlockLoginBridgeProbeRawRecv SHARED
    GGThreadBlock/LoginBridgeProbeDll.cpp
    GGThreadBlock/util/LoginBridge.h
    GGThreadBlock/util/LoginBridge.cpp)
target_link_libraries(GGThreadBlockLoginBridgeProbeRawRecv ClientDevLib ${WINDOWS_UTIL_LIB_FILES})
target_include_directories(GGThreadBlockLoginBridgeProbeRawRecv PRIVATE
    ${WINDOWS_UTIL_INCLUDE_DIRS}
    ${PRO_SUP}/General/ThirdParty
    ${CMAKE_CURRENT_LIST_DIR}/GGThreadBlock)
target_link_options(GGThreadBlockLoginBridgeProbeRawRecv PRIVATE "/SAFESEH:NO")

add_library(GGThreadBlockLoginBridgeProbeNativeConnect SHARED
    GGThreadBlock/LoginBridgeProbeDll.cpp
    GGThreadBlock/util/LoginBridge.h
    GGThreadBlock/util/LoginBridge.cpp)
target_link_libraries(GGThreadBlockLoginBridgeProbeNativeConnect ClientDevLib ${WINDOWS_UTIL_LIB_FILES})
target_include_directories(GGThreadBlockLoginBridgeProbeNativeConnect PRIVATE
    ${WINDOWS_UTIL_INCLUDE_DIRS}
    ${PRO_SUP}/General/ThirdParty
    ${CMAKE_CURRENT_LIST_DIR}/GGThreadBlock)
target_compile_definitions(GGThreadBlockLoginBridgeProbeNativeConnect PRIVATE
    GGTB_LOGIN_BRIDGE_PIPE_PREFIX="GGTB_LOGIN_BRIDGE_NATIVE_")
target_link_options(GGThreadBlockLoginBridgeProbeNativeConnect PRIVATE "/SAFESEH:NO")

add_executable(GGThreadBlockStarter
    GGThreadBlock/Starter.cpp)
target_link_libraries(GGThreadBlockStarter ClientDevLib ${WINDOWS_UTIL_LIB_FILES})
target_include_directories(GGThreadBlockStarter PRIVATE
    ${WINDOWS_UTIL_INCLUDE_DIRS}
    ${PRO_SUP}/General/ThirdParty)
target_link_options(GGThreadBlockStarter PRIVATE "/SAFESEH:NO")

set_property(
  TARGET ClientDevLib ${PROJECT_NAME} GGThreadBlock GGThreadBlockLoginBridgeProbe GGThreadBlockLoginBridgeProbeRawRecv GGThreadBlockLoginBridgeProbeNativeConnect GGThreadBlockStarter
  PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

ClientDevLibFinalize()
