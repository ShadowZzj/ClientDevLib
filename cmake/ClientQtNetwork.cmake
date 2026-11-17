include_guard(GLOBAL)
get_filename_component(_client_qt_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Network)
add_library(clientdev_qt_network STATIC
  "${_client_qt_root}/General/util/Network/Qt/FramedChannel.cpp"
  "${_client_qt_root}/General/util/Network/Qt/FramedChannel.h"
  "${_client_qt_root}/General/util/Network/Qt/TlsConfiguration.cpp"
  "${_client_qt_root}/General/util/Network/Qt/TcpHalfClose.cpp"
  "${_client_qt_root}/General/util/Network/Qt/ReverseRelay.cpp"
  "${_client_qt_root}/General/util/Network/Qt/ReverseRelay.h"
)
add_library(ClientDevLib::QtNetwork ALIAS clientdev_qt_network)
target_include_directories(clientdev_qt_network PUBLIC "${_client_qt_root}")
target_link_libraries(clientdev_qt_network PUBLIC Qt6::Core Qt6::Network)
set_target_properties(clientdev_qt_network PROPERTIES AUTOMOC ON)
if(WIN32)
  target_sources(clientdev_qt_network PRIVATE
    "${_client_qt_root}/Windows/util/Network/Qt/NativeTcpStream.cpp"
    "${_client_qt_root}/Windows/util/Network/Qt/NativeTcpStream.h"
    "${_client_qt_root}/Windows/util/Network/Qt/NativeTcpListener.cpp"
    "${_client_qt_root}/Windows/util/Network/Qt/NativeTcpListener.h"
    "${_client_qt_root}/Windows/util/Network/Qt/NativeDatagramSocket.cpp"
    "${_client_qt_root}/Windows/util/Network/Qt/NativeDatagramSocket.h"
    "${_client_qt_root}/Windows/util/IPC/QtNamedPipeServer.cpp"
    "${_client_qt_root}/Windows/util/IPC/QtNamedPipeServer.h")
  target_link_libraries(clientdev_qt_network PUBLIC ClientDevLib::WindowsNetwork)
  target_compile_definitions(clientdev_qt_network PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
