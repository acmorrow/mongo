
 # TODO: Should find/configure python
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/action_type.h ${CMAKE_CURRENT_BINARY_DIR}/action_type.cpp
    COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/generate_action_types.py ${CMAKE_CURRENT_SOURCE_DIR}/action_types.txt ${CMAKE_CURRENT_BINARY_DIR}/action_type.h ${CMAKE_CURRENT_BINARY_DIR}/action_type.cpp
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/generate_action_types.py ${CMAKE_CURRENT_SOURCE_DIR}/action_types.txt
)

add_library(mongo-db-auth-core
  action_set.h
  action_set.cpp
  action_type.cpp
  authorization_manager.cpp
  authorization_session.cpp
  authz_documents_update_guard.cpp
  authz_manager_external_state.cpp
  authz_manager_external_state_local.cpp
  authz_session_external_state.cpp
  privilege.cpp
  privilege_parser.cpp
  resource_pattern.cpp
  role_graph.cpp
  role_graph_update.cpp
  role_name.cpp
  role_graph_builtin_roles.cpp
  user.cpp
  user_document_parser.cpp
  user_management_commands_parser.cpp
  user_name.cpp
  user_set.cpp
)

target_link_libraries(mongo-db-auth-core
  mongo-bson-mutable
  mongo-db-common
  mongo-db-ops-update-driver
)

add_library(mongo-db-auth-mocks
  authz_manager_external_state_mock.cpp
)

add_library(mongo-db-auth-mongod
  authz_manager_external_state_d.cpp
  authz_session_external_state_d.cpp
  auth_index_d.cpp
)

target_link_libraries(mongo-db-auth-mongod
  mongo-db-auth-server-common
  mongo-db-server-options-core
)

add_library(mongo-db-auth-mongos
  authz_manager_external_state_s.cpp
  authz_session_external_state_s.cpp
  user_cache_invalidator_job.cpp
)

target_link_libraries(mongo-db-auth-mongos
  mongodb-auth-server-common
)

add_library(mongo-db-auth-server-common
  authorization_manager_global.cpp
  authz_session_external_state_server_common.cpp
  mechanism_scram.cpp
  security_key.cpp
)

target_link_libraries(mongo-db-auth-server-common
  mongo-db-auth-core
)

add_library(mongo-db-auth-server
  mongo_authentication_session.cpp
)

macro(mongo_db_auth_unittest testname)
  mongo_cxx_unittest_crutch(db/auth ${testname})
  target_link_libraries(${testname} mongo-db-auth-core)
endmacro()

mongo_db_auth_unittest(action_set_test)
mongo_db_auth_unittest(privilege_parser_test)
mongo_db_auth_unittest(role_graph_test)
mongo_db_auth_unittest(user_document_parser_test)
mongo_db_auth_unittest(user_set_test)

macro(mongo_db_authmock_unittest testname)
  mongo_db_auth_unittest(${testname})
  target_link_libraries(${testname} mongo-db-auth-mocks)
endmacro()

mongo_db_authmock_unittest(authorization_manager_test)
mongo_db_authmock_unittest(authorization_session_test)

file (GLOB ch_files
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)
source_group(mongo\\db\\auth FILES ${ch_files})
