set(PRO_SUP $ENV{ZV_PRO_SUP})

file(GLOB LUAUTIL_SRCS ${PRO_SUP}/General/util/Lua/*.h
     ${PRO_SUP}/General/util/Lua/*.cpp)

source_group(LuaUtil FILES ${LUAUTIL_SRCS})
