# =============================================================================
# TIANSHU CLI tools (ti dispatcher + ti-* tools)
# =============================================================================

add_executable(ti ti_main.cc)
target_link_libraries(ti PRIVATE tianshu)

add_executable(ti-launch ti_launch_main.cc)
target_link_libraries(ti-launch PRIVATE tianshu tianshu_compiler)
# Same registry-head unification requirement as ti-compile (--flows providers).
set_target_properties(ti-launch PROPERTIES ENABLE_EXPORTS ON)

add_executable(ti-monitor ti_monitor_main.cc)
target_link_libraries(ti-monitor PRIVATE tianshu)

add_executable(ti-info ti_info_main.cc)
target_link_libraries(ti-info PRIVATE tianshu)

add_executable(ti-compile ti_compile_main.cc)
target_link_libraries(ti-compile PRIVATE tianshu tianshu_compiler)
# The flow registry's head is an inline static-local (GNU unique symbol):
# a dlopened provider's registrations only unify with this process's
# instance if the executable exports its dynamic symbols.
set_target_properties(ti-compile PROPERTIES ENABLE_EXPORTS ON)
