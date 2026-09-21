add_executable(tos_unit_tests
    base_test.cpp
    command_line_test.cpp
    certificate_test.cpp
    http_test.cpp
    status_test.cpp
    result_test.cpp
    time_test.cpp
    config_test.cpp
    environment_test.cpp
    nlohmann_json_test.cpp
    fkyaml_test.cpp
    fmt_test.cpp
    span_test.cpp
    string_test.cpp
    system_test.cpp
    random_test.cpp
    process_test.cpp
    registry_test.cpp
    strconv_test.cpp
    filesystem_test.cpp
    crypto_test.cpp
    dynamic_library_test.cpp
    logging_test.cpp
    network_test.cpp
    scheduler_test.cpp
    thread_pool_test.cpp
    application_test.cpp
    debug_test.cpp
    event_bus_test.cpp
)

# Each public base header must compile without relying on another header being included first.
file(GLOB TOS_BASE_PUBLIC_HEADERS CONFIGURE_DEPENDS
     RELATIVE "${PROJECT_SOURCE_DIR}/include/tos/base"
     "${PROJECT_SOURCE_DIR}/include/tos/base/*.h")

set(tos_public_header_targets)
set(tos_public_header_directory "${CMAKE_CURRENT_BINARY_DIR}/public_headers")
file(MAKE_DIRECTORY "${tos_public_header_directory}")
foreach(header IN LISTS TOS_BASE_PUBLIC_HEADERS)
    get_filename_component(header_name "${header}" NAME_WE)
    set(header_source "${tos_public_header_directory}/${header_name}.cpp")
    file(WRITE "${header_source}" "#include <tos/base/${header}>\nint main() { return 0; }\n")
    add_library(tos_public_header_${header_name} OBJECT EXCLUDE_FROM_ALL "${header_source}")
    target_link_libraries(tos_public_header_${header_name} PRIVATE tos::base)
    list(APPEND tos_public_header_targets tos_public_header_${header_name})
endforeach()
add_custom_target(tos_public_headers ALL DEPENDS ${tos_public_header_targets})

add_library(tos_dynamic_library_test_module SHARED dynamic_library_test_module.cpp)

add_library(tos_dynamic_module_test_module SHARED dynamic_module_test_module.cpp)
add_library(tos_dynamic_module_null_test_module SHARED dynamic_module_null_test_module.cpp)
foreach(target tos_dynamic_module_test_module tos_dynamic_module_null_test_module)
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/include")
    target_compile_features(${target} PRIVATE cxx_std_17)
endforeach()

add_executable(tos_process_test_helper process_test_helper.cpp)
target_link_libraries(tos_process_test_helper PRIVATE tos::base)

add_subdirectory(app_consumer)

target_link_libraries(
    tos_unit_tests
    PRIVATE tos::app GTest::gtest_main)
if(WIN32)
    target_link_libraries(tos_unit_tests PRIVATE Ws2_32)
endif()
target_compile_definitions(
    tos_unit_tests
    PRIVATE TOS_DYNAMIC_LIBRARY_TEST_MODULE="$<TARGET_FILE:tos_dynamic_library_test_module>"
            TOS_DYNAMIC_MODULE_TEST_MODULE="$<TARGET_FILE:tos_dynamic_module_test_module>"
            TOS_DYNAMIC_MODULE_NULL_TEST_MODULE="$<TARGET_FILE:tos_dynamic_module_null_test_module>"
            TOS_PROCESS_TEST_HELPER="$<TARGET_FILE:tos_process_test_helper>")
add_dependencies(
    tos_unit_tests
    tos_dynamic_library_test_module
    tos_dynamic_module_test_module
    tos_dynamic_module_null_test_module
    tos_process_test_helper)

add_test(NAME tos.ApplicationConsumer COMMAND tos_app_consumer)
set_tests_properties(tos.ApplicationConsumer PROPERTIES LABELS unit TIMEOUT 30)

# A separate process is needed to observe static destruction after main returns.
add_executable(tos_result_shutdown_test result_shutdown_test.cpp)
target_link_libraries(tos_result_shutdown_test PRIVATE tos::base)
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
target_link_libraries(tos_allocation_tests PRIVATE tos::base GTest::gtest_main)
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
