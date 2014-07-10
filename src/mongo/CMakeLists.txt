find_package (Boost COMPONENTS filesystem program_options system thread REQUIRED)

find_library(PCRECPP_LIBRARY
  NAMES pcrecpp
)

add_definitions(-DMONGO_CMAKE_BUILD)
add_definitions(-DMONGO_EXPOSE_MACROS)

# TODO(acm): Should be a configure test, or a Find...
add_definitions(-DMONGO_HAVE_EXECINFO_BACKTRACE)

# TODO(acm): Need a way to pass in auxiliary include and library search paths
set(CMAKE_LIBRARY_PATH "${CMAKE_LIBRARY_PATH} /opt/local/lib")

include_directories(${Boost_INCLUDE_DIR})
include_directories(${PROJECT_SOURCE_DIR}/src)
include_directories(${PROJECT_BINARY_DIR}/src)

macro(mongo_cxx_unittest suite testname)
  add_executable(${testname}
    ${testname}.cpp
  )
  target_link_libraries(${testname}
    mongo_unittest_main
    ${Boost_LIBRARIES}
  )
  add_test(
    NAME ${suite}/${testname}
    COMMAND ${testname}
  )
endmacro()

macro(mongo_cxx_unittest_crutch suite testname)
  mongo_cxx_unittest(${suite} ${testname})
  target_link_libraries(${testname}
    mongo_unittest_crutch
    )
endmacro()

# NOTE: Keep these alphabetized
add_subdirectory(base)
add_subdirectory(bson)
#add_subdirectory(client)
add_subdirectory(db)
add_subdirectory(util)
add_subdirectory(logger)
add_subdirectory(platform)
add_subdirectory(unittest)

file (GLOB ch_files
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)
source_group(mongo ch_files)
