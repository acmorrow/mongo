
add_library(mongo_util_concurrency_spinlock
  spin_lock.cpp
)

add_library(mongo_util_concurrency_threadname
  thread_name.cpp
)

target_link_libraries(mongo_util_concurrency_threadname
  mongo_base
)

macro(mongo_util_concurrency_unittest testname)
  mongo_cxx_unittest_crutch(util/concurrency ${testname})
  target_link_libraries(${testname} mongo_util_concurrency_spinlock)
endmacro()

mongo_util_concurrency_unittest(spin_lock_test)

file (GLOB ch_files
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)
source_group(mongo\\util\\concurrency FILES ${ch_files})
