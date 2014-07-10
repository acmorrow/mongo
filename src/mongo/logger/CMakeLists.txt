add_library(mongo-logger
  console.cpp
  log_manager.cpp
  log_severity.cpp
  log_component.cpp
  log_component_settings.cpp
  logger.cpp
  logstream_builder.cpp
  message_event_utf8_encoder.cpp
  message_log_domain.cpp
  component_message_log_domain.cpp
  ramlog.cpp
  rotatable_file_manager.cpp
  rotatable_file_writer.cpp
)

target_link_libraries(mongo-logger
  mongo-base
  mongo-util-concurrency-threadname
)

macro(mongo_bson_logger_unittest testname)
  mongo_cxx_unittest_crutch(logger ${testname})
  target_link_libraries(${testname} mongo-logger)
endmacro()

mongo_bson_logger_unittest(console_test)
mongo_bson_logger_unittest(log_test)
mongo_bson_logger_unittest(rotatable_file_writer_test)

file (GLOB ch_files
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)
source_group(mongo\\logger FILES ${ch_files})
