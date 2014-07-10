add_library(mongo_bson_util
  bson_extract.cpp
)

target_link_libraries(mongo_bson_util
  mongo_util_foundation
)

file (GLOB ch_files
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)
source_group(mongo\\bson\\util ch_files)
