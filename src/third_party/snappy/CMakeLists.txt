
add_library(mongo_thirdparty_snappy
  snappy.cc
)

file (GLOB ch_files
  ${CMAKE_CURRENT_SOURCE_DIR}/snappy.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/snappy.h
)

source_group(mongo_thirdparty_snappy
  ch_files
)
