#----------------------------------------------------------------
# Generated CMake target import file.
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "gRPC::cares" for configuration ""
set_property(TARGET gRPC::cares APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(gRPC::cares PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_NOCONFIG "C"
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/lib/libcares.a"
  )

list(APPEND _IMPORT_CHECK_TARGETS gRPC::cares )
list(APPEND _IMPORT_CHECK_FILES_FOR_gRPC::cares "${_IMPORT_PREFIX}/lib/libcares.a" )

# Import target "gRPC::re2" for configuration ""
set_property(TARGET gRPC::re2 APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(gRPC::re2 PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_NOCONFIG "CXX"
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/lib/libre2.a"
  )

list(APPEND _IMPORT_CHECK_TARGETS gRPC::re2 )
list(APPEND _IMPORT_CHECK_FILES_FOR_gRPC::re2 "${_IMPORT_PREFIX}/lib/libre2.a" )

# Import target "gRPC::ssl" for configuration ""
set_property(TARGET gRPC::ssl APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(gRPC::ssl PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_NOCONFIG "CXX"
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/lib/libssl.a"
  )

list(APPEND _IMPORT_CHECK_TARGETS gRPC::ssl )
list(APPEND _IMPORT_CHECK_FILES_FOR_gRPC::ssl "${_IMPORT_PREFIX}/lib/libssl.a" )

# Import target "gRPC::crypto" for configuration ""
set_property(TARGET gRPC::crypto APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(gRPC::crypto PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_NOCONFIG "ASM;C"
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/lib/libcrypto.a"
  )

list(APPEND _IMPORT_CHECK_TARGETS gRPC::crypto )
list(APPEND _IMPORT_CHECK_FILES_FOR_gRPC::crypto "${_IMPORT_PREFIX}/lib/libcrypto.a" )

# Import target "gRPC::zlibstatic" for configuration ""
set_property(TARGET gRPC::zlibstatic APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(gRPC::zlibstatic PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_NOCONFIG "C"
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/lib/libz.a"
  )

list(APPEND _IMPORT_CHECK_TARGETS gRPC::zlibstatic )
list(APPEND _IMPORT_CHECK_FILES_FOR_gRPC::zlibstatic "${_IMPORT_PREFIX}/lib/libz.a" )

# Import target "gRPC::address_sorting" for configuration ""
set_property(TARGET gRPC::address_sorting APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(gRPC::address_sorting PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_NOCONFIG "C"
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/lib/libaddress_sorting.a"
  )

list(APPEND _IMPORT_CHECK_TARGETS gRPC::address_sorting )
list(APPEND _IMPORT_CHECK_FILES_FOR_gRPC::address_sorting "${_IMPORT_PREFIX}/lib/libaddress_sorting.a" )

# Import target "gRPC::gpr" for configuration ""
set_property(TARGET gRPC::gpr APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(gRPC::gpr PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_NOCONFIG "C;CXX"
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/lib/libgpr.a"
  )

list(APPEND _IMPORT_CHECK_TARGETS gRPC::gpr )
list(APPEND _IMPORT_CHECK_FILES_FOR_gRPC::gpr "${_IMPORT_PREFIX}/lib/libgpr.a" )

# Import target "gRPC::grpc" for configuration ""
set_property(TARGET gRPC::grpc APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(gRPC::grpc PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_NOCONFIG "C;CXX"
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/lib/libgrpc.a"
  )

list(APPEND _IMPORT_CHECK_TARGETS gRPC::grpc )
list(APPEND _IMPORT_CHECK_FILES_FOR_gRPC::grpc "${_IMPORT_PREFIX}/lib/libgrpc.a" )

# Import target "gRPC::grpc_unsecure" for configuration ""
set_property(TARGET gRPC::grpc_unsecure APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(gRPC::grpc_unsecure PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_NOCONFIG "C;CXX"
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/lib/libgrpc_unsecure.a"
  )

list(APPEND _IMPORT_CHECK_TARGETS gRPC::grpc_unsecure )
list(APPEND _IMPORT_CHECK_FILES_FOR_gRPC::grpc_unsecure "${_IMPORT_PREFIX}/lib/libgrpc_unsecure.a" )

# Import target "gRPC::grpc++" for configuration ""
set_property(TARGET gRPC::grpc++ APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(gRPC::grpc++ PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_NOCONFIG "CXX"
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/lib/libgrpc++.a"
  )

list(APPEND _IMPORT_CHECK_TARGETS gRPC::grpc++ )
list(APPEND _IMPORT_CHECK_FILES_FOR_gRPC::grpc++ "${_IMPORT_PREFIX}/lib/libgrpc++.a" )

# Import target "gRPC::grpc++_alts" for configuration ""
set_property(TARGET gRPC::grpc++_alts APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(gRPC::grpc++_alts PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_NOCONFIG "CXX"
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/lib/libgrpc++_alts.a"
  )

list(APPEND _IMPORT_CHECK_TARGETS gRPC::grpc++_alts )
list(APPEND _IMPORT_CHECK_FILES_FOR_gRPC::grpc++_alts "${_IMPORT_PREFIX}/lib/libgrpc++_alts.a" )

# Import target "gRPC::grpc++_error_details" for configuration ""
set_property(TARGET gRPC::grpc++_error_details APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(gRPC::grpc++_error_details PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_NOCONFIG "CXX"
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/lib/libgrpc++_error_details.a"
  )

list(APPEND _IMPORT_CHECK_TARGETS gRPC::grpc++_error_details )
list(APPEND _IMPORT_CHECK_FILES_FOR_gRPC::grpc++_error_details "${_IMPORT_PREFIX}/lib/libgrpc++_error_details.a" )

# Import target "gRPC::grpc++_reflection" for configuration ""
set_property(TARGET gRPC::grpc++_reflection APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(gRPC::grpc++_reflection PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_NOCONFIG "CXX"
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/lib/libgrpc++_reflection.a"
  )

list(APPEND _IMPORT_CHECK_TARGETS gRPC::grpc++_reflection )
list(APPEND _IMPORT_CHECK_FILES_FOR_gRPC::grpc++_reflection "${_IMPORT_PREFIX}/lib/libgrpc++_reflection.a" )

# Import target "gRPC::grpc++_unsecure" for configuration ""
set_property(TARGET gRPC::grpc++_unsecure APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(gRPC::grpc++_unsecure PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_NOCONFIG "CXX"
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/lib/libgrpc++_unsecure.a"
  )

list(APPEND _IMPORT_CHECK_TARGETS gRPC::grpc++_unsecure )
list(APPEND _IMPORT_CHECK_FILES_FOR_gRPC::grpc++_unsecure "${_IMPORT_PREFIX}/lib/libgrpc++_unsecure.a" )

# Import target "gRPC::grpc_plugin_support" for configuration ""
set_property(TARGET gRPC::grpc_plugin_support APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(gRPC::grpc_plugin_support PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_NOCONFIG "CXX"
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/lib/libgrpc_plugin_support.a"
  )

list(APPEND _IMPORT_CHECK_TARGETS gRPC::grpc_plugin_support )
list(APPEND _IMPORT_CHECK_FILES_FOR_gRPC::grpc_plugin_support "${_IMPORT_PREFIX}/lib/libgrpc_plugin_support.a" )

# Import target "gRPC::grpcpp_channelz" for configuration ""
set_property(TARGET gRPC::grpcpp_channelz APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(gRPC::grpcpp_channelz PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_NOCONFIG "CXX"
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/lib/libgrpcpp_channelz.a"
  )

list(APPEND _IMPORT_CHECK_TARGETS gRPC::grpcpp_channelz )
list(APPEND _IMPORT_CHECK_FILES_FOR_gRPC::grpcpp_channelz "${_IMPORT_PREFIX}/lib/libgrpcpp_channelz.a" )

# Import target "gRPC::upb" for configuration ""
set_property(TARGET gRPC::upb APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(gRPC::upb PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_NOCONFIG "C"
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/lib/libupb.a"
  )

list(APPEND _IMPORT_CHECK_TARGETS gRPC::upb )
list(APPEND _IMPORT_CHECK_FILES_FOR_gRPC::upb "${_IMPORT_PREFIX}/lib/libupb.a" )

# Import target "gRPC::grpc_cpp_plugin" for configuration ""
set_property(TARGET gRPC::grpc_cpp_plugin APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(gRPC::grpc_cpp_plugin PROPERTIES
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/bin/grpc_cpp_plugin"
  )

list(APPEND _IMPORT_CHECK_TARGETS gRPC::grpc_cpp_plugin )
list(APPEND _IMPORT_CHECK_FILES_FOR_gRPC::grpc_cpp_plugin "${_IMPORT_PREFIX}/bin/grpc_cpp_plugin" )

# Import target "gRPC::grpc_csharp_plugin" for configuration ""
set_property(TARGET gRPC::grpc_csharp_plugin APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(gRPC::grpc_csharp_plugin PROPERTIES
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/bin/grpc_csharp_plugin"
  )

list(APPEND _IMPORT_CHECK_TARGETS gRPC::grpc_csharp_plugin )
list(APPEND _IMPORT_CHECK_FILES_FOR_gRPC::grpc_csharp_plugin "${_IMPORT_PREFIX}/bin/grpc_csharp_plugin" )

# Import target "gRPC::grpc_node_plugin" for configuration ""
set_property(TARGET gRPC::grpc_node_plugin APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(gRPC::grpc_node_plugin PROPERTIES
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/bin/grpc_node_plugin"
  )

list(APPEND _IMPORT_CHECK_TARGETS gRPC::grpc_node_plugin )
list(APPEND _IMPORT_CHECK_FILES_FOR_gRPC::grpc_node_plugin "${_IMPORT_PREFIX}/bin/grpc_node_plugin" )

# Import target "gRPC::grpc_objective_c_plugin" for configuration ""
set_property(TARGET gRPC::grpc_objective_c_plugin APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(gRPC::grpc_objective_c_plugin PROPERTIES
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/bin/grpc_objective_c_plugin"
  )

list(APPEND _IMPORT_CHECK_TARGETS gRPC::grpc_objective_c_plugin )
list(APPEND _IMPORT_CHECK_FILES_FOR_gRPC::grpc_objective_c_plugin "${_IMPORT_PREFIX}/bin/grpc_objective_c_plugin" )

# Import target "gRPC::grpc_php_plugin" for configuration ""
set_property(TARGET gRPC::grpc_php_plugin APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(gRPC::grpc_php_plugin PROPERTIES
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/bin/grpc_php_plugin"
  )

list(APPEND _IMPORT_CHECK_TARGETS gRPC::grpc_php_plugin )
list(APPEND _IMPORT_CHECK_FILES_FOR_gRPC::grpc_php_plugin "${_IMPORT_PREFIX}/bin/grpc_php_plugin" )

# Import target "gRPC::grpc_python_plugin" for configuration ""
set_property(TARGET gRPC::grpc_python_plugin APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(gRPC::grpc_python_plugin PROPERTIES
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/bin/grpc_python_plugin"
  )

list(APPEND _IMPORT_CHECK_TARGETS gRPC::grpc_python_plugin )
list(APPEND _IMPORT_CHECK_FILES_FOR_gRPC::grpc_python_plugin "${_IMPORT_PREFIX}/bin/grpc_python_plugin" )

# Import target "gRPC::grpc_ruby_plugin" for configuration ""
set_property(TARGET gRPC::grpc_ruby_plugin APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(gRPC::grpc_ruby_plugin PROPERTIES
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/bin/grpc_ruby_plugin"
  )

list(APPEND _IMPORT_CHECK_TARGETS gRPC::grpc_ruby_plugin )
list(APPEND _IMPORT_CHECK_FILES_FOR_gRPC::grpc_ruby_plugin "${_IMPORT_PREFIX}/bin/grpc_ruby_plugin" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
