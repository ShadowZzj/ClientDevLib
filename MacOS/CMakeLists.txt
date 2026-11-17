cmake_minimum_required(VERSION 3.15)

set(PRO_SUP ${CMAKE_CURRENT_LIST_DIR}/../)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -x objective-c++")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_OSX_DEPLOYMENT_TARGET 10.15 CACHE STRING "" FORCE)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
project(ClientDevLib)
add_definitions(-DUNICODE -D_UNICODE)
include(${PRO_SUP}/Features.cmake)

include_directories(
  ${PRO_SUP}
  ${PRO_SUP}/General/ThirdParty
  ${PRO_SUP}/General/ThirdParty/lua/mac/include
  ${PRO_SUP}/General/ThirdParty/boost_1_75_0)
link_directories(${PRO_SUP}/General/ThirdParty/boost_1_75_0/lib)

if(BUILD_TYPE STREQUAL "Release")
  set(CMAKE_CONFIGURATION_TYPES
      "RelWithDebInfo"
      CACHE STRING "" FORCE)
  set(OutputDirectory ${PRO_SUP}/MacOS/build/${PLATFORM_DIR}/Release)
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
  set(OutputDirectory ${PRO_SUP}/MacOS/build/${PLATFORM_DIR}/Debug)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${OutputDirectory}/bin)
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG ${OutputDirectory}/bin)
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${OutputDirectory}/bin)
  set(CMAKE_PDB_OUTPUT_DIRECTORY_DEBUG ${OutputDirectory}/archive)

  # set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -ggdb")
endif()

GenerateUtil()

add_executable(${PROJECT_NAME}_test test.mm ${MACOS_UTIL_FILES} ${GENERAL_UTIL_FILES})


target_link_libraries(${PROJECT_NAME}_test
    PRIVATE "-framework Foundation"
    PRIVATE "-framework CoreWlan"
    PRIVATE "-framework Security"
    PRIVATE "-framework SystemConfiguration"
    PRIVATE "-framework IOKit"
    PRIVATE "-framework DiskArbitration"
    PRIVATE "-framework AppKit"
    PRIVATE pcap
    PRIVATE ${CurlDll}
    PRIVATE ${SSLDll}
    PRIVATE ${CryptoDll}
    PRIVATE ${Sqlite3Dll}
    PRIVATE ${GrpcLib}
    PRIVATE ${BoostFileSystemLib}
    PRIVATE ${SqliteLib}
)
set_property(TARGET ${PROJECT_NAME}_test APPEND_STRING PROPERTY
    COMPILE_FLAGS "-fobjc-arc")
set_target_properties(
    ${PROJECT_NAME}_test
    PROPERTIES
    XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME
    "YES"
    XCODE_ATTRIBUTE_GCC_OPTIMIZATION_LEVEL
    "0"
    XCODE_ATTRIBUTE_GCC_SYMBOLS_PRIVATE_EXTERN
    "YES"
)
set_target_properties(${PROJECT_NAME}_test PROPERTIES CXX_VISIBILITY_PRESET hidden)

file(COPY ${CurlDll} DESTINATION ${OutputDirectory})
file(COPY ${SSLDll} DESTINATION ${OutputDirectory})
file(COPY ${CryptoDll} DESTINATION ${OutputDirectory})
file(COPY ${Sqlite3Dll} DESTINATION ${OutputDirectory})