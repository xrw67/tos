add_executable(tos_minimal minimal.cpp)
target_link_libraries(tos_minimal PRIVATE tos::base)

add_executable(tos_logging_example logging.cpp)
target_link_libraries(tos_logging_example PRIVATE tos::base)

add_executable(tos_process_example process.cpp)
target_link_libraries(tos_process_example PRIVATE tos::base)

add_executable(tos_application_example application.cpp)
target_link_libraries(tos_application_example PRIVATE tos::app)

if(BUILD_TESTING)
    add_test(
        NAME tos.example
        COMMAND "${CMAKE_COMMAND}"
            "-DEXAMPLE_EXECUTABLE=$<TARGET_FILE:tos_minimal>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/../tests/check_example.cmake"
    )
    set_tests_properties(tos.example PROPERTIES LABELS example TIMEOUT 30)

    add_test(NAME tos.logging_example COMMAND tos_logging_example)
    set_tests_properties(tos.logging_example PROPERTIES LABELS example TIMEOUT 30)

    add_test(NAME tos.process_example COMMAND tos_process_example)
    set_tests_properties(tos.process_example PROPERTIES LABELS example TIMEOUT 30)

    add_test(NAME tos.application_example COMMAND tos_application_example)
    set_tests_properties(tos.application_example PROPERTIES LABELS example TIMEOUT 30)
endif()
