add_executable(tos_header_smoke header_smoke.cpp)
target_link_libraries(tos_header_smoke PRIVATE tos::tos)

add_test(NAME tos.header_smoke COMMAND tos_header_smoke)
set_tests_properties(tos.header_smoke PROPERTIES LABELS smoke TIMEOUT 30)
