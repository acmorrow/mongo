find_package(PythonInterp 2.7 REQUIRED)

add_definitions(-DMONGO_CMAKE_BUILD)
add_definitions(-DMONGO_EXPOSE_MACROS)

# TODO(acm): Should be a configure test, or a Find...
add_definitions(-DMONGO_HAVE_EXECINFO_BACKTRACE)

macro(mongo_cxx_unittest suite testname)
  add_mongo_executable(${testname}
    ${testname}.cpp
  )
  target_link_libraries(${testname}
    mongo-unittest-main
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
    mongo-unittest-crutch
  )
endmacro()

# NOTE: Keep these alphabetized
add_subdirectory(base)
add_subdirectory(bson)
add_subdirectory(client)
add_subdirectory(db)
add_subdirectory(dbtests)
add_subdirectory(util)
add_subdirectory(logger)
add_subdirectory(platform)
add_subdirectory(unittest)

file (GLOB ch_files
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)
source_group(mongo FILES ${ch_files})
