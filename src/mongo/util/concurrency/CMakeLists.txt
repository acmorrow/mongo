
add_mongo_library(mongo-util-concurrency-spinlock
  spin_lock.cpp
)

add_mongo_library(mongo-util-concurrency-mutex
  mutexdebugger.cpp
)

add_mongo_library(mongo-util-concurrency-threadname
  thread_name.cpp
)

target_link_libraries(mongo-util-concurrency-threadname
  mongo-base
)

macro(mongo_util_concurrency_unittest testname)
  mongo_cxx_unittest_crutch(util/concurrency ${testname})
  target_link_libraries(${testname} mongo-util-concurrency-spinlock)
endmacro()

mongo_util_concurrency_unittest(spin_lock_test)

file (GLOB ch_files
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)
source_group(mongo\\util\\concurrency FILES ${ch_files})
