
add_library(mongo_thirdpary_s2_util_math
  mathutil.cc
)

file (GLOB ch_files
    ${CMAKE_CURRENT_SOURCE_DIR}/mathutil.*
)
source_group(mongo_thirdpary_s2_util_math ch_files)
