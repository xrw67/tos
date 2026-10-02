# Invalid cleanup types must be rejected by the independently includable public header.
# Function scope prevents the probe's target type from leaking into parent checks.
function(tos_check_scope_exit_constraints)
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
    foreach(scenario throwing_move throwing_destructor non_callable)
        set(callback "struct Callback { Callback() = default; Callback(Callback&&) noexcept = default; ~Callback() noexcept = default; void operator()() {} };\n")
        if(scenario STREQUAL "throwing_move")
            string(REPLACE "Callback(Callback&&) noexcept = default" "Callback(Callback&&) noexcept(false) {}" callback "${callback}")
        elseif(scenario STREQUAL "throwing_destructor")
            string(REPLACE "~Callback() noexcept = default" "~Callback() noexcept(false) {}" callback "${callback}")
        else()
            string(REPLACE "void operator()() {}" "" callback "${callback}")
        endif()
        set(probe "${CMAKE_CURRENT_BINARY_DIR}/scope_exit_${scenario}.cpp")
        file(WRITE "${probe}" "#include \"${PROJECT_SOURCE_DIR}/include/tos/base/scope_exit.h\"\n${callback}void probe() { tos::ScopeExit guard(Callback{}); }\n")
        try_compile(accepted "${CMAKE_CURRENT_BINARY_DIR}/scope_exit_${scenario}"
            "${probe}" CMAKE_FLAGS "-DCMAKE_CXX_STANDARD=17" OUTPUT_VARIABLE diagnostics)
        if(accepted)
            message(FATAL_ERROR "ScopeExit accepted invalid callback: ${scenario}")
        endif()
        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/scope_exit_${scenario}.log" "${diagnostics}")
    endforeach()
endfunction()
tos_check_scope_exit_constraints()

add_executable(tos_unit_tests
    scope_exit_test.cpp
    native_resource_test.cpp
    base_test.cpp
    cli11_test.cpp
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
    dynamic_library_test.cpp
    logging_test.cpp
    network_test.cpp
    platform_primitives_test.cpp
    scheduler_test.cpp
    thread_pool_test.cpp
)

target_link_libraries(tos_unit_tests PRIVATE tos::base GTest::gtest_main)
target_include_directories(tos_unit_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/base")
tos_target_defaults(tos_unit_tests)
if(TOS_ENABLE_CRYPTO)
    target_sources(tos_unit_tests PRIVATE crypto_test.cpp certificate_test.cpp)
    target_link_libraries(tos_unit_tests PRIVATE tos::crypto OpenSSL::Crypto)
endif()
if(TOS_ENABLE_HTTP)
    target_sources(tos_unit_tests PRIVATE http_test.cpp)
    target_link_libraries(tos_unit_tests PRIVATE tos::http)
endif()
if(TOS_ENABLE_APP)
    target_sources(tos_unit_tests PRIVATE application_test.cpp debug_test.cpp event_bus_test.cpp)
    target_link_libraries(tos_unit_tests PRIVATE tos::app)
endif()

# Compile each installed public header in its own translation unit.
file(GLOB TOS_PUBLIC_HEADERS CONFIGURE_DEPENDS
    RELATIVE "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include/tos/base/*.h")
if(NOT TOS_ENABLE_CRYPTO)
    list(REMOVE_ITEM TOS_PUBLIC_HEADERS tos/base/crypto.h tos/base/certificate.h)
endif()
if(NOT TOS_ENABLE_HTTP)
    list(REMOVE_ITEM TOS_PUBLIC_HEADERS tos/base/http.h)
endif()
if(TOS_ENABLE_APP)
    file(GLOB app_headers CONFIGURE_DEPENDS RELATIVE "${PROJECT_SOURCE_DIR}/include"
        "${PROJECT_SOURCE_DIR}/include/tos/app/*.h")
    list(APPEND TOS_PUBLIC_HEADERS ${app_headers})
endif()
set(tos_public_header_targets)
set(tos_public_header_directory "${CMAKE_CURRENT_BINARY_DIR}/public_headers")
file(MAKE_DIRECTORY "${tos_public_header_directory}")
foreach(header IN LISTS TOS_PUBLIC_HEADERS)
    string(REPLACE "/" "_" header_name "${header}")
    set(header_source "${tos_public_header_directory}/${header_name}.cpp")
    file(WRITE "${header_source}" "#include <${header}>\nint main() { return 0; }\n")
    add_library(tos_public_header_${header_name} OBJECT EXCLUDE_FROM_ALL "${header_source}")
    target_link_libraries(tos_public_header_${header_name} PRIVATE tos::base)
    tos_target_defaults(tos_public_header_${header_name})
    list(APPEND tos_public_header_targets tos_public_header_${header_name})
endforeach()
add_custom_target(tos_public_headers ALL DEPENDS ${tos_public_header_targets})

add_library(tos_dynamic_library_test_module SHARED dynamic_library_test_module.cpp)
add_library(tos_dynamic_library_lifetime_module SHARED dynamic_library_lifetime_module.cpp)
target_compile_features(tos_dynamic_library_lifetime_module PRIVATE cxx_std_17)
tos_target_defaults(tos_dynamic_library_lifetime_module)
add_executable(tos_dynamic_library_lifetime_helper dynamic_library_lifetime_helper.cpp)
target_link_libraries(tos_dynamic_library_lifetime_helper PRIVATE tos::base)
tos_target_defaults(tos_dynamic_library_lifetime_helper)
target_compile_definitions(tos_unit_tests PRIVATE
    TOS_DYNAMIC_LIBRARY_LIFETIME_HELPER="$<TARGET_FILE:tos_dynamic_library_lifetime_helper>"
    TOS_DYNAMIC_LIBRARY_LIFETIME_MODULE="$<TARGET_FILE:tos_dynamic_library_lifetime_module>")
add_dependencies(tos_unit_tests tos_dynamic_library_lifetime_helper tos_dynamic_library_lifetime_module)

add_executable(tos_resource_test_helper resource_test_helper.cpp)
target_link_libraries(tos_resource_test_helper PRIVATE tos::base)
target_include_directories(tos_resource_test_helper PRIVATE "${PROJECT_SOURCE_DIR}/src/base")
tos_target_defaults(tos_resource_test_helper)
target_compile_definitions(tos_unit_tests PRIVATE
    TOS_RESOURCE_TEST_HELPER="$<TARGET_FILE:tos_resource_test_helper>")
add_dependencies(tos_unit_tests tos_resource_test_helper)

add_executable(tos_process_test_helper process_test_helper.cpp)
target_link_libraries(tos_process_test_helper PRIVATE tos::base)
add_executable(tos_platform_primitives_helper platform_primitives_helper.cpp)
target_link_libraries(tos_platform_primitives_helper PRIVATE tos::base)
tos_target_defaults(tos_platform_primitives_helper)
target_compile_definitions(tos_unit_tests PRIVATE
    TOS_PLATFORM_PRIMITIVES_HELPER="$<TARGET_FILE:tos_platform_primitives_helper>")
add_dependencies(tos_unit_tests tos_platform_primitives_helper)

if(WIN32)
    target_link_libraries(tos_unit_tests PRIVATE Ws2_32)
endif()
target_compile_definitions(
    tos_unit_tests
    PRIVATE TOS_DYNAMIC_LIBRARY_TEST_MODULE="$<TARGET_FILE:tos_dynamic_library_test_module>"
            TOS_PROCESS_TEST_HELPER="$<TARGET_FILE:tos_process_test_helper>")
add_dependencies(
    tos_unit_tests
    tos_dynamic_library_test_module
    tos_process_test_helper)

if(TOS_ENABLE_APP)
    add_library(tos_dynamic_module_test_module SHARED dynamic_module_test_module.cpp)
    add_library(tos_dynamic_module_null_test_module SHARED dynamic_module_null_test_module.cpp)
    foreach(target tos_dynamic_module_test_module tos_dynamic_module_null_test_module)
        target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/include")
        target_compile_features(${target} PRIVATE cxx_std_17)
        tos_target_defaults(${target})
        if(WIN32)
            target_compile_definitions(${target} PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
            if(MSVC)
                target_compile_options(${target} PRIVATE /utf-8)
            endif()
        endif()
    endforeach()

    add_subdirectory(app_consumer)
    tos_target_defaults(tos_app_consumer)
    target_compile_definitions(tos_unit_tests PRIVATE
        TOS_DYNAMIC_MODULE_TEST_MODULE="$<TARGET_FILE:tos_dynamic_module_test_module>"
        TOS_DYNAMIC_MODULE_NULL_TEST_MODULE="$<TARGET_FILE:tos_dynamic_module_null_test_module>")
    add_dependencies(tos_unit_tests tos_dynamic_module_test_module tos_dynamic_module_null_test_module)
    add_test(NAME tos.ApplicationConsumer COMMAND tos_app_consumer)
    set_tests_properties(tos.ApplicationConsumer PROPERTIES LABELS unit TIMEOUT ${TOS_TEST_TIMEOUT})
endif()

# A separate process is needed to observe static destruction after main returns.
add_executable(tos_result_shutdown_test result_shutdown_test.cpp)
target_link_libraries(tos_result_shutdown_test PRIVATE tos::base)
foreach(scenario moved valueless)
    add_test(NAME tos.ResultShutdown.${scenario}
        COMMAND tos_result_shutdown_test ${scenario})
    set_tests_properties(tos.ResultShutdown.${scenario} PROPERTIES LABELS unit TIMEOUT ${TOS_TEST_TIMEOUT})
endforeach()

include(GoogleTest)
gtest_discover_tests(
    tos_unit_tests
    TEST_PREFIX tos.
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT ${TOS_TEST_TIMEOUT}
    PROPERTIES LABELS unit TIMEOUT ${TOS_TEST_TIMEOUT}
)

# Keep global allocation hooks out of the regular test executable.
add_executable(tos_allocation_tests status_allocation_test.cpp)
target_link_libraries(tos_allocation_tests PRIVATE tos::base GTest::gtest_main)
if(TOS_SANITIZER STREQUAL "thread" AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
   CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # LLVM's static TSan runtime defines strong new/delete symbols. The shared runtime permits
    # this executable's intentional replacements while still intercepting their malloc/free.
    execute_process(COMMAND "${CMAKE_CXX_COMPILER}" --print-runtime-dir
        OUTPUT_VARIABLE tos_tsan_runtime_dir OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE tos_tsan_runtime_result)
    if(NOT tos_tsan_runtime_result EQUAL 0 OR NOT IS_DIRECTORY "${tos_tsan_runtime_dir}")
        message(FATAL_ERROR "Cannot locate Clang's shared TSan runtime")
    endif()
    target_link_options(tos_allocation_tests PRIVATE -shared-libsan)
    set_property(TARGET tos_allocation_tests APPEND PROPERTY BUILD_RPATH "${tos_tsan_runtime_dir}")
endif()
# Global new/delete replacements intentionally use malloc/free in this isolated
# allocation-regression executable. GCC's diagnostic cannot model that pairing.
target_compile_options(
    tos_allocation_tests
    PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-mismatched-new-delete>)
gtest_discover_tests(
    tos_allocation_tests
    TEST_PREFIX tos.
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT ${TOS_TEST_TIMEOUT}
    PROPERTIES LABELS unit TIMEOUT ${TOS_TEST_TIMEOUT}
)

foreach(target tos_dynamic_library_test_module tos_process_test_helper tos_result_shutdown_test tos_allocation_tests)
    tos_target_defaults(${target})
endforeach()

# Deliberate faults are run only by verify_sanitizers.py, never by ordinary CTest.
if(NOT TOS_SANITIZER STREQUAL "none")
    add_executable(tos_sanitizer_probe sanitizer_probe.cpp)
    target_link_libraries(tos_sanitizer_probe PRIVATE Threads::Threads)
    tos_target_defaults(tos_sanitizer_probe)
endif()
