#add_subdirectory(cmdline_utils)
add_subdirectory(concurrency)
#add_subdirectory(mongoutils)
add_subdirectory(net)
add_subdirectory(options_parser)

add_library(mongo_util_background_job
  background.cpp
)

add_library(mongo_util_failpoint
  fail_point.cpp
  fail_point_registry.cpp
  fail_point_service.cpp
)

target_link_libraries(mongo_util_failpoint
  mongo_bson
  mongo_util_foundation
)

add_library(mongo_util_foundation
   assert_util.cpp
   debug_util.cpp
   exception_filter_win32.cpp
   file.cpp
   log.cpp
   platform_init.cpp
   startup_test.cpp
   text.cpp
   time_support.cpp
   timer.cpp
   touch_pages.cpp
   util.cpp
)

target_link_libraries(mongo_util_foundation
  mongo_base
  mongo_db_lasterror
  mongo_logger
  mongo_util_concurrency_threadname
  mongo_util_stacktrace
)

add_library(mongo_util_md5
  md5.cpp
  password_digest.cpp
)

# TODO: Add Windows support
add_library(mongo_util_stacktrace
  stacktrace_posix.cpp
)

target_link_libraries(mongo_util_stacktrace
  mongo_base
  mongo_bson
  mongo_db_bson
  mongo_util_strings
  mongo_util_version
)

add_library(mongo_util_strings
  base64.cpp
  hex.cpp
  stringutils.cpp
)

add_library(mongo_util_version
  version.cpp
)

target_link_libraries(mongo_util_version
  mongo_base
  mongo_util_strings
)

file (GLOB ch_files
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)
source_group(mongo\\base ch_files)
