add_executable(ti_info_calibrate_test ti_info_calibrate_test.cc)
target_link_libraries(ti_info_calibrate_test PRIVATE tianshu GTest::gtest_main)
gtest_discover_tests(ti_info_calibrate_test PROPERTIES ENVIRONMENT "TI_BIN_DIR=${CMAKE_BINARY_DIR}/bin")
