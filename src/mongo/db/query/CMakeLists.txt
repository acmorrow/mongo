add_library(mongo-db-query
  eof_runner.cpp
  explain.cpp
  explain_plan.cpp
  get_executor.cpp
  get_runner.cpp
  idhack_runner.cpp
  internal_runner.cpp
  new_find.cpp
  plan_executor.cpp
  plan_ranker.cpp
  single_solution_runner.cpp
  stage_builder.cpp
  subplan_runner.cpp
  type_explain.cpp
)

target_link_libraries(mongo-db-query
  mongo-db-query-planner
  mongo-db-auth-core
)

add_library(mongo-db-query-index-bounds
  index_bounds.cpp
  index_bounds_builder.cpp
  interval.cpp
)

target_link_libraries(mongo-db-query-index-bounds
  mongo-db-geoquery
  mongo-db-hasher
  mongo-logger
  mongo-thirdparty-s2
)

add_library(mongo-db-query-lite-parsed-query
  lite_parsed_query.cpp
)

add_library(mongo-db-query-logger
  qlog.cpp
)

target_link_libraries(mongo-db-query-logger
  mongo-db-server-parameters
)

add_library(mongo-db-query-planner
  canonical_query.cpp
  query_settings.cpp
  index_tag.cpp
  parsed_projection.cpp
  plan_cache.cpp
  plan_enumerator.cpp
  planner_access.cpp
  planner_analysis.cpp
  planner_ixselect.cpp
  query_knobs.cpp
  query_planner.cpp
  query_solution.cpp
)

target_link_libraries(mongo-db-query-planner
  mongo-db-index-names
  mongo-db-query-index-bounds
  mongo-db-query-lite-parsed-query
  mongo-db-query-logger
)

