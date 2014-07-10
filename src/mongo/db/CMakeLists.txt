
add_library(mongo_db_bson
  jsobj.cpp
)

target_link_libraries(mongo_db_bson
  mongo_db_json
  mongo_util_md5
)

add_library(mongo_db_json
  json.cpp
)

add_library(mongo_db_lasterror
  lasterror.cpp
)

target_link_libraries(mongo_db_lasterror
  mongo_util_foundation
  mongo_util_network
)

add_library(mongo_db_server_options_core
  server_options.cpp
)



