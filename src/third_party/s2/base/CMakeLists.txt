
add_mongo_library(mongo-thirdparty-s2-base
  int128.cc
  logging.cc
  strtoint.cc
  stringprintf.cc
)

file (GLOB ch_files
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)

source_group(third_party\\s2\\base FILES ${ch_files})
