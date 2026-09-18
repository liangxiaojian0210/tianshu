# =============================================================================
# TIANSHU tests CMake build (top-level)
# =============================================================================

# GCC's optimizer-driven -Wnull-dereference misfires on gtest macro
# expansion paths at -O3 (e.g. EXPECT_FALSE on intentionally-corrupt
# buffers, pointer returns it cannot prove non-null). Test code
# deliberately exercises null/degenerate paths; the diagnostic adds no
# value here while -Werror makes it fatal.
add_compile_options(-Wno-null-dereference)

add_executable(hello_test hello_test.cc)
target_link_libraries(hello_test PRIVATE tianshu GTest::gtest_main)
gtest_discover_tests(hello_test)

add_subdirectory(base)
add_subdirectory(cli)
add_subdirectory(compiler)
add_subdirectory(core)
add_subdirectory(dsl)
add_subdirectory(sched)
add_subdirectory(shm)
add_subdirectory(transport)
