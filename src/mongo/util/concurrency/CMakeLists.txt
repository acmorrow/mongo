
add_library(mongo_util_concurrency_spinlock
  spin_lock.cpp
)

add_library(mongo_util_concurrency_threadname
  thread_name.cpp
)

target_link_libraries(mongo_util_concurrency_threadname
  mongo_base
)
