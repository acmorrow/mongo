add_library(mongo_bson_mutable
  element.cpp
  document.cpp)

target_link_libraries(mongo_bson_mutable
  mongo_base
)

file (GLOB ch_files
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)
source_group(mongo\\bson\\mutable ch_files)
