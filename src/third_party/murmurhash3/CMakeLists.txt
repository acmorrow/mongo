
add_mongo_library(mongo-thirdparty-murmurhash3
  murmurhash3.cpp)

file (GLOB ch_files
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)
source_group(thirdparty\\murmurhash3 FILES ${ch_files})
