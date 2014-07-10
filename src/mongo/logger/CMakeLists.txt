add_library(mongo_logger
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

target_link_libraries(mongo_logger
  mongo_base
  mongo_util_concurrency_threadname
)

file (GLOB ch_files
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)
source_group(mongo\\logger ch_files)
