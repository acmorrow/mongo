find_package (Boost COMPONENTS filesystem program_options system thread REQUIRED)
include_directories(${Boost_INCLUDE_DIR})

include_directories(third_party/yaml-cpp-0.5.1/include)
include_directories(third_party/s2)
include_directories(third_party/gflags-2.0)
include_directories(third_party/pcre-8.30)
include_directories(${PROJECT_SOURCE_DIR}/src)
include_directories(${PROJECT_BINARY_DIR}/src)

add_definitions(-DMONGO_EXPOSE_MACROS)

add_custom_target(mongo_generate)

function(add_mongo_library library)
  add_library(${library} ${ARGN})
  add_dependencies(${library} mongo_generate)
endfunction()

function(add_mongo_executable executable)
  add_executable(${executable} ${ARGN})
  add_dependencies(${executable} mongo_generate)
endfunction()

add_subdirectory(third_party)
add_subdirectory(mongo)
