cmake_minimum_required(VERSION 3.0)

project(mongodb)

set(mongodb_version_major 2)
set(mongodb_version_minor 7)
set(mongodb_version_patch 3)
set(mongodb_version_extra -pre)

set (CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_SOURCE_DIR}/lib/cmake)

if(WIN32)
  add_compile_options(
    /EHsc
    /W3
    /wd4355
    /wd4800
    /wd4267
    /wd4244
    /wd4290
    /wd4099
  )

  add_definitions(
    /D_CONSOLE
    /D_CRT_SECURE_NO_WARNINGS
    /D_UNICODE
    /DUNICODE
  )
else()
  set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-all_load")

  add_compile_options(
    -Wno-deprecated-declarations
  )
endif()

enable_testing()
add_subdirectory(src)

# TODO: http://cmake.3232098.n2.nabble.com/Linking-an-entire-static-library-td7581572.html
# NOTE: http://voices.canonical.com/jussi.pakkanen/2013/03/26/a-list-of-common-cmake-antipatterns/
