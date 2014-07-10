
add_library(mongo_client
  connpool.cpp
  dbclient.cpp
  dbclient_rs.cpp
  dbclientcursor.cpp
  replica_set_monitor.cpp
  sasl_client_authenticate.cpp
  syncclusterconnection.cpp
)

target_link_libraries(mongo_client
  mongo_bson_util
  mongo_db_message
  mongo_util_net
  mongo_util_net_message_server_port
)

macro(mongo_client_unittest testname)
  mongo_cxx_unittest(client ${testname})
  target_link_libraries(${testname} mongo_client mongo_dbtests_mock)
endmacro()

mongo_client_unittest(dbclient_rs_test)
target_link_libraries(dbclient_rs_test
  mongo_unittest_crutch)

mongo_client_unittest(replica_set_monitor_test)
target_link_libraries(replica_set_monitor_test
  mongo_unittest_crutch)

mongo_client_unittest(scoped_db_conn_test)

file (GLOB ch_files
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)
source_group(mongo\\client ch_files)
