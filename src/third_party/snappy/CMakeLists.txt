
add_mongo_library(mongo-thirdparty-snappy
  snappy.cc
)

file (GLOB ch_files
  ${CMAKE_CURRENT_SOURCE_DIR}/snappy.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/snappy.h
)

source_group(third_party\\snappy FILES ${ch_files})
