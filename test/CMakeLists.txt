include_directories(../include ../src)

set(TEST_SOURCES
    stress.c
    queue.c
)

pll_add_subproject(criterion
    PATH test/criterion
    OPTS -DCTESTS=OFF
         -DDEV_BUILD=ON
         -DI18N=OFF
         -DLANG_CXX=OFF
    CMAKE)

add_executable(paralull_unit_tests ${TEST_SOURCES})
target_link_libraries(paralull_unit_tests paralull)

pll_link_subproject(paralull_unit_tests criterion SHARED)

add_test(paralull_unit_tests paralull_unit_tests)
