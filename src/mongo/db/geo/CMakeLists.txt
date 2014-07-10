
add_library(mongo-db-geometry
  hash.cpp
  shapes.cpp
)

target_link_libraries(mongo-db-geometry
  mongo-thirdparty-s2
)

add_library(mongo-db-geoparser
  geoparser.cpp
)

target_link_libraries(mongo-db-geoparser
  mongo-db-geometry
)

add_library(mongo-db-geoquery
  geoquery.cpp
  r2_region_coverer.cpp
)

target_link_libraries(mongo-db-geoquery
  mongo-db-geometry
  mongo-db-geoparser
)
