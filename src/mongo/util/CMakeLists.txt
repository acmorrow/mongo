add_subdirectory(cmdline_utils)
add_subdirectory(concurrency)
add_subdirectory(net)
add_subdirectory(options_parser)

add_mongo_library(mongo-util-background-job
  background.cpp
)

add_mongo_library(mongo-util-failpoint
  fail_point.cpp
  fail_point_registry.cpp
  fail_point_service.cpp
)

target_link_libraries(mongo-util-failpoint
  mongo-bson
  mongo-util-foundation
)

add_mongo_library(mongo-util-foundation
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

target_link_libraries(mongo-util-foundation
  mongo-base
  mongo-db-lasterror
  mongo-logger
  mongo-util-concurrency-mutex
  mongo-util-concurrency-threadname
  mongo-util-stacktrace
)

add_mongo_library(mongo-util-md5
  md5.cpp
  password_digest.cpp
)

add_mongo_library(mongo-util-safenum
  safe_num.cpp
)

if(WIN32)
  add_library(mongo-util-stacktrace
    stacktrace_win.cpp
  )
else()
  add_library(mongo-util-stacktrace
    stacktrace_posix.cpp
  )
endif()

target_link_libraries(mongo-util-stacktrace
  mongo-base
  mongo-bson
  mongo-db-bson
  mongo-util-strings
  mongo-util-version
)

add_mongo_library(mongo-util-strings
  base64.cpp
  hex.cpp
  stringutils.cpp
)

add_mongo_library(mongo-util-version
  version.cpp
)

target_link_libraries(mongo-util-version
  mongo-base
  mongo-util-strings
)

file (GLOB ch_files
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)
source_group(mongo\\util FILES ${ch_files})
