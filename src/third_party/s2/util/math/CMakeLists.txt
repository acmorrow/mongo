
add_mongo_library(mongo-thirdparty-s2-util-math
  mathutil.cc
)

file (GLOB ch_files
    ${CMAKE_CURRENT_SOURCE_DIR}/mathutil.*
)

source_group(third_party\\s2\\util\\math FILES ${ch_files})
