
add_library(mongo_util_options_parser
  constraints.cpp
  option_description.cpp
  option_section.cpp
  options_parser.cpp
  startup_option_init.cpp
  startup_options.cpp
  value.cpp
)

target_link_libraries(mongo_util_options_parser
  mongo_bson
  yaml-cpp # TODO(acm); FindYaml
)

add_library(mongo_util_options_parser_init
  options_parser_init.cpp
)

target_link_libraries(mongo_util_options_parser_init
  mongo_util_options_parser
)
