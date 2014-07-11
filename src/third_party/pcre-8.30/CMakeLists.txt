
add_definitions(-DHAVE_CONFIG_H)

add_mongo_library(mongo-thirdparty-pcrecpp
  # pcre
  pcre_byte_order.c
  pcre_compile.c
  pcre_config.c
  pcre_dfa_exec.c
  pcre_exec.c
  pcre_fullinfo.c
  pcre_get.c
  pcre_globals.c
  pcre_maketables.c
  pcre_newline.c
  pcre_ord2utf8.c
  pcre_refcount.c
  pcre_string_utils.c
  pcre_study.c
  pcre_tables.c
  pcre_ucd.c
  pcre_valid_utf8.c
  pcre_version.c
  pcre_xclass.c
  # pcre nodist
  pcre_chartables.c
  # pcre cpp
  pcrecpp.cc
  pcre_scanner.cc
  pcre_stringpiece.cc
  # pcre posix
  pcreposix.c
)

file (GLOB ch_files
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)
source_group(thirdparty\\pcrecpp FILES ${ch_files})
