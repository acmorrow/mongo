add_subdirectory(auth)
add_subdirectory(stats)

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
  mongo_util_net
)

add_library(mongo_db_message
  dbmessage.cpp
)

add_library(mongo_db_server_options_core
  server_options.cpp
)

file (GLOB ch_files
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)
source_group(mongo\\db FILES ${ch_files})
