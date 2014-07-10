include_directories(third_party/yaml-cpp-0.5.1/include)
include_directories(third_party/s2)
include_directories(third_party/gflags-2.0)
include_directories(${PROJECT_SOURCE_DIR}/src)
include_directories(${PROJECT_BINARY_DIR}/src)

add_definitions(-DMONGO_EXPOSE_MACROS)

add_subdirectory(third_party)
add_subdirectory(mongo)
