
add_library(mongo_util_net
  httpclient.cpp
  listen.cpp
  message.cpp
  message_port.cpp
  sock.cpp
  socket_poll.cpp
  ssl_manager.cpp
  ssl_options.cpp
)

target_link_libraries(mongo_util_net
  mongo_db_server_options_core
  mongo_util_background_job
  mongo_util_concurrency_spinlock
  mongo_util_failpoint
  mongo_util_net_hostandport
  mongo_util_options_parser
)

add_library(mongo_util_net_hostandport
  hostandport.cpp
)

add_library(mongo_util_net_message_server_port
  message_server_port.cpp
)

target_link_libraries(mongo_util_net_message_server_port
  mongo_util_net
  mongo_db_stats_counters
)

macro(mongo_util_net_unittest testname)
  mongo_cxx_unittest_crutch(util/net ${testname})
  target_link_libraries(${testname} mongo_util_net mongo_util_net_hostandport)
endmacro()

mongo_util_net_unittest(hostandport_test)
mongo_util_net_unittest(sock_test)

file (GLOB ch_files
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)
source_group(mongo\\util\\net ch_files)
