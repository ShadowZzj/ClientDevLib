include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/ClientControllerDependencies.cmake")
get_filename_component(_client_queue_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
add_library(clientdev_durable_json_queue STATIC
    "${_client_queue_root}/General/util/Persistence/DurableJsonQueue.cpp")
add_library(ClientDevLib::DurableJsonQueue ALIAS clientdev_durable_json_queue)
target_link_libraries(clientdev_durable_json_queue PUBLIC ClientDevLib::VersionedJsonStore)
unset(_client_queue_root)
