cmake_minimum_required(VERSION 3.15)

set(CMAKE_CONFIGURATION_TYPES "RelWithDebInfo" CACHE STRING "" FORCE)
set (THISDIR ${CMAKE_CURRENT_LIST_DIR})
set(OutputDirectory ${THISDIR}/build/rel_deb)
set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE STRING "" FORCE)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${OutputDirectory})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO ${OutputDirectory})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO ${OutputDirectory})

set(CMAKE_CXX_STANDARD 17)
add_definitions(-DUNICODE -D_UNICODE)
set (CMAKE_OSX_DEPLOYMENT_TARGET 10.13 CACHE STRING " target 10.13" FORCE)
set (CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "")
project(Lua-5.4.3)

file(GLOB SRC_FILE 
"${PROJECT_SOURCE_DIR}/*.h" 
"${PROJECT_SOURCE_DIR}/*.c" 
)
list(REMOVE_ITEM SRC_FILE 
"${PROJECT_SOURCE_DIR}/lua.c" 
"${PROJECT_SOURCE_DIR}/luac.c"
)
add_library(${PROJECT_NAME}_static STATIC  ${SRC_FILE})
add_library(${PROJECT_NAME}_dynamic SHARED  ${SRC_FILE})

add_executable(${PROJECT_NAME}_lua  ${SRC_FILE} ${PROJECT_SOURCE_DIR}/lua.c)
add_executable(${PROJECT_NAME}_luac  ${SRC_FILE} ${PROJECT_SOURCE_DIR}/luac.c)

set_target_properties(
    ${PROJECT_NAME}_static
                PROPERTIES
                XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME
                "YES"
)
set_property (TARGET ${PROJECT_NAME}_static APPEND_STRING PROPERTY 
              COMPILE_FLAGS "-fobjc-arc")
set_target_properties(${PROJECT_NAME}_static PROPERTIES CXX_VISIBILITY_PRESET hidden)





set_target_properties(
    ${PROJECT_NAME}_dynamic
                PROPERTIES
                XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME
                "YES"
)
set_property (TARGET ${PROJECT_NAME}_dynamic APPEND_STRING PROPERTY 
              COMPILE_FLAGS "-fobjc-arc")
set_target_properties(${PROJECT_NAME}_dynamic PROPERTIES CXX_VISIBILITY_PRESET hidden)








set_target_properties(
    ${PROJECT_NAME}_lua
                PROPERTIES
                XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME
                "YES"
)
set_property (TARGET ${PROJECT_NAME}_lua APPEND_STRING PROPERTY 
              COMPILE_FLAGS "-fobjc-arc")
set_target_properties(${PROJECT_NAME}_lua PROPERTIES CXX_VISIBILITY_PRESET hidden)







set_target_properties(
    ${PROJECT_NAME}_luac
                PROPERTIES
                XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME
                "YES"
)
set_property (TARGET ${PROJECT_NAME}_luac APPEND_STRING PROPERTY 
              COMPILE_FLAGS "-fobjc-arc")
set_target_properties(${PROJECT_NAME}_luac PROPERTIES CXX_VISIBILITY_PRESET hidden)