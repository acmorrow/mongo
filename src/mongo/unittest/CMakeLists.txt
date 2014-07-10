add_library(mongo_unittest
  temp_dir.cpp
  unittest.cpp
)

target_link_libraries(mongo_unittest
  mongo_util_foundation
)

add_library(mongo_unittest_main
  unittest_main.cpp
)

target_link_libraries(mongo_unittest_main
  mongo_base
  mongo_unittest
)

add_library(mongo_unittest_crutch
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
