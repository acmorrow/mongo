add_library(mongo-thirdparty-yaml
  binary.cpp
  graphbuilder.cpp
  graphbuilderadapter.cpp
  convert.cpp
  directives.cpp
  emit.cpp
  emitfromevents.cpp
  emitter.cpp
  emitterstate.cpp
  emitterutils.cpp
  exp.cpp
  memory.cpp
  node.cpp
  node_data.cpp
  nodebuilder.cpp
  nodeevents.cpp
  null.cpp
  ostream_wrapper.cpp
  parse.cpp
  parser.cpp
  regex.cpp
  scanner.cpp
  scanscalar.cpp
  scantag.cpp
  scantoken.cpp
  simplekey.cpp
  singledocparser.cpp
  stream.cpp
  tag.cpp
)

file (GLOB ch_files
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)

source_group(third_party\\yaml-cpp FILES ${ch_files})
