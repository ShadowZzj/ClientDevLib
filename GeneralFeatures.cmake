
set(GENERAL_FEATURES_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")
function(GenerateGeneralUtil)
	file(GLOB_RECURSE GENERAL_UTIL_FILES 
	"${GENERAL_FEATURES_CMAKE_DIR}/General/util/*.h" 
	"${GENERAL_FEATURES_CMAKE_DIR}/General/util/*.cpp" 
	"${GENERAL_FEATURES_CMAKE_DIR}/General/util/*.hpp"
	)
	
	message(STATUS "AES FEATURE: ${FEATURE_AES}")
	if (${FEATURE_AES})
		file(GLOB FEATURE_FILES
		"${GENERAL_FEATURES_CMAKE_DIR}/General/ThirdParty/AES/*.h"
		"${GENERAL_FEATURES_CMAKE_DIR}/General/ThirdParty/AES/*.cpp"
		)

		list(APPEND GENERAL_UTIL_FILES ${FEATURE_FILES})
	endif()

	message(STATUS "SQLITE3PP FEATURE: ${FEATURE_SQLITE3PP}")
	if(${FEATURE_SQLITE3PP})
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

	message(STATUS "PLIST FEATURE: ${FEATURE_PLIST}")
	if((${FEATURE_PLIST}) OR (DEFINED APPLE))
		file(GLOB FEATURE_FILES
		"${GENERAL_FEATURES_CMAKE_DIR}/General/ThirdParty/plist/*.hpp"
		"${GENERAL_FEATURES_CMAKE_DIR}/General/ThirdParty/plist/*.cpp"
		)
		list(REMOVE_ITEM FEATURE_FILES
	     "${GENERAL_FEATURES_CMAKE_DIR}/General/ThirdParty/plist/runTests.cpp"
	     "${GENERAL_FEATURES_CMAKE_DIR}/General/ThirdParty/plist/plistTests.cpp")
		list(APPEND GENERAL_UTIL_FILES ${FEATURE_FILES})
	endif()

	message(STATUS "LUA FEATURE: ${FEATURE_LUA}")
	if ((NOT DEFINED FEATURE_LUA) OR (NOT ${FEATURE_LUA}))
		file(GLOB FEATURE_FILES
		"${GENERAL_FEATURES_CMAKE_DIR}/General/util/Lua/LuaUtil.h"
		"${GENERAL_FEATURES_CMAKE_DIR}/General/util/Lua/LuaUtil.cpp"
		)
		list(REMOVE_ITEM GENERAL_UTIL_FILES ${FEATURE_FILES})
	endif()

	message(STATUS "CURL FEATURE: ${FEATURE_CURL}")
	if ((NOT DEFINED FEATURE_CURL) OR (NOT ${FEATURE_CURL}))
		file(GLOB FEATURE_FILES
		"${GENERAL_FEATURES_CMAKE_DIR}/General/util/Network/Http/Http.h"
		"${GENERAL_FEATURES_CMAKE_DIR}/General/util/Network/Http/Http.cpp"
		)
		list(REMOVE_ITEM GENERAL_UTIL_FILES ${FEATURE_FILES})
	endif()

	message(STATUS "imgui	FEATURE: ${FEATURE_IMGUI_WIN32_DIRECTX11}")
	if (${FEATURE_IMGUI_WIN32_DIRECTX11})
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