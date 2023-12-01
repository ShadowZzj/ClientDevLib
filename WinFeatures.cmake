set(WINDOWS_FEATURES_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")
enable_language(ASM_MASM)
function(GenerateWindowsUtil)
	file(GLOB_RECURSE WINDOWS_UTIL_FILES 
	"${WINDOWS_FEATURES_CMAKE_DIR}/Windows/util/*.h" 
	"${WINDOWS_FEATURES_CMAKE_DIR}/Windows/util/*.cpp"
	"${WINDOWS_FEATURES_CMAKE_DIR}/Windows/util/*.hpp"
	"${WINDOWS_FEATURES_CMAKE_DIR}/Windows/util/*.asm"
	)
	file(GLOB_RECURSE TEST_FILES_TO_REMOVE
	"${WINDOWS_FEATURES_CMAKE_DIR}/Windows/util/Application/test/*.cpp"
	)
	
	foreach(file_to_remove ${TEST_FILES_TO_REMOVE})
	    list(REMOVE_ITEM WINDOWS_UTIL_FILES ${file_to_remove})
	endforeach()

	file(GLOB WINDOWS_UTIL_LIB_FILES)
	file(GLOB WINDOWS_UTIL_DLL_FILES)
	set(WINDOWS_UTIL_INCLUDE_DIRS)

	# 64位编译时，移除32位的文件
	if(CMAKE_VS_PLATFORM_NAME STREQUAL "x64")
		file(GLOB FEATURE_FILES 
		"${WINDOWS_FEATURES_CMAKE_DIR}/Windows/util/AntiDebug/AssemblyX86.asm"
		)
		list(REMOVE_ITEM WINDOWS_UTIL_FILES ${FEATURE_FILES})
	elseif(CMAKE_VS_PLATFORM_NAME STREQUAL "Win32")
		file(GLOB FEATURE_FILES 
		"${WINDOWS_FEATURES_CMAKE_DIR}/Windows/util/AntiDebug/AssemblyX64.asm"
		)
		list(REMOVE_ITEM WINDOWS_UTIL_FILES ${FEATURE_FILES})
	endif()
	# 默认所有文件都会被包含，下面开始根据特性决定是否移除某些文件
	if ((NOT DEFINED FEATURE_ANTIDEBUG_TLSCALLBACK) OR (NOT ${FEATURE_ANTIDEBUG_TLSCALLBACK}))
		message(STATUS "AntiDebug-TLSCallBack feature: ${FEATURE_ANTIDEBUG_TLSCALLBACK}")
		file(GLOB FEATURE_FILES 
		"${WINDOWS_FEATURES_CMAKE_DIR}/Windows/util/AntiDebug/TLSCallBack.h"
		"${WINDOWS_FEATURES_CMAKE_DIR}/Windows/util/AntiDebug/TLSCallBack.cpp"
		)

		list(REMOVE_ITEM WINDOWS_UTIL_FILES ${FEATURE_FILES})
	endif()

	if ((NOT DEFINED FEATURE_ANTIDEBUG) OR (NOT ${FEATURE_ANTIDEBUG}))
		message(STATUS "AntiDebug feature: ${FEATURE_ANTIDEBUG_TLSCALLBACK}")
		file(GLOB FEATURE_FILES 
		"${WINDOWS_FEATURES_CMAKE_DIR}/Windows/util/AntiDebug/*.h"
		"${WINDOWS_FEATURES_CMAKE_DIR}/Windows/util/AntiDebug/*.cpp"
		"${WINDOWS_FEATURES_CMAKE_DIR}/Windows/util/AntiDebug/*.asm"
		)

		list(REMOVE_ITEM WINDOWS_UTIL_FILES ${FEATURE_FILES})
	else()
		message(STATUS "AntiDebug opened, you need to disable safeseh in linker options")
	endif()

	if ((NOT DEFINED FEATURE_INTERCEPTION) OR (NOT ${FEATURE_INTERCEPTION}))
		message(STATUS "Interception feature: ${FEATURE_INTERCEPTION}")
		file(GLOB FEATURE_FILES 
		"${WINDOWS_FEATURES_CMAKE_DIR}/Windows/util/Device/Interception/*.h"
		"${WINDOWS_FEATURES_CMAKE_DIR}/Windows/util/Device/Interception/*.cpp"
		"${WINDOWS_FEATURES_CMAKE_DIR}/Windows/util/Device/Interception/*.hpp"
		)

		list(REMOVE_ITEM WINDOWS_UTIL_FILES ${FEATURE_FILES})
	else()
	
		list(APPEND WINDOWS_UTIL_LIB_FILES ${InterceptionLib})
		list(APPEND WINDOWS_UTIL_DLL_FILES ${InterceptionDll})
	endif()

	if((NOT DEFINED FEATURE_DIRECTX) OR (NOT ${FEATURE_DIRECTX}))
		message(STATUS "DirectX feature: ${FEATURE_DIRECTX}")
	    file(GLOB FEATURE_FILES 
	    "${WINDOWS_FEATURES_CMAKE_DIR}/Windows/util/Graphics/*"
		"${WINDOWS_FEATURES_CMAKE_DIR}/Windows/util/Graphics/**/*"
	    )
	    list(REMOVE_ITEM WINDOWS_UTIL_FILES ${FEATURE_FILES})
	endif()


	if((NOT DEFINED FEATURE_DIRECTX) OR (NOT ${FEATURE_DIRECTX}) OR (NOT DEFINED FEATURE_DIRECTXAPP) OR (NOT ${FEATURE_DIRECTXAPP}))
		message(STATUS "DirectXApp feature: ${FEATURE_DIRECTXAPP}")
	    file(GLOB FEATURE_FILES 
	    "${WINDOWS_FEATURES_CMAKE_DIR}/Windows/util/Application/App.h"
	    "${WINDOWS_FEATURES_CMAKE_DIR}/Windows/util/Application/App.cpp"
	    )

	    list(REMOVE_ITEM WINDOWS_UTIL_FILES ${FEATURE_FILES})
	endif()


	if ((NOT DEFINED FEATURE_ETW) OR (NOT ${FEATURE_ETW}))
		message(STATUS "ETW feature: ${FEATURE_ETW}")
	    file(GLOB FEATURE_FILES 
	    "${WINDOWS_FEATURES_CMAKE_DIR}/Windows/util/EventTrace/*.h"
	    "${WINDOWS_FEATURES_CMAKE_DIR}/Windows/util/EventTrace/*.cpp"
	    "${WINDOWS_FEATURES_CMAKE_DIR}/Windows/util/EventTrace/*.hpp"
	    )

	    list(REMOVE_ITEM WINDOWS_UTIL_FILES ${FEATURE_FILES})
	endif()

	
	if(${FEATURE_SHADERS})
		message(STATUS "Shaders feature: ${FEATURE_SHADERS}")
		file(GLOB_RECURSE FEATURE_FILES 
			"${WINDOWS_FEATURES_CMAKE_DIR}/Windows/util/*.hlsl"
		)
		list(APPEND WINDOWS_UTIL_FILES ${FEATURE_FILES})
		
		file(GLOB VERTEX_SHADERS "${WINDOWS_FEATURES_CMAKE_DIR}/Windows/util/Graphics/Shaders/VertexShader/*.hlsl")
		foreach(SHADER ${VERTEX_SHADERS})
			set_property(SOURCE ${SHADER} PROPERTY VS_SHADER_TYPE Vertex)
			set_property(SOURCE ${SHADER} PROPERTY VS_SHADER_MODEL 5.0)
		endforeach()

		file(GLOB PIXEL_SHADERS "${WINDOWS_FEATURES_CMAKE_DIR}/Windows/util/Graphics/Shaders/PixelShader/*.hlsl")
		foreach(SHADER ${PIXEL_SHADERS})
			set_property(SOURCE ${SHADER} PROPERTY VS_SHADER_TYPE Pixel)
			set_property(SOURCE ${SHADER} PROPERTY VS_SHADER_MODEL 5.0)
		endforeach()
	endif()

	if(${FEATURE_SQLITE3PP})
		message(STATUS "SQLite3pp feature: ${FEATURE_SQLITE3PP}")
		list(APPEND WINDOWS_UTIL_LIB_FILES ${SqliteLib})
		list(APPEND WINDOWS_UTIL_DLL_FILES ${SqliteDll})
	endif()

	if(${FEATURE_DETOURS})
		message(STATUS "Detours feature: ${FEATURE_DETOURS}")
		list(APPEND WINDOWS_UTIL_LIB_FILES ${DetoursLib})
	endif()
	
	if (${FEATURE_LUA})
		message(STATUS "Lua feature: ${FEATURE_LUA}")
		list(APPEND WINDOWS_UTIL_LIB_FILES ${LuaLib})
		list(APPEND WINDOWS_UTIL_INCLUDE_DIRS ${PRO_SUP}/General/ThirdParty/lua/win/include)
	endif()

	if (${FEATURE_IMGUI_WIN32_DIRECTX11})
		message(STATUS "ImGui feature: ${FEATURE_IMGUI}")
		list(APPEND WINDOWS_UTIL_INCLUDE_DIRS ${WINDOWS_FEATURES_CMAKE_DIR}/General/ThirdParty/imgui)
	endif()


	if (${FEATURE_CURL})
		message(STATUS "Curl feature: ${FEATURE_CURL}")
		set(FEATURE_OPENSSL ON)
		list(APPEND WINDOWS_UTIL_LIB_FILES ${CurlLib} ${SSLLib} ${CryptoLib})
		list(APPEND WINDOWS_UTIL_DLL_FILES ${CurlDll} ${SSLDll} ${CryptoDll})
	endif()


	if (${FEATURE_GRPC})
		message(STATUS "gRPC feature: ${FEATURE_GRPC}")
		list(APPEND WINDOWS_UTIL_LIB_FILES ${GrpcLib} ${SSLLib} ${CryptoLib})
		list(APPEND WINDOWS_UTIL_DLL_FILES ${ZlibDll} ${VCRUNTIME140DLL} ${SSLDll} ${CryptoDll})
		list(APPEND WINDOWS_UTIL_INCLUDE_DIRS ${WINDOWS_FEATURES_CMAKE_DIR}/General/ThirdParty/grpc/include/win)
	endif()

	if (${FEATURE_OPENSSL})
		message(STATUS "OpenSSL feature: ${FEATURE_OPENSSL}")
		list(APPEND WINDOWS_UTIL_LIB_FILES ${SSLLib} ${CryptoLib})
		list(APPEND WINDOWS_UTIL_DLL_FILES ${SSLDll} ${CryptoDll})
		list(APPEND WINDOWS_UTIL_INCLUDE_DIRS ${WINDOWS_FEATURES_CMAKE_DIR}/General/ThirdParty/openssl/include)
	endif()

	if (${FEATURE_BOOST})
		message(STATUS "Boost feature: ${FEATURE_BOOST}")
		list(APPEND WINDOWS_UTIL_LIB_FILES ${BoostLibs})
	endif()

	if(${FEATURE_THIRDPARTY_ASSIMP})
		message(STATUS "Assimp feature: ${FEATURE_THIRDPARTY_ASSIMP}")
		list(APPEND WINDOWS_UTIL_LIB_FILES ${AssimpLib})
		list(APPEND WINDOWS_UTIL_DLL_FILES ${AssimpDll})
		list(APPEND WINDOWS_UTIL_INCLUDE_DIRS ${WINDOWS_FEATURES_CMAKE_DIR}/General/ThirdParty/assimp/source/include)
	endif()

	if(${FEATURE_THIRDPARTY_ZIPPER})
		message(STATUS "Zipper feature: ${FEATURE_THIRDPARTY_ZIPPER}")
		list(APPEND WINDOWS_UTIL_LIB_FILES ${ZipperLib} ${ZLibLib})
		list(APPEND WINDOWS_UTIL_INCLUDE_DIRS ${WINDOWS_FEATURES_CMAKE_DIR}/General/ThirdParty/zipper/include)
	endif()

	foreach(source IN LISTS WINDOWS_UTIL_FILES)
	    get_filename_component(source_path "${source}" PATH)
	    string(REPLACE "${WINDOWS_FEATURES_CMAKE_DIR}/Windows/util" "WindowsUtil" source_path "${source_path}")
	    string(REPLACE "/" "\\" source_path_msvc "${source_path}")
	    #message (STATUS "source_path_msvc: ${source_path_msvc}")
	    source_group("${source_path_msvc}" FILES "${source}")
	endforeach()
	set(WINDOWS_UTIL_FILES ${WINDOWS_UTIL_FILES} PARENT_SCOPE)
	set(WINDOWS_UTIL_LIB_FILES ${WINDOWS_UTIL_LIB_FILES} PARENT_SCOPE)
	set(WINDOWS_UTIL_DLL_FILES ${WINDOWS_UTIL_DLL_FILES} PARENT_SCOPE)
	set(WINDOWS_UTIL_INCLUDE_DIRS ${WINDOWS_UTIL_INCLUDE_DIRS} PARENT_SCOPE)
endfunction()