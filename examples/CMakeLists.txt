add_executable(tos_minimal minimal.cpp)
target_link_libraries(tos_minimal PRIVATE tos::base)

add_executable(tos_logging_example logging.cpp)
target_link_libraries(tos_logging_example PRIVATE tos::base)

add_executable(tos_process_example process.cpp)
target_link_libraries(tos_process_example PRIVATE tos::base)

if(TOS_ENABLE_APP)
    add_executable(tos_application_example application.cpp)
    target_link_libraries(tos_application_example PRIVATE tos::app)

    add_executable(tos_debug_example debugging.cpp)
    target_link_libraries(tos_debug_example PRIVATE tos::app)

endif()

add_executable(tos_scheduler_example scheduler.cpp)
target_link_libraries(tos_scheduler_example PRIVATE tos::base)

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

    if(TOS_ENABLE_APP)
        add_test(NAME tos.application_example COMMAND tos_application_example)
        set_tests_properties(tos.application_example PROPERTIES LABELS example TIMEOUT 30)

        add_test(NAME tos.debug_example COMMAND tos_debug_example)
        set_tests_properties(tos.debug_example PROPERTIES LABELS example TIMEOUT 30)

    endif()

    add_test(NAME tos.scheduler_example COMMAND tos_scheduler_example)
    set_tests_properties(tos.scheduler_example PROPERTIES LABELS example TIMEOUT 30)
endif()

foreach(target tos_minimal tos_logging_example tos_process_example tos_scheduler_example tos_application_example tos_debug_example)
    if(TARGET ${target})
        tos_target_defaults(${target})
    endif()
endforeach()
