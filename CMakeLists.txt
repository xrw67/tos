cmake_minimum_required(VERSION 3.24)

project(tos LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_TESTING "Build tests" ${PROJECT_IS_TOP_LEVEL})
option(TOS_BUILD_EXAMPLES "Build tos examples" ${PROJECT_IS_TOP_LEVEL})

add_library(tos INTERFACE)
add_library(tos::tos ALIAS tos)
target_include_directories(tos INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(tos INTERFACE cxx_std_17)

include(CTest)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

if(TOS_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
