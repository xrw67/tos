add_executable(tos_minimal minimal.cpp)
target_link_libraries(tos_minimal PRIVATE tos::tos)

if(BUILD_TESTING)
    add_test(
        NAME tos.example
        COMMAND "${CMAKE_COMMAND}"
            "-DEXAMPLE_EXECUTABLE=$<TARGET_FILE:tos_minimal>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/../tests/check_example.cmake"
    )
    set_tests_properties(tos.example PROPERTIES LABELS example TIMEOUT 30)
endif()
