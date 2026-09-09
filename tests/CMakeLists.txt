add_executable(tos_unit_tests smoke.cpp)
target_link_libraries(tos_unit_tests PRIVATE tos::tos GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(
    tos_unit_tests
    TEST_PREFIX tos.
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 30
    PROPERTIES LABELS unit TIMEOUT 30
)
