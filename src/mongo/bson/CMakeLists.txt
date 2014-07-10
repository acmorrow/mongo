add_subdirectory(util)
add_subdirectory(mutable)

add_library(mongo_bson
  oid.cpp
  optime.cpp
  bson_validate.cpp
)

target_link_libraries(mongo_bson
  mongo_platform
  mongo_util_foundation
)

file (GLOB ch_files
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)
source_group(mongo\\bson ch_files)
