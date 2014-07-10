
add_subdirectory(murmurhash3)
add_subdirectory(yaml-cpp-0.5.1)
add_subdirectory(snappy)
add_subdirectory(pcre-8.30)
add_subdirectory(s2)
# add_subdirectory(libstemmer_c)

add_library(
  mongo_thirdparty_shim_yaml
  shim_yaml.cpp
)

target_link_libraries(
  mongo_thirdparty_shim_yaml
  mongo_thirdparty_yaml
)

add_library(
  mongo_thirdparty_shim_snappy
  shim_snappy.cpp
)

target_link_libraries(
  mongo_thirdparty_shim_snappy
  mongo_thirdparty_snappy
)

# add_library(
#   mongo_thirdparty_shim_stemmer
#   shim_stemmer.cpp
# )

add_library(
  mongo_thirdparty_shim_pcrecpp
  shim_pcrecpp.cc
)

target_link_libraries(
  mongo_thirdparty_shim_pcrecpp
  mongo_thirdparty_pcrecpp
)
