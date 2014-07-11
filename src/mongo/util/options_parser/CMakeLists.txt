
add_mongo_library(mongo-util-options-parser
  constraints.cpp
  environment.cpp
  option_description.cpp
  option_section.cpp
  options_parser.cpp
  startup_option_init.cpp
  startup_options.cpp
  value.cpp
)

target_link_libraries(mongo-util-options-parser
  mongo-bson
  # TODO: Should just be program_options
  ${Boost_LIBRARIES}
  mongo-thirdparty-shim-yaml
  mongo-thirdparty-shim-pcrecpp
)

add_mongo_library(mongo-util-options-parser-init
  options_parser_init.cpp
)

target_link_libraries(mongo-util-options-parser-init
  mongo-util-options-parser
)
