add_library(mongo-db-ops-update
  modifier_add_to_set.cpp
  modifier_bit.cpp
  modifier_compare.cpp
  modifier_current_date.cpp
  modifier_inc.cpp
  modifier_object_replace.cpp
  modifier_pop.cpp
  modifier_pull.cpp
  modifier_pull_all.cpp
  modifier_push.cpp
  modifier_rename.cpp
  modifier_set.cpp
  modifier_unset.cpp
)

target_link_libraries(mongo-db-ops-update
  mongo-bson-mutable
  mongo-db-global-optime
  mongo-db-matcher-expressions
  mongo-db-ops-update-common
)

macro(mongo_db_ops_update_unittest testname)
  mongo_cxx_unittest_crutch(db/ops/update ${testname})
  target_link_libraries(${testname}
    mongo-bson-mutable-test-utils
    mongo-db-ops-update
  )
endmacro()

mongo_db_ops_update_unittest(modifier_add_to_set_test)
mongo_db_ops_update_unittest(modifier_bit_test)
mongo_db_ops_update_unittest(modifier_compare_test)
mongo_db_ops_update_unittest(modifier_current_date_test)
mongo_db_ops_update_unittest(modifier_inc_test)
mongo_db_ops_update_unittest(modifier_object_replace_test)
mongo_db_ops_update_unittest(modifier_pop_test)
mongo_db_ops_update_unittest(modifier_pull_test)
mongo_db_ops_update_unittest(modifier_pull_all_test)
mongo_db_ops_update_unittest(modifier_push_test)
mongo_db_ops_update_unittest(modifier_rename_test)
mongo_db_ops_update_unittest(modifier_set_test)
mongo_db_ops_update_unittest(modifier_unset_test)


add_library(mongo-db-ops-update-common
  field_checker.cpp
  log_builder.cpp
  path_support.cpp
)

target_link_libraries(mongo-db-ops-update-common
  mongo-bson-mutable
  mongo-db-common
)

macro(mongo_db_ops_update_common_unittest testname)
  mongo_cxx_unittest_crutch(db/ops/update/common ${testname})
  target_link_libraries(${testname}
    mongo-bson-mutable-test-utils
    mongo-db-ops-update-common
  )
endmacro()

mongo_db_ops_update_common_unittest(field_checker_test)
mongo_db_ops_update_common_unittest(log_builder_test)
mongo_db_ops_update_common_unittest(path_support_test)


add_library(mongo-db-ops-update-driver
  modifier_table.cpp
  update_driver.cpp
)

target_link_libraries(mongo-db-ops-update-driver
  mongo-db-index-set
  mongo-db-ops-update
  mongo-db-query-planner
)

macro(mongo_db_ops_update_driver_unittest testname)
  mongo_cxx_unittest_crutch(db/ops/update/driver ${testname})
  target_link_libraries(${testname}
    mongo-bson-mutable-test-utils
    mongo-db-ops-update-driver
  )
endmacro()

mongo_db_ops_update_driver_unittest(update_driver_test)


