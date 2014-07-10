
add_library(mongo_thirdparty_s2_strings
  split.cc
  stringprintf.cc
  strutil.cc
)

file (GLOB ch_files
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)

source_group(mongo_thirdparty_s2_strings
  ch_files
)
