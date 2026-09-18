# =============================================================================
# TIANSHU benchmarks CMake build
# =============================================================================

add_executable(version_benchmark version_benchmark.cc)
target_link_libraries(version_benchmark PRIVATE tianshu benchmark::benchmark)
target_compile_options(version_benchmark PRIVATE -Wno-c2y-extensions)

add_executable(object_pool_benchmark object_pool_benchmark.cc)
target_link_libraries(object_pool_benchmark PRIVATE tianshu benchmark::benchmark)
target_compile_options(object_pool_benchmark PRIVATE -Wno-c2y-extensions)

add_executable(shm_transport_benchmark shm_transport_benchmark.cc)
target_link_libraries(shm_transport_benchmark PRIVATE tianshu pthread)

add_executable(h3_wcet_rig h3_wcet_rig.cc)
target_link_libraries(h3_wcet_rig PRIVATE tianshu pthread)

add_executable(lineage_benchmark lineage_benchmark.cc)
target_link_libraries(lineage_benchmark PRIVATE tianshu benchmark::benchmark)
target_compile_options(lineage_benchmark PRIVATE -Wno-c2y-extensions)

add_executable(codegen_vs_handwritten codegen_vs_handwritten.cc)
target_link_libraries(codegen_vs_handwritten PRIVATE tianshu tianshu_compiler benchmark::benchmark)
target_compile_options(codegen_vs_handwritten PRIVATE -Wno-c2y-extensions)
