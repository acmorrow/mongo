
 # TODO: Should find/configure python
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/error_codes.h ${CMAKE_CURRENT_BINARY_DIR}/error_codes.cpp
    COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/generate_error_codes.py ${CMAKE_CURRENT_SOURCE_DIR}/error_codes.err ${CMAKE_CURRENT_BINARY_DIR}/error_codes.h ${CMAKE_CURRENT_BINARY_DIR}/error_codes.cpp
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/generate_error_codes.py ${CMAKE_CURRENT_SOURCE_DIR}/error_codes.err)

add_library(mongo_base
  error_codes.h
  error_codes.cpp
  global_initializer.cpp
  global_initializer_registerer.cpp
  init.cpp
  initializer.cpp
  initializer_context.cpp
  initializer_dependency_graph.cpp
  make_string_vector.cpp
  parse_number.cpp
  status.cpp
  string_data.cpp
)

target_link_libraries(mongo_base
  mongo_murmurhash3
)

file (GLOB ch_files
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)
source_group(mongo\\base ch_files)
