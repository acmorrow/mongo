add_mongo_library(mongo-db-matcher-expressions
  expression.cpp
  expression_array.cpp
  expression_leaf.cpp
  expression_parser.cpp
  expression_parser_tree.cpp
  expression_tree.cpp
  expression_where_noop.cpp
  match_details.cpp
  matchable.cpp
)

target_link_libraries(mongo-db-matcher-expressions
  mongo-db-matcher-path
  mongo-thirdparty-shim-pcrecpp
)

add_mongo_library(mongo-db-matcher-expressions-geo
  expression_geo.cpp
  expression_parser_geo.cpp
)

add_mongo_library(mongo-db-matcher-expressions-text
  expression_text.cpp
  expression_parser_text.cpp
)

add_mongo_library(mongo-db-matcher-path
  path.cpp
  path_internal.cpp
)
