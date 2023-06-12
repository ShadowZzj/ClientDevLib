set(MACOS_FEATURES_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")

function(GenerateMacOSUtil)
    file(GLOB_RECURSE MACOS_UTIL_FILES 
    "${MACOS_FEATURES_CMAKE_DIR}/MacOS/util/*.h" 
    "${MACOS_FEATURES_CMAKE_DIR}/MacOS/util/*.cpp"
    "${MACOS_FEATURES_CMAKE_DIR}/MacOS/util/*.hpp"
    "${MACOS_FEATURES_CMAKE_DIR}/MacOS/util/*.mm"
    )
    message(STATUS "PCAP FEATURE: ${FEATURE_PCAP}")
    if ((NOT DEFINED FEATURE_PCAP) OR (NOT ${FEATURE_PCAP}))
		file(GLOB FEATURE_FILES 
		"${MACOS_FEATURES_CMAKE_DIR}/MacOS/util/Network/Pcap/*.h"
		"${MACOS_FEATURES_CMAKE_DIR}/MacOS/util/Network/Pcap/*.cpp"
		"${MACOS_FEATURES_CMAKE_DIR}/MacOS/util/Network/Pcap/*.mm"
		)

		list(REMOVE_ITEM MACOS_UTIL_FILES ${FEATURE_FILES})
	endif()
    foreach(source IN LISTS MACOS_UTIL_FILES)
        get_filename_component(source_path "${source}" PATH)
        string(REPLACE "${MACOS_FEATURES_CMAKE_DIR}/MacOS/util" "MacOSUtil" source_path "${source_path}")
        string(REPLACE "/" "\\" source_path_msvc "${source_path}")
        source_group("${source_path_msvc}" FILES "${source}")
    endforeach()
    set(MACOS_UTIL_FILES ${MACOS_UTIL_FILES} PARENT_SCOPE)
endfunction()