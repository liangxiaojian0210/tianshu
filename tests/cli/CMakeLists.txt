add_executable(ti_info_calibrate_test ti_info_calibrate_test.cc)
target_link_libraries(ti_info_calibrate_test PRIVATE tianshu GTest::gtest_main)
gtest_discover_tests(ti_info_calibrate_test PROPERTIES ENVIRONMENT "TI_BIN_DIR=${CMAKE_BINARY_DIR}/bin")

add_executable(ti_compile_test ti_compile_test.cc)
target_link_libraries(ti_compile_test PRIVATE tianshu GTest::gtest_main)
gtest_discover_tests(ti_compile_test PROPERTIES ENVIRONMENT "TI_BIN_DIR=${CMAKE_BINARY_DIR}/bin")

# Provider .so registering test flows at static init; ti-compile/ti-launch
# find it through --flows dlopen. Output next to the binaries so the
# TI_BIN_DIR env resolves both.
add_library(ti_compile_test_flows SHARED ti_compile_test_flows.cc)
target_link_libraries(ti_compile_test_flows PRIVATE tianshu)
set_target_properties(ti_compile_test_flows PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
add_dependencies(ti_compile_test ti_compile_test_flows ti-compile)

add_executable(ti_launch_flow_test ti_launch_flow_test.cc)
target_link_libraries(ti_launch_flow_test PRIVATE tianshu GTest::gtest_main)
gtest_discover_tests(ti_launch_flow_test PROPERTIES ENVIRONMENT "TI_BIN_DIR=${CMAKE_BINARY_DIR}/bin")

add_library(ti_launch_flow_test_flows SHARED ti_launch_flow_test_flows.cc)
target_link_libraries(ti_launch_flow_test_flows PRIVATE tianshu)
set_target_properties(ti_launch_flow_test_flows PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
add_dependencies(ti_launch_flow_test ti_launch_flow_test_flows ti-launch)
