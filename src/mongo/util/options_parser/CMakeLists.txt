
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

#include_directories(../../../third_party/yaml-cpp-0.5.1/include)

target_link_libraries(mongo_util_options_parser
  mongo_bson
  # TODO: Should just be program_options
  ${Boost_LIBRARIES}
  mongo_thirdparty_shim_yaml
  mongo_thirdparty_shim_pcrecpp
)

add_library(mongo_util_options_parser_init
  options_parser_init.cpp
)

target_link_libraries(mongo_util_options_parser_init
  mongo_util_options_parser
)
