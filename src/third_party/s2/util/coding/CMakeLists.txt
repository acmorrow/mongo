
add_mongo_library(mongo-thirdparty-s2-util-coding
  coder.cc
  varint.cc
)

file (GLOB ch_files
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)

source_group(third_party\\s2\\util\\coding FILES ${ch_files})
