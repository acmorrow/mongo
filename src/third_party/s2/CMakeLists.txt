
add_definitions(-DDEBUG_MODE=false)

add_library(mongo_thirdparty_s2
  s1angle.cc
  s2.cc
  s2cellid.cc
  s2latlng.cc
  s1interval.cc
  s2cap.cc
  s2cell.cc
  s2cellunion.cc
  s2edgeindex.cc
  s2edgeutil.cc
  s2latlngrect.cc
  s2loop.cc
  s2pointregion.cc
  s2polygon.cc
  s2polygonbuilder.cc
  s2polyline.cc
  s2r2rect.cc
  s2region.cc
  s2regioncoverer.cc
  s2regionintersection.cc
  s2regionunion.cc
)

target_link_libraries(mongo_thirdparty_s2
  mongo_thirdparty_s2_base
  mongo_thirdparty_s2_strings
  mongo_thirdparty_s2_util_math
  mongo_thirdparty_s2_util_coding
)

file (GLOB ch_files
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)

source_group(mongo_thirdparty_s2
  ch_files
)
