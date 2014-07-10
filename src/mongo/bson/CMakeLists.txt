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

macro(mongo_bson_cxx_unittest testname)
  mongo_cxx_unittest_crutch(bson ${testname})
  target_link_libraries(${testname} mongo_bson)
endmacro()

mongo_bson_cxx_unittest(bson_field_test)
mongo_bson_cxx_unittest(bson_obj_test)
mongo_bson_cxx_unittest(bson_validate_test)
mongo_bson_cxx_unittest(bsonobjbuilder_test)

file (GLOB ch_files
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)
source_group(mongo\\bson FILES ${ch_files})
