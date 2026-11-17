# Independent, dependency-light targets. No legacy feature globals are changed.
if(TARGET ClientDevLib::Network)
    return()
endif()
get_filename_component(_client_network_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
add_library(client_network STATIC
    "${_client_network_root}/General/util/Network/DomainName.cpp"
    "${_client_network_root}/General/util/Network/IpAddress.cpp"
    "${_client_network_root}/General/util/Network/DnsWire.cpp")
add_library(ClientDevLib::Network ALIAS client_network)
target_compile_features(client_network PUBLIC cxx_std_17)
target_include_directories(client_network PUBLIC "${_client_network_root}")

if(WIN32)
    add_library(client_windows_network STATIC
        "${_client_network_root}/Windows/util/Network/DnsCache.cpp"
        "${_client_network_root}/Windows/util/Network/Wfp/WfpEngine.cpp"
        "${_client_network_root}/Windows/util/Network/Wfp/RedirectMetadata.cpp"
        "${_client_network_root}/Windows/util/IPC/NamedPipeSecurity.cpp")
    add_library(ClientDevLib::WindowsNetwork ALIAS client_windows_network)
    target_compile_features(client_windows_network PUBLIC cxx_std_17)
    target_include_directories(client_windows_network PUBLIC "${_client_network_root}")
    target_compile_definitions(client_windows_network PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(client_windows_network PUBLIC ClientDevLib::Network fwpuclnt ws2_32 advapi32 dnsapi)
endif()
unset(_client_network_root)
