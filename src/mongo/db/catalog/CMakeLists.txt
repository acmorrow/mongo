add_mongo_library(mongo-db-catalog
  collection.cpp
  collection_cursor_cache.cpp
  collection_info_cache.cpp
  database.cpp
  database_holder.cpp
  index_catalog.cpp
  index_catalog_entry.cpp
  index_create.cpp
)

add_mongo_library(mongo-db-catalog-collection-options
  collection_options.cpp
)

macro(mongo_db_catalog_unittest testname)
  mongo_cxx_unittest_crutch(db/catalog ${testname})
  target_link_libraries(${testname} mongo-db-catalog-collection-options)
endmacro()

mongo_db_catalog_unittest(collection_options_test)
