cmake_minimum_required(VERSION 3.0)

project(mongodb)

set(mongodb_version_major 2)
set(mongodb_version_minor 7)
set(mongodb_version_patch 3)
set(mongodb_version_extra -pre)

set (CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_SOURCE_DIR}/lib/cmake)

# TODO: -Wl,--whole-archive on non OS X
set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-all_load")

enable_testing()
add_subdirectory(src)

# TODO: http://cmake.3232098.n2.nabble.com/Linking-an-entire-static-library-td7581572.html
# NOTE: http://voices.canonical.com/jussi.pakkanen/2013/03/26/a-list-of-common-cmake-antipatterns/
