include_directories(../include ../src)

add_subdirectory(criterion)
include_directories(criterion/include)

set(TEST_SOURCES
    stress.c
    queue.c
)

add_executable(paralull_unit_tests ${TEST_SOURCES})
target_link_libraries(paralull_unit_tests criterion paralull)
add_test(parallul_unit_tests parallul_unit_tests)
