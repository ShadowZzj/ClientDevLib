set(MACOS_FEATURES_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")

function(GenerateMacOSUtil)
    file(GLOB_RECURSE MACOS_UTIL_FILES 
    "${MACOS_FEATURES_CMAKE_DIR}/MacOS/util/*.h" 
    "${MACOS_FEATURES_CMAKE_DIR}/MacOS/util/*.cpp"
    "${MACOS_FEATURES_CMAKE_DIR}/MacOS/util/*.hpp"
    "${MACOS_FEATURES_CMAKE_DIR}/MacOS/util/*.mm"
    )

    file(GLOB MACOS_UTIL_LIB_FILES)
	file(GLOB MACOS_UTIL_DLL_FILES)
	set(MACOS_UTIL_INCLUDE_DIRS)

    message(STATUS "PCAP FEATURE: ${FEATURE_PCAP}")
    if ((NOT DEFINED FEATURE_PCAP) OR (NOT ${FEATURE_PCAP}))
		file(GLOB FEATURE_FILES 
		"${MACOS_FEATURES_CMAKE_DIR}/MacOS/util/Network/Pcap/*.h"
		"${MACOS_FEATURES_CMAKE_DIR}/MacOS/util/Network/Pcap/*.cpp"
		"${MACOS_FEATURES_CMAKE_DIR}/MacOS/util/Network/Pcap/*.mm"
		)

		list(REMOVE_ITEM MACOS_UTIL_FILES ${FEATURE_FILES})
	endif()

    message(STATUS "SQLite3pp feature: ${FEATURE_SQLITE3PP}")
	if(${FEATURE_SQLITE3PP})
		list(APPEND MACOS_UTIL_LIB_FILES ${SqliteLib} ${Sqlite3Dll})
		list(APPEND MACOS_UTIL_DLL_FILES ${Sqlite3Dll})
	endif()
	
	message(STATUS "Lua feature: ${FEATURE_LUA}")
	if (${FEATURE_LUA})
		list(APPEND MACOS_UTIL_LIB_FILES ${LuaLib})
		list(APPEND MACOS_UTIL_INCLUDE_DIRS ${PRO_SUP}/General/ThirdParty/lua/mac/include)
	endif()

	message(STATUS "Curl feature: ${FEATURE_CURL}")
	if (${FEATURE_CURL})
		list(APPEND MACOS_UTIL_LIB_FILES ${CurlDll} ${SSLDll} ${CryptoDll})
		list(APPEND MACOS_UTIL_DLL_FILES ${CurlDll} ${SSLDll} ${CryptoDll})
	endif()

	message(STATUS "gRPC feature: ${FEATURE_GRPC}")
	if (${FEATURE_GRPC})
		list(APPEND MACOS_UTIL_LIB_FILES ${GrpcLib} ${SSLDll} ${CryptoDll} ${ZlibLib})
		list(APPEND MACOS_UTIL_DLL_FILES ${SSLDll} ${CryptoDll})
		list(APPEND MACOS_UTIL_INCLUDE_DIRS ${MACOS_FEATURES_CMAKE_DIR}/General/ThirdParty/grpc/mac/universal/include)
	endif()

	message(STATUS "OpenSSL feature: ${FEATURE_OPENSSL}")
	if (${FEATURE_OPENSSL})
		list(APPEND MACOS_UTIL_LIB_FILES ${SSLDll} ${CryptoDll})
		list(APPEND MACOS_UTIL_DLL_FILES ${SSLDll} ${CryptoDll})
		list(APPEND MACOS_UTIL_INCLUDE_DIRS ${MACOS_FEATURES_CMAKE_DIR}/General/ThirdParty/openssl/include)
	endif()

	if (${FEATURE_BOOST})
		list(APPEND MACOS_UTIL_LIB_FILES ${BoostLibs})
	endif()

    foreach(source IN LISTS MACOS_UTIL_FILES)
        get_filename_component(source_path "${source}" PATH)
        string(REPLACE "${MACOS_FEATURES_CMAKE_DIR}/MacOS/util" "MacOSUtil" source_path "${source_path}")
        string(REPLACE "/" "\\" source_path_msvc "${source_path}")
        source_group("${source_path_msvc}" FILES "${source}")
    endforeach()
    set(MACOS_UTIL_FILES ${MACOS_UTIL_FILES} PARENT_SCOPE)
    set(MACOS_UTIL_LIB_FILES ${MACOS_UTIL_LIB_FILES} PARENT_SCOPE)
    set(MACOS_UTIL_DLL_FILES ${MACOS_UTIL_DLL_FILES} PARENT_SCOPE)
    set(MACOS_UTIL_INCLUDE_DIRS ${MACOS_UTIL_INCLUDE_DIRS} PARENT_SCOPE)
endfunction()