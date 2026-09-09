execute_process(
    COMMAND "${EXAMPLE_EXECUTABLE}"
    RESULT_VARIABLE example_result
    OUTPUT_VARIABLE example_output
    ERROR_VARIABLE example_error
    OUTPUT_STRIP_TRAILING_WHITESPACE
    TIMEOUT 25
)

if(NOT "${example_result}" STREQUAL "0")
    message(FATAL_ERROR "Example failed (${example_result}): ${example_error}")
endif()

if(NOT "${example_output}" STREQUAL "tos example ready")
    message(FATAL_ERROR "Unexpected example output: ${example_output}")
endif()
