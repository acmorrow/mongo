add_subdirectory(auth)
add_subdirectory(catalog)
add_subdirectory(geo)
add_subdirectory(matcher)
add_subdirectory(ops)
add_subdirectory(query)
add_subdirectory(stats)

add_mongo_library(mongo-db-bson
  jsobj.cpp
)

target_link_libraries(mongo-db-bson
  mongo-db-json
  mongo-util-md5
)

add_mongo_library(mongo-db-common
  field_ref.cpp
  field_ref_set.cpp
  field_parser.cpp
  write_concern_options.cpp
)

target_link_libraries(mongo-db-common
  mongo-bson
  mongo-bson-util
  mongo-util-foundation
)

add_mongo_library(mongo-db-global-optime
  global_optime.cpp
)

add_mongo_library(mongo-db-hasher
  hasher.cpp
)

add_mongo_library(mongo-db-index-names
  index_names.cpp
)

add_mongo_library(mongo-db-index-set
  index_set.cpp
)

add_mongo_library(mongo-db-json
  json.cpp
)

add_mongo_library(mongo-db-lasterror
  lasterror.cpp
)

target_link_libraries(mongo-db-lasterror
  mongo-util-foundation
  mongo-util-net
)

add_mongo_library(mongo-db-message
  dbmessage.cpp
)

add_mongo_library(mongo-db-server-options-core
  server_options.cpp
)

add_mongo_library(mongo-db-server-parameters
  server_parameters.cpp
)

add_mongo_executable(mongod
  db.cpp
)

target_link_libraries(mongod
  ${Boost_LIBRARIES}
  mongo-db-auth-mongod
  mongo-util-cmdline-utils
  mongo-util-net-message-server-port
)

file (GLOB ch_files
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)
source_group(mongo\\db FILES ${ch_files})
