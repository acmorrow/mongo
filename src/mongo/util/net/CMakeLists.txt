
add_library(mongo-util-net
  httpclient.cpp
  listen.cpp
  message.cpp
  message_port.cpp
  sock.cpp
  socket_poll.cpp
  ssl_manager.cpp
  ssl_options.cpp
)

target_link_libraries(mongo-util-net
  mongo-db-server-options-core
  mongo-util-background-job
  mongo-util-concurrency-spinlock
  mongo-util-failpoint
  mongo-util-net-hostandport
  mongo-util-options-parser
)

add_library(mongo-util-net-hostandport
  hostandport.cpp
)

add_library(mongo-util-net-message-server-port
  message_server_port.cpp
)

target_link_libraries(mongo-util-net-message-server-port
  mongo-util-net
  mongo-db-stats-counters
)

macro(mongo_util_net_unittest testname)
  mongo_cxx_unittest_crutch(util/net ${testname})
  target_link_libraries(${testname} mongo-util-net mongo-util-net-hostandport)
endmacro()

mongo_util_net_unittest(hostandport_test)
mongo_util_net_unittest(sock_test)

file (GLOB ch_files
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)
source_group(mongo\\util\\net FILES ${ch_files})
