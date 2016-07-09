cmake_minimum_required (VERSION 2.8)

project (Paralull C)

set (MODULE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.cmake/Modules")
set (CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${MODULE_DIR})

set (CMAKE_C_FLAGS_DEFAULT "${CMAKE_C_FLAGS}")
set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99 -Wall -Wextra -pedantic -pthread")

include_directories(include src)
add_subdirectory (src)

include (Subprojects)

add_library (paralull SHARED ${SOURCE_FILES})
target_link_libraries (paralull pthread)

enable_testing()
add_subdirectory(test)
