add_executable(hello_world hello_world.cc)
target_link_libraries(hello_world PRIVATE tianshu)

add_executable(intra_example intra_example.cc)
target_link_libraries(intra_example PRIVATE tianshu)

add_executable(shm_talker shm_talker.cc)
target_link_libraries(shm_talker PRIVATE tianshu)

add_executable(shm_listener shm_listener.cc)
target_link_libraries(shm_listener PRIVATE tianshu)

add_executable(hello_dag hello_dag.cc)
target_link_libraries(hello_dag PRIVATE tianshu)

add_executable(dsl_demo dsl_demo.cc)
target_link_libraries(dsl_demo PRIVATE tianshu)

add_executable(full_chain_demo full_chain_demo.cc avp_devices.cc)
target_link_libraries(full_chain_demo PRIVATE tianshu)

add_executable(state_recovery_demo state_recovery_demo.cc)
target_link_libraries(state_recovery_demo PRIVATE tianshu)

add_executable(lidar_imu_demo lidar_imu_demo.cc)
target_link_libraries(lidar_imu_demo PRIVATE tianshu)

add_executable(record_replay_demo record_replay_demo.cc)
target_link_libraries(record_replay_demo PRIVATE tianshu)

add_executable(traceable_flow_demo traceable_flow_demo.cc)
target_link_libraries(traceable_flow_demo PRIVATE tianshu tianshu_compiler)

# Provider .so for the PoC demo walkthrough (demo/poc-demo.sh): registers
# demo_traceable at static init; ti-compile/ti-launch find it via --flows.
add_library(traceable_flow_provider SHARED traceable_flow_provider.cc)
target_link_libraries(traceable_flow_provider PRIVATE tianshu)
set_target_properties(traceable_flow_provider PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
