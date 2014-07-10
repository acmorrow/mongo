
add_library(mongo_util_options_parser
  constraints.cpp
  environment.cpp
  option_description.cpp
  option_section.cpp
  options_parser.cpp
  startup_option_init.cpp
  startup_options.cpp
  value.cpp
)

# TODO(acm), create or obtain FindYaml
find_library(YamlCpp_LIBRARY
  NAMES yaml-cpp
)

target_link_libraries(mongo_util_options_parser
  mongo_bson
  # TODO: Should just be program_options
  ${Boost_LIBRARIES}
  ${YamlCpp_LIBRARY}
  ${PCRECPP_LIBRARY}
)

add_library(mongo_util_options_parser_init
  options_parser_init.cpp
)

target_link_libraries(mongo_util_options_parser_init
  mongo_util_options_parser
)
