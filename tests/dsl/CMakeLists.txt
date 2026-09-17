add_executable(dsl_test dsl_test.cc)
target_link_libraries(dsl_test PRIVATE tianshu GTest::gtest_main)
gtest_discover_tests(dsl_test)

add_executable(sla_test sla_test.cc)
target_link_libraries(sla_test PRIVATE tianshu GTest::gtest_main)
gtest_discover_tests(sla_test)

add_executable(slice_state_test slice_state_test.cc)
target_link_libraries(slice_state_test PRIVATE tianshu GTest::gtest_main)
gtest_discover_tests(slice_state_test)

add_executable(record_v2_test record_v2_test.cc)
target_link_libraries(record_v2_test PRIVATE tianshu GTest::gtest_main)
gtest_discover_tests(record_v2_test)

add_executable(runtime_coverage_test runtime_coverage_test.cc)
target_link_libraries(runtime_coverage_test PRIVATE tianshu GTest::gtest_main)
gtest_discover_tests(runtime_coverage_test)

add_executable(flow_fallback_test flow_fallback_test.cc)
target_link_libraries(flow_fallback_test PRIVATE tianshu tianshu_compiler GTest::gtest_main)
gtest_discover_tests(flow_fallback_test)

add_executable(specialize_test specialize_test.cc)
target_link_libraries(specialize_test PRIVATE tianshu GTest::gtest_main)
gtest_discover_tests(specialize_test)
