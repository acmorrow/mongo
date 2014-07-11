add_mongo_library(mongo-unittest
  temp_dir.cpp
  unittest.cpp
)

target_link_libraries(mongo-unittest
  mongo-util-foundation
)

add_mongo_library(mongo-unittest-main
  unittest_main.cpp
)

target_link_libraries(mongo-unittest-main
  mongo-base
  mongo-unittest
)

add_mongo_library(mongo-unittest-crutch
  crutch.cpp
)

mongo_cxx_unittest_crutch(unittest unittest_test)
mongo_cxx_unittest_crutch(unittest fixture_test)
mongo_cxx_unittest_crutch(unittest temp_dir_test)

file (GLOB ch_files
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)
source_group(mongo\\unittest FILES ${ch_files})
