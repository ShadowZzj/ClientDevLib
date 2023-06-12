file(GLOB_RECURSE WINDOWS_UTIL_FILES 
"${CMAKE_CURRENT_LIST_DIR}/Windows/util/*.h" 
"${CMAKE_CURRENT_LIST_DIR}/Windows/util/*.cpp"
"${CMAKE_CURRENT_LIST_DIR}/Windows/util/*.hpp"
)


# 默认所有文件都会被包含，下面开始根据特性决定是否移除某些文件
if ((NOT DEFINED FEATURE_INTERCEPTION) OR (NOT ${FEATURE_INTERCEPTION}))
	file(GLOB FEATURE_FILES 
	"${CMAKE_CURRENT_LIST_DIR}/Windows/util/Device/Interception/*.h"
	"${CMAKE_CURRENT_LIST_DIR}/Windows/util/Device/Interception/*.cpp"
	"${CMAKE_CURRENT_LIST_DIR}/Windows/util/Device/Interception/*.hpp"
	)

	list(REMOVE_ITEM WINDOWS_UTIL_FILES ${FEATURE_FILES})
endif()

if((NOT DEFINED FEATURE_DIRECTX) OR (NOT ${FEATURE_DIRECTX}))
	message(STATUS "DirectX feature is not enabled, remove DirectX related files")
    file(GLOB FEATURE_FILES 
    "${CMAKE_CURRENT_LIST_DIR}/Windows/util/Device/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/Windows/util/Device/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Windows/util/Device/*.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/Windows/util/Device/DXError/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/Windows/util/Device/DXError/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Windows/util/Device/DXError/*.hpp"
    )

    list(REMOVE_ITEM WINDOWS_UTIL_FILES ${FEATURE_FILES})
endif()

if((NOT DEFINED FEATURE_DIRECTX) OR (NOT ${FEATURE_DIRECTX}) OR (NOT DEFINED FEATURE_DIRECTXAPP) OR (NOT ${FEATURE_DIRECTXAPP}))
    file(GLOB FEATURE_FILES 
    "${CMAKE_CURRENT_LIST_DIR}/Windows/util/Application/App.h"
    "${CMAKE_CURRENT_LIST_DIR}/Windows/util/Application/App.cpp"
    )

    list(REMOVE_ITEM WINDOWS_UTIL_FILES ${FEATURE_FILES})
endif()

if ((NOT DEFINED FEATURE_ETW) OR (NOT ${FEATURE_ETW}))
    file(GLOB FEATURE_FILES 
    "${CMAKE_CURRENT_LIST_DIR}/Windows/util/EventTrace/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/Windows/util/EventTrace/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Windows/util/EventTrace/*.hpp"
    )

    list(REMOVE_ITEM WINDOWS_UTIL_FILES ${FEATURE_FILES})
endif()

foreach(source IN LISTS WINDOWS_UTIL_FILES)
    #message(STATUS "Windows util file: ${source}")
    get_filename_component(source_path "${source}" PATH)
    string(REPLACE "${CMAKE_CURRENT_LIST_DIR}/Windows/util" "WindowsUtil" source_path "${source_path}")
    string(REPLACE "/" "\\" source_path_msvc "${source_path}")
    #message (STATUS "source_path_msvc: ${source_path_msvc}")
    source_group("${source_path_msvc}" FILES "${source}")

endforeach()
