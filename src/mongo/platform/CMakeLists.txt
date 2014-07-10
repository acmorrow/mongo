add_library(mongo-platform
  backtrace.cpp
  posix_fadvise.cpp
  process_id
  random.cpp
  strcasestr.cpp
)

macro(mongo_platform_cxx_unittest testname)
  mongo_cxx_unittest_crutch(platform ${testname})
  target_link_libraries(${testname} mongo-platform)
endmacro()

mongo_platform_cxx_unittest(atomic_word_test)
mongo_platform_cxx_unittest(bits_test)
mongo_platform_cxx_unittest(process_id_test)
mongo_platform_cxx_unittest(random_test)

file (GLOB ch_files
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)
source_group(mongo\\platform FILES ${ch_files})
