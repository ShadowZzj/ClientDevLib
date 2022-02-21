cmake_minimum_required(VERSION 3.16)

project(antiword)

set(LIBYARA_SOURCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}")

set(LIBYARA_MODULES
        ${LIBYARA_SOURCE_PATH}/main_u.c
        ${LIBYARA_SOURCE_PATH}/asc85enc.c
        ${LIBYARA_SOURCE_PATH}/blocklist.c
        ${LIBYARA_SOURCE_PATH}/chartrans.c
        ${LIBYARA_SOURCE_PATH}/datalist.c
        ${LIBYARA_SOURCE_PATH}/depot.c
        ${LIBYARA_SOURCE_PATH}/dib2eps.c
        ${LIBYARA_SOURCE_PATH}/doclist.c
        ${LIBYARA_SOURCE_PATH}/fail.c
        ${LIBYARA_SOURCE_PATH}/finddata.c
        ${LIBYARA_SOURCE_PATH}/findtext.c
        ${LIBYARA_SOURCE_PATH}/fmt_text.c
        ${LIBYARA_SOURCE_PATH}/fontlist.c
        ${LIBYARA_SOURCE_PATH}/fonts.c
        ${LIBYARA_SOURCE_PATH}/fonts_u.c
        ${LIBYARA_SOURCE_PATH}/hdrftrlist.c
        ${LIBYARA_SOURCE_PATH}/imgexam.c
        ${LIBYARA_SOURCE_PATH}/imgtrans.c
        ${LIBYARA_SOURCE_PATH}/jpeg2eps.c
        ${LIBYARA_SOURCE_PATH}/listlist.c
        ${LIBYARA_SOURCE_PATH}/misc.c
        ${LIBYARA_SOURCE_PATH}/notes.c
        ${LIBYARA_SOURCE_PATH}/options.c
        ${LIBYARA_SOURCE_PATH}/out2window.c
        ${LIBYARA_SOURCE_PATH}/output.c
        ${LIBYARA_SOURCE_PATH}/pdf.c
        ${LIBYARA_SOURCE_PATH}/pictlist.c
        ${LIBYARA_SOURCE_PATH}/png2eps.c
        ${LIBYARA_SOURCE_PATH}/postscript.c
        ${LIBYARA_SOURCE_PATH}/prop0.c
        ${LIBYARA_SOURCE_PATH}/prop2.c
        ${LIBYARA_SOURCE_PATH}/prop6.c
        ${LIBYARA_SOURCE_PATH}/prop8.c
        ${LIBYARA_SOURCE_PATH}/properties.c
        ${LIBYARA_SOURCE_PATH}/propmod.c
        ${LIBYARA_SOURCE_PATH}/rowlist.c
        ${LIBYARA_SOURCE_PATH}/sectlist.c
        ${LIBYARA_SOURCE_PATH}/stylelist.c
        ${LIBYARA_SOURCE_PATH}/stylesheet.c
        ${LIBYARA_SOURCE_PATH}/summary.c
        ${LIBYARA_SOURCE_PATH}/tabstop.c
        ${LIBYARA_SOURCE_PATH}/text.c
        ${LIBYARA_SOURCE_PATH}/unix.c
        ${LIBYARA_SOURCE_PATH}/utf8.c
        ${LIBYARA_SOURCE_PATH}/word2text.c
        ${LIBYARA_SOURCE_PATH}/worddos.c
        ${LIBYARA_SOURCE_PATH}/wordlib.c
        ${LIBYARA_SOURCE_PATH}/wordmac.c
        ${LIBYARA_SOURCE_PATH}/wordole.c
        ${LIBYARA_SOURCE_PATH}/wordwin.c
        ${LIBYARA_SOURCE_PATH}/xmalloc.c
        ${LIBYARA_SOURCE_PATH}/xml.c)

set(PARSER_FILES
        ${PROJECT_SOURCE_DIR}/../../../../General/TerminalManager/DocsScanPlugin/CStrCvt.h
        ${PROJECT_SOURCE_DIR}/../../../../General/TerminalManager/DocsScanPlugin/CStrCvt.cpp
        ${PROJECT_SOURCE_DIR}/../../../../MacOS/TerminalManager/DocsScanPlugin/Parser/CDocParser.h
        ${PROJECT_SOURCE_DIR}/../../../../MacOS/TerminalManager/DocsScanPlugin/Parser/CDocParser.cpp)

message(${PROJECT_SOURCE_DIR}/../../../General/TerminalManager/DocsScanPlugin/CStrCvt.h)

source_group(General FILES ${LIBYARA_MODULES})
source_group(Parser FILES ${PARSER_FILES})

#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-shift-count-overflow")
add_library(antiword STATIC ${LIBYARA_MODULES} ${PARSER_FILES})

add_compile_definitions("DEBUG")

target_include_directories(
        antiword
        PRIVATE ${PROJECT_SOURCE_DIR}/../../../../General/TerminalManager/DocsScanPlugin
        PRIVATE ${PROJECT_SOURCE_DIR}/../../../../General/TerminalManager/DocsScanPlugin/Parser
        PRIVATE ${PROJECT_SOURCE_DIR}/../../../../MacOS/TerminalManager/DocsScanPlugin/Parser
)
