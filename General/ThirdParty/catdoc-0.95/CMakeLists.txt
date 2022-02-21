cmake_minimum_required(VERSION 3.16)

project(catppt)

set(LIBYARA_SOURCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}")

set(LIBYARA_MODULES
        ${LIBYARA_SOURCE_PATH}/src/catppt.c
        ${LIBYARA_SOURCE_PATH}/src/charsets.c
        ${LIBYARA_SOURCE_PATH}/src/confutil.c
        ${LIBYARA_SOURCE_PATH}/src/fileutil.c
        ${LIBYARA_SOURCE_PATH}/src/numutils.c
        ${LIBYARA_SOURCE_PATH}/src/ole.c
        ${LIBYARA_SOURCE_PATH}/src/pptparse.c
        ${LIBYARA_SOURCE_PATH}/src/substmap.c)

set(PARSER_FILES
        ${PROJECT_SOURCE_DIR}/../../../../General/TerminalManager/DocsScanPlugin/CStrCvt.h
        ${PROJECT_SOURCE_DIR}/../../../../General/TerminalManager/DocsScanPlugin/CStrCvt.cpp
        ${PROJECT_SOURCE_DIR}/../../../../MacOS/TerminalManager/DocsScanPlugin/Parser/CPptParser.h
        ${PROJECT_SOURCE_DIR}/../../../../MacOS/TerminalManager/DocsScanPlugin/Parser/CPptParser.cpp)

source_group(General FILES ${LIBYARA_MODULES})
source_group(Parser FILES ${PARSER_FILES})

#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-shift-count-overflow")
#add_library(catppt STATIC ${LIBYARA_MODULES} ${PARSER_FILES})
add_library(catppt STATIC ${LIBYARA_MODULES} ${PARSER_FILES})

target_include_directories(
        catppt
        PRIVATE ${LIBYARA_SOURCE_PATH}/src
        PRIVATE ${PROJECT_SOURCE_DIR}/../../../../General/TerminalManager/DocsScanPlugin
        PRIVATE ${PROJECT_SOURCE_DIR}/../../../../General/TerminalManager/DocsScanPlugin/Parser
        PRIVATE ${PROJECT_SOURCE_DIR}/../../../../MacOS/TerminalManager/DocsScanPlugin/Parser
)
