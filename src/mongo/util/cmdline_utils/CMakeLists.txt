add_library(mongo-util-cmdline-utils
  censor_cmdline.cpp
)

macro(mongo_util_cmdline_utils_unittest testname)
  mongo_cxx_unittest_crutch(util/cmdline_utils ${testname})
  target_link_libraries(${testname} mongo-util-cmdline-utils)
endmacro()

mongo_util_cmdline_utils_unittest(censor_cmdline_test)


