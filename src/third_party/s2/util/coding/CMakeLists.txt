
add_library(mongo_thirdpary_s2_util_coding
  coder.cc
  varint.cc
)

file (GLOB ch_files
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h

source_group(mongo_thirdpary_s2_util_coder ch_files)
