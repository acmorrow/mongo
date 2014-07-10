
add_library(mongo_thirdparty_s2_base
  int128.cc
  logging.cc
  strtoint.cc
  stringprintf.cc
)

file (GLOB ch_files
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)

source_group(mongo_thirdparty_s2_base
  ch_files
)
