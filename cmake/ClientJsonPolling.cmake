include_guard(GLOBAL)
get_filename_component(_client_json_poll_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
include("${CMAKE_CURRENT_LIST_DIR}/ClientJson.cmake")
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Network)
add_library(clientdev_json_polling STATIC
  "${_client_json_poll_root}/General/util/Network/Qt/JsonPollingClient.cpp"
  "${_client_json_poll_root}/General/util/Network/Qt/JsonPollingClient.h")
add_library(ClientDevLib::JsonPolling ALIAS clientdev_json_polling)
target_include_directories(clientdev_json_polling PUBLIC "${_client_json_poll_root}")
target_compile_features(clientdev_json_polling PUBLIC cxx_std_17)
target_link_libraries(clientdev_json_polling PUBLIC ClientDevLib::Json Qt6::Core Qt6::Network)
set_target_properties(clientdev_json_polling PROPERTIES AUTOMOC ON)
