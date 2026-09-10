add_executable(tos_unit_tests
    status_test.cpp
    result_test.cpp
    time_test.cpp
    config_test.cpp
    nlohmann_json_test.cpp
    fkyaml_test.cpp
    fmt_test.cpp
)

target_link_libraries(
    tos_unit_tests
    PRIVATE tos::tos GTest::gtest_main)

# A separate process is needed to observe static destruction after main returns.
add_executable(tos_result_shutdown_test result_shutdown_test.cpp)
target_link_libraries(tos_result_shutdown_test PRIVATE tos::tos)
foreach(scenario moved valueless)
    add_test(NAME tos.ResultShutdown.${scenario}
        COMMAND tos_result_shutdown_test ${scenario})
    set_tests_properties(tos.ResultShutdown.${scenario} PROPERTIES LABELS unit TIMEOUT 30)
endforeach()

include(GoogleTest)
gtest_discover_tests(
    tos_unit_tests
    TEST_PREFIX tos.
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS unit TIMEOUT 30
)

# Keep global allocation hooks out of the regular test executable.
add_executable(tos_allocation_tests status_allocation_test.cpp)
target_link_libraries(tos_allocation_tests PRIVATE tos::tos GTest::gtest_main)
# Global new/delete replacements intentionally use malloc/free in this isolated
# allocation-regression executable. GCC's diagnostic cannot model that pairing.
target_compile_options(
    tos_allocation_tests
    PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-mismatched-new-delete>)
gtest_discover_tests(
    tos_allocation_tests
    TEST_PREFIX tos.
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS unit TIMEOUT 30
)
