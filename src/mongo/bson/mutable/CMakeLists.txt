add_library(mongo-bson-mutable
  element.cpp
  document.cpp)

target_link_libraries(mongo-bson-mutable
  mongo-base
  mongo-bson
  mongo-util-safenum
)

add_library(mongo-bson-mutable-test-utils
  mutable_bson_test_utils.cpp
)

target_link_libraries(mongo-bson-mutable-test-utils
  mongo-bson-mutable
)

macro(mongo_bson_mutable_cxx_unittest testname)
  mongo_cxx_unittest_crutch(bson/mutable ${testname})
  target_link_libraries(${testname} mongo-bson-mutable-test-utils mongo-bson-mutable)
endmacro()

mongo_bson_mutable_cxx_unittest(mutable_bson_algo_test)
mongo_bson_mutable_cxx_unittest(mutable_bson_test)

file (GLOB ch_files
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)
source_group(mongo\\bson\\mutable FILES ${ch_files})
