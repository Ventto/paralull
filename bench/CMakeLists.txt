include_directories(../include ../src)

add_executable(bench bench.cc)
target_link_libraries(bench paralull lfds benchmark)
