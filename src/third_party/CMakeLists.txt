
add_subdirectory(murmurhash3)
add_subdirectory(yaml-cpp-0.5.1)
add_subdirectory(snappy)
add_subdirectory(pcre-8.30)
add_subdirectory(s2)
# add_subdirectory(libstemmer_c)

add_library(mongo-thirdparty-shim-yaml
  shim_yaml.cpp
)

target_link_libraries(mongo-thirdparty-shim-yaml
  mongo-thirdparty-yaml
)

add_library(mongo-thirdparty-shim-snappy
  shim_snappy.cpp
)

target_link_libraries(mongo-thirdparty-shim-snappy
  mongo-thirdparty-snappy
)

# add_library(
#   mongo_thirdparty_shim_stemmer
#   shim_stemmer.cpp
# )

add_library(mongo-thirdparty-shim-pcrecpp
  shim_pcrecpp.cc
)

target_link_libraries(mongo-thirdparty-shim-pcrecpp
  mongo-thirdparty-pcrecpp
)
