
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
add_subdirectory(src)

file (GLOB ch_files
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
)
source_group(mongo_thirdparty_yaml ch_files)
