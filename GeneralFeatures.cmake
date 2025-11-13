
set(GENERAL_FEATURES_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")
function(GenerateGeneralUtil)
	file(GLOB_RECURSE GENERAL_UTIL_FILES 
	"${GENERAL_FEATURES_CMAKE_DIR}/General/util/*.h" 
	"${GENERAL_FEATURES_CMAKE_DIR}/General/util/*.cpp" 
	"${GENERAL_FEATURES_CMAKE_DIR}/General/util/*.hpp"
	)
	
	if ((NOT DEFINED FEATURE_CRYPTO) OR (NOT ${FEATURE_CRYPTO}))
		file(GLOB FEATURE_FILES
		"${GENERAL_FEATURES_CMAKE_DIR}/General/util/Crypto/AES.h"
		"${GENERAL_FEATURES_CMAKE_DIR}/General/util/Crypto/AES.cpp"
		)
		list(REMOVE_ITEM GENERAL_UTIL_FILES ${FEATURE_FILES})
	else()
		message(STATUS "CURL FEATURE: ${FEATURE_CRYPTO}")
	endif()

	if(${FEATURE_SQLITE3PP})
		message(STATUS "SQLITE3PP FEATURE: ${FEATURE_SQLITE3PP}")
		file(GLOB FEATURE_FILES
		"${GENERAL_FEATURES_CMAKE_DIR}/General/ThirdParty/sqlite3pp/*.h"
		"${GENERAL_FEATURES_CMAKE_DIR}/General/ThirdParty/sqlite3pp/*.cpp"
		)

		list(APPEND GENERAL_UTIL_FILES ${FEATURE_FILES})
	else()
		file(GLOB FEATURE_FILES
		"${GENERAL_FEATURES_CMAKE_DIR}/General/util/DataBaseConfigManager/DataBaseConfigManager.h"
		"${GENERAL_FEATURES_CMAKE_DIR}/General/util/DataBaseConfigManager/DataBaseConfigManager.cpp"
		)

		list(REMOVE_ITEM GENERAL_UTIL_FILES ${FEATURE_FILES})
	endif()

	if((${FEATURE_PLIST}) OR (DEFINED APPLE))
		if(${FEATURE_PLIST})
			message(STATUS "PLIST FEATURE: ${FEATURE_PLIST}")
		endif()
		file(GLOB FEATURE_FILES
		"${GENERAL_FEATURES_CMAKE_DIR}/General/ThirdParty/plist/*.hpp"
		"${GENERAL_FEATURES_CMAKE_DIR}/General/ThirdParty/plist/*.cpp"
		)
		list(REMOVE_ITEM FEATURE_FILES
	     "${GENERAL_FEATURES_CMAKE_DIR}/General/ThirdParty/plist/runTests.cpp"
	     "${GENERAL_FEATURES_CMAKE_DIR}/General/ThirdParty/plist/plistTests.cpp")
		list(APPEND GENERAL_UTIL_FILES ${FEATURE_FILES})
	endif()

	if ((NOT DEFINED FEATURE_LUA) OR (NOT ${FEATURE_LUA}))
		file(GLOB FEATURE_FILES
		"${GENERAL_FEATURES_CMAKE_DIR}/General/util/Lua/LuaUtil.h"
		"${GENERAL_FEATURES_CMAKE_DIR}/General/util/Lua/LuaUtil.cpp"
		)
		list(REMOVE_ITEM GENERAL_UTIL_FILES ${FEATURE_FILES})
	else()
		message(STATUS "LUA FEATURE: ${FEATURE_LUA}")
	endif()

	if ((NOT DEFINED FEATURE_CURL) OR (NOT ${FEATURE_CURL}))
		file(GLOB FEATURE_FILES
		"${GENERAL_FEATURES_CMAKE_DIR}/General/util/Network/Http/Http.h"
		"${GENERAL_FEATURES_CMAKE_DIR}/General/util/Network/Http/Http.cpp"
		)
		list(REMOVE_ITEM GENERAL_UTIL_FILES ${FEATURE_FILES})
	else()
		message(STATUS "CURL FEATURE: ${FEATURE_CURL}")
	endif()

	if ((NOT DEFINED FEATURE_SSH) OR (NOT ${FEATURE_SSH}))
		file(GLOB FEATURE_FILES
		"${GENERAL_FEATURES_CMAKE_DIR}/General/util/SSH/SSHCertificate.h"
		"${GENERAL_FEATURES_CMAKE_DIR}/General/util/SSH/SSHCertificate.cpp"
		"${GENERAL_FEATURES_CMAKE_DIR}/General/util/SSH/Bytebuffer.hpp"
		"${GENERAL_FEATURES_CMAKE_DIR}/General/util/SSH/SSHAgentProtocol.h"
		"${GENERAL_FEATURES_CMAKE_DIR}/General/util/SSH/SSHAgentProtocol.cpp"
		)
		list(REMOVE_ITEM GENERAL_UTIL_FILES ${FEATURE_FILES})
	else()
		message(STATUS "SSH FEATURE: ${FEATURE_SSH}")
		set(FEATURE_OPENSSL ON)
	endif()
	if(${FEATURE_D3D9})
		message(STATUS "D3D9Hook feature: ${FEATURE_D3D9}")
		set (FEATURE_IMGUI_WIN32_DIRECTX9 ON)
		set (FEATURE_DETOURS ON)
	endif()
	if (${FEATURE_IMGUI_WIN32_DIRECTX11})
		message(STATUS "imgui FEATURE: ${FEATURE_IMGUI_WIN32_DIRECTX11}")
		file(GLOB FEATURE_FILES
		"${GENERAL_FEATURES_CMAKE_DIR}/General/ThirdParty/imgui/backends/imgui_impl_win32.h"
		"${GENERAL_FEATURES_CMAKE_DIR}/General/ThirdParty/imgui/backends/imgui_impl_win32.cpp"
		"${GENERAL_FEATURES_CMAKE_DIR}/General/ThirdParty/imgui/backends/imgui_impl_dx11.h"
		"${GENERAL_FEATURES_CMAKE_DIR}/General/ThirdParty/imgui/backends/imgui_impl_dx11.cpp"
		"${GENERAL_FEATURES_CMAKE_DIR}/General/ThirdParty/imgui/*.h"
		"${GENERAL_FEATURES_CMAKE_DIR}/General/ThirdParty/imgui/*.cpp"
		)
		list(APPEND GENERAL_UTIL_FILES ${FEATURE_FILES})
	endif()
	if (${FEATURE_IMGUI_WIN32_DIRECTX9})
		message(STATUS "imgui FEATURE: ${FEATURE_IMGUI_WIN32_DIRECTX9}")
		file(GLOB FEATURE_FILES
		"${GENERAL_FEATURES_CMAKE_DIR}/General/ThirdParty/imgui/backends/imgui_impl_win32.h"
		"${GENERAL_FEATURES_CMAKE_DIR}/General/ThirdParty/imgui/backends/imgui_impl_win32.cpp"
		"${GENERAL_FEATURES_CMAKE_DIR}/General/ThirdParty/imgui/backends/imgui_impl_dx9.h"
		"${GENERAL_FEATURES_CMAKE_DIR}/General/ThirdParty/imgui/backends/imgui_impl_dx9.cpp"
		"${GENERAL_FEATURES_CMAKE_DIR}/General/ThirdParty/imgui/*.h"
		"${GENERAL_FEATURES_CMAKE_DIR}/General/ThirdParty/imgui/*.cpp"
		)
		list(APPEND GENERAL_UTIL_FILES ${FEATURE_FILES})
	endif()

	if (${FEATURE_PYBIND11})
		message(STATUS "pybind11 FEATURE: ${FEATURE_PYBIND11}")
		file(GLOB FEATURE_FILES
		"${GENERAL_FEATURES_CMAKE_DIR}/General/ThirdParty/pybind11/include/pybind11/**.h"
		)
		list(APPEND GENERAL_UTIL_FILES ${FEATURE_FILES})
		
		set (FEATURE_PYTHON ON)
	endif()
	if(${FEATURE_PYTHON})
		message(STATUS "PYTHON FEATURE: ${FEATURE_PYTHON}")
		file(GLOB_RECURSE FEATURE_FILES
			"${GENERAL_FEATURES_CMAKE_DIR}/General/ThirdParty/python/python311/include/**/*.h"
		)
		list(APPEND GENERAL_UTIL_FILES ${FEATURE_FILES})
	endif()
	if(${FEATURE_DETOURS})
		file(GLOB FEATURE_FILES
		"${GENERAL_FEATURES_CMAKE_DIR}/General/ThirdParty/Detours/build/include/*.h"
		)
		list(APPEND GENERAL_UTIL_FILES ${FEATURE_FILES})
	endif()
	foreach(source IN LISTS GENERAL_UTIL_FILES)
	    #message(STATUS "General util file: ${source}")
	    get_filename_component(source_path "${source}" PATH)
	    string(REPLACE "${GENERAL_FEATURES_CMAKE_DIR}/General/util" "GeneralUtil" source_path "${source_path}")
		string(REPLACE "${GENERAL_FEATURES_CMAKE_DIR}/General/ThirdParty" "ThirdParty" source_path "${source_path}")
	    string(REPLACE "/" "\\" source_path_msvc "${source_path}")
	    #message (STATUS "source_path_msvc: ${source_path_msvc}")
	    source_group("${source_path_msvc}" FILES "${source}")
	endforeach()
	set(GENERAL_UTIL_FILES "${GENERAL_UTIL_FILES}" PARENT_SCOPE)
endfunction()