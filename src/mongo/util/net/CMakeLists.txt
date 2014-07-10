
add_library(mongo_util_network
  httpclient.cpp
  listen.cpp
  message.cpp
  message_port.cpp
  sock.cpp
  socket_poll.cpp
  ssl_manager.cpp
  ssl_options.cpp
)

target_link_libraries(mongo_util_network
  mongo_db_server_options_core
  mongo_util_background_job
  mongo_util_concurrency_spinlock
  mongo_util_failpoint
  mongo_util_network_hostandport
  mongo_util_options_parser
)

add_library(mongo_util_network_hostandport
  hostandport.cpp
)

macro(mongo_util_net_unittest testname)
  mongo_cxx_unittest_crutch(util/net ${testname})
  target_link_libraries(${testname} mongo_util_network mongo_util_network_hostandport)
endmacro()

mongo_util_net_unittest(hostandport_test)
mongo_util_net_unittest(sock_test)

file (GLOB ch_files
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)
source_group(mongo\\util\\net ch_files)
