#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "LIEF::LIEF" for configuration "Release"
set_property(TARGET LIEF::LIEF APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(LIEF::LIEF PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C;CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/LIEF.lib"
  )

list(APPEND _IMPORT_CHECK_TARGETS LIEF::LIEF )
list(APPEND _IMPORT_CHECK_FILES_FOR_LIEF::LIEF "${_IMPORT_PREFIX}/lib/LIEF.lib" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)