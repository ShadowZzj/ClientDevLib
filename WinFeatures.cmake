set(WINDOWS_FEATURES_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")

function(GenerateWindowsUtil)
	file(GLOB_RECURSE WINDOWS_UTIL_FILES 
	"${WINDOWS_FEATURES_CMAKE_DIR}/Windows/util/*.h" 
	"${WINDOWS_FEATURES_CMAKE_DIR}/Windows/util/*.cpp"
	"${WINDOWS_FEATURES_CMAKE_DIR}/Windows/util/*.hpp"
	"${WINDOWS_FEATURES_CMAKE_DIR}/Windows/util/*.hlsl"
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

	# 默认所有文件都会被包含，下面开始根据特性决定是否移除某些文件
	message(STATUS "Interception feature: ${FEATURE_INTERCEPTION}")
	if ((NOT DEFINED FEATURE_INTERCEPTION) OR (NOT ${FEATURE_INTERCEPTION}))
		file(GLOB FEATURE_FILES 
		"${WINDOWS_FEATURES_CMAKE_DIR}/Windows/util/Device/Interception/*.h"
		"${WINDOWS_FEATURES_CMAKE_DIR}/Windows/util/Device/Interception/*.cpp"
		"${WINDOWS_FEATURES_CMAKE_DIR}/Windows/util/Device/Interception/*.hpp"
		)

		list(REMOVE_ITEM WINDOWS_UTIL_FILES ${FEATURE_FILES})
	endif()

	message(STATUS "DirectX feature: ${FEATURE_DIRECTX}")
	if((NOT DEFINED FEATURE_DIRECTX) OR (NOT ${FEATURE_DIRECTX}))
	    file(GLOB FEATURE_FILES 
	    "${WINDOWS_FEATURES_CMAKE_DIR}/Windows/util/Graphics/*"
		"${WINDOWS_FEATURES_CMAKE_DIR}/Windows/util/Graphics/**/*"
	    )
	    list(REMOVE_ITEM WINDOWS_UTIL_FILES ${FEATURE_FILES})
	endif()

	message(STATUS "DirectXApp feature: ${FEATURE_DIRECTXAPP}")
	if((NOT DEFINED FEATURE_DIRECTX) OR (NOT ${FEATURE_DIRECTX}) OR (NOT DEFINED FEATURE_DIRECTXAPP) OR (NOT ${FEATURE_DIRECTXAPP}))
	    file(GLOB FEATURE_FILES 
	    "${WINDOWS_FEATURES_CMAKE_DIR}/Windows/util/Application/App.h"
	    "${WINDOWS_FEATURES_CMAKE_DIR}/Windows/util/Application/App.cpp"
	    )

	    list(REMOVE_ITEM WINDOWS_UTIL_FILES ${FEATURE_FILES})
	endif()

	message(STATUS "ETW feature: ${FEATURE_ETW}")
	if ((NOT DEFINED FEATURE_ETW) OR (NOT ${FEATURE_ETW}))
	    file(GLOB FEATURE_FILES 
	    "${WINDOWS_FEATURES_CMAKE_DIR}/Windows/util/EventTrace/*.h"
	    "${WINDOWS_FEATURES_CMAKE_DIR}/Windows/util/EventTrace/*.cpp"
	    "${WINDOWS_FEATURES_CMAKE_DIR}/Windows/util/EventTrace/*.hpp"
	    )

	    list(REMOVE_ITEM WINDOWS_UTIL_FILES ${FEATURE_FILES})
	endif()

	message(STATUS "SQLite3pp feature: ${FEATURE_SQLITE3PP}")
	if(${FEATURE_SQLITE3PP})
		list(APPEND WINDOWS_UTIL_LIB_FILES ${SqliteLib})
		list(APPEND WINDOWS_UTIL_DLL_FILES ${SqliteDll})
	endif()
	
	message(STATUS "Lua feature: ${FEATURE_LUA}")
	if (${FEATURE_LUA})
		list(APPEND WINDOWS_UTIL_LIB_FILES ${LuaLib})
		list(APPEND WINDOWS_UTIL_INCLUDE_DIRS ${PRO_SUP}/General/ThirdParty/lua/win/include)
	endif()

	message(STATUS "ImGui feature: ${FEATURE_IMGUI}")
	if (${FEATURE_IMGUI_WIN32_DIRECTX11})
		list(APPEND WINDOWS_UTIL_INCLUDE_DIRS ${WINDOWS_FEATURES_CMAKE_DIR}/General/ThirdParty/imgui)
	endif()

	message(STATUS "Curl feature: ${FEATURE_CURL}")
	if (${FEATURE_CURL})
		list(APPEND WINDOWS_UTIL_LIB_FILES ${CurlLib} ${SSLLib} ${CryptoLib})
		list(APPEND WINDOWS_UTIL_DLL_FILES ${CurlDll} ${SSLDll} ${CryptoDll})
	endif()

	message(STATUS "gRPC feature: ${FEATURE_GRPC}")
	if (${FEATURE_GRPC})
		list(APPEND WINDOWS_UTIL_LIB_FILES ${GrpcLib} ${SSLLib} ${CryptoLib})
		list(APPEND WINDOWS_UTIL_DLL_FILES ${ZlibDll} ${VCRUNTIME140DLL} ${SSLDll} ${CryptoDll})
		list(APPEND WINDOWS_UTIL_INCLUDE_DIRS ${WINDOWS_FEATURES_CMAKE_DIR}/General/ThirdParty/grpc/include/win)
	endif()

	message(STATUS "OpenSSL feature: ${FEATURE_OPENSSL}")
	if (${FEATURE_OPENSSL})
		list(APPEND WINDOWS_UTIL_LIB_FILES ${SSLLib} ${CryptoLib})
		list(APPEND WINDOWS_UTIL_DLL_FILES ${SSLDll} ${CryptoDll})
		list(APPEND WINDOWS_UTIL_INCLUDE_DIRS ${WINDOWS_FEATURES_CMAKE_DIR}/General/ThirdParty/openssl/include)
	endif()

	if (${FEATURE_BOOST})
		list(APPEND WINDOWS_UTIL_LIB_FILES ${BoostLibs})
	endif()
	foreach(source IN LISTS WINDOWS_UTIL_FILES)
	    #message(STATUS "Windows util file: ${source}")
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