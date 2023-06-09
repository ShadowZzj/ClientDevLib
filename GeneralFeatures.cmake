
file(GLOB_RECURSE GENERAL_UTIL_FILES 
"${CMAKE_CURRENT_LIST_DIR}/General/util/*.h" 
"${CMAKE_CURRENT_LIST_DIR}/General/util/*.cpp" 
"${CMAKE_CURRENT_LIST_DIR}/General/util/*.hpp"
)

if (${FEATURE_AES})
	file(GLOB FEATURE_FILES
	"${CMAKE_CURRENT_LIST_DIR}/General/ThirdParty/AES/*.h"
	"${CMAKE_CURRENT_LIST_DIR}/General/ThirdParty/AES/*.cpp"
	)
	
	list(APPEND GENERAL_UTIL_FILES ${FEATURE_FILES})
endif()
if(${FEATURE_SQLITE3PP})
	file(GLOB FEATURE_FILES
	"${CMAKE_CURRENT_LIST_DIR}/General/ThirdParty/sqlite3pp/*.h"
	"${CMAKE_CURRENT_LIST_DIR}/General/ThirdParty/sqlite3pp/*.cpp"
	)
	
	list(APPEND GENERAL_UTIL_FILES ${FEATURE_FILES})
endif()
foreach(source IN LISTS GENERAL_UTIL_FILES)
    #message(STATUS "General util file: ${source}")
    get_filename_component(source_path "${source}" PATH)
    string(REPLACE "${CMAKE_CURRENT_LIST_DIR}/General/util" "GeneralUtil" source_path "${source_path}")
	string(REPLACE "${CMAKE_CURRENT_LIST_DIR}/General/ThirdParty" "ThirdParty" source_path "${source_path}")
    string(REPLACE "/" "\\" source_path_msvc "${source_path}")
    #message (STATUS "source_path_msvc: ${source_path_msvc}")
    source_group("${source_path_msvc}" FILES "${source}")
endforeach()