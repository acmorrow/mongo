add_library(mongo-bson-util
  bson_extract.cpp
)

target_link_libraries(mongo-bson-util
  mongo-util-foundation
)

macro(mongo_bson_util_cxx_unittest testname)
  mongo_cxx_unittest_crutch(bson/util ${testname})
  target_link_libraries(${testname} mongo-bson-util)
endmacro()

mongo_bson_util_cxx_unittest(bson_check_test)
mongo_bson_util_cxx_unittest(bson_extract_test)

file (GLOB ch_files
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)
source_group(mongo\\bson\\util FILES ${ch_files})
