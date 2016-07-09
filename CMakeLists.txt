cmake_minimum_required (VERSION 2.8)

project (Paralull C)

include_directories(include src)
add_subdirectory (src)

add_library (paralull SHARED ${SOURCE_FILES})

enable_testing()
add_subdirectory(test)
