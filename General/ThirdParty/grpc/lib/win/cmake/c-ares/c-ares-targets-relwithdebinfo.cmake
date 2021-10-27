#----------------------------------------------------------------
# Generated CMake target import file for configuration "RelWithDebInfo".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "c-ares::cares" for configuration "RelWithDebInfo"
set_property(TARGET c-ares::cares APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
set_target_properties(c-ares::cares PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELWITHDEBINFO "C"
  IMPORTED_LOCATION_RELWITHDEBINFO "${_IMPORT_PREFIX}/lib/cares.lib"
  )

list(APPEND _IMPORT_CHECK_TARGETS c-ares::cares )
list(APPEND _IMPORT_CHECK_FILES_FOR_c-ares::cares "${_IMPORT_PREFIX}/lib/cares.lib" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
