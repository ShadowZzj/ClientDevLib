include (${PRO_SUP}/General/ThirdParty/sqlite3pp/export.cmake)
file(GLOB _CONFIGMANAGER_SRCS 
${PRO_SUP}/General/util/ConfigManager/*.h
${PRO_SUP}/General/util/ConfigManager/*.cpp
)

file(GLOB CONFIGMANAGER_SRCS
${_CONFIGMANAGER_SRCS}
${SQLITE3PP_INCLUDES}
${SQLITE3PP_SRCS}
)


source_group(ConfigManager FILES  ${_CONFIGMANAGER_SRCS})
