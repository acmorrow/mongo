
add_library(mongo_murmurhash3
  murmurhash3.cpp)

file (GLOB ch_files
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)
source_group(murmurhash3 ch_files)
