cmake_minimum_required(VERSION 3.24)
project(tos VERSION 0.1.0 LANGUAGES CXX)

option(BUILD_TESTING "Build tests" ${PROJECT_IS_TOP_LEVEL})
option(TOS_BUILD_EXAMPLES "Build tos examples" ${PROJECT_IS_TOP_LEVEL})
option(TOS_ENABLE_CRYPTO "Build crypto and certificate support (OpenSSL 3)" OFF)
option(TOS_ENABLE_HTTP "Build HTTP support (libcurl)" OFF)
option(TOS_ENABLE_APP "Build the Application framework" ON)
option(TOS_INSTALL "Generate tos installation rules" ${PROJECT_IS_TOP_LEVEL})

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Sanitizers.cmake")

include(GNUInstallDirs)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/TosCURL.cmake")
find_package(Threads REQUIRED)

# Private diagnostics must not change a parent's flags or a consumer's build policy.
function(tos_target_defaults target)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
    tos_instrument_target(${target})
    if(MSVC)
        target_compile_options(${target} PRIVATE /W3 /WX /MP)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS _SCL_SECURE_NO_WARNINGS)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
    endif()
endfunction()

function(tos_component target component)
    add_library(tos::${component} ALIAS ${target})
    set_target_properties(${target} PROPERTIES EXPORT_NAME ${component})
    target_include_directories(${target} PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
    target_compile_features(${target} PUBLIC cxx_std_17)
    tos_target_defaults(${target})
endfunction()

add_library(tosbase STATIC
    src/base/base64.cpp
    src/base/atomic_file_writer.cpp
    src/base/dynamic_library.cpp
    src/base/environment.cpp
    src/base/file_lock.cpp
    src/base/filesystem.cpp
    src/base/logging.cpp
    src/base/network.cpp
    src/base/process.cpp
    src/base/registry.cpp
    src/base/secure_random.cpp
    src/base/scheduler.cpp
    src/base/system.cpp
    src/base/termination.cpp
    src/base/thread_pool.cpp
    src/base/uuid.cpp)
tos_component(tosbase base)
target_link_libraries(tosbase PUBLIC Threads::Threads PRIVATE ${CMAKE_DL_LIBS})
if(WIN32)
    target_link_libraries(tosbase PRIVATE Advapi32 Bcrypt Kernel32 Ws2_32)
    target_compile_definitions(tosbase PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(tosbase PUBLIC /utf-8)
    endif()
endif()

set(tos_components base)
if(TOS_ENABLE_CRYPTO)
    find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto)
    add_library(toscrypto STATIC src/base/crypto.cpp src/base/certificate.cpp)
    tos_component(toscrypto crypto)
    target_link_libraries(toscrypto PUBLIC tos::base PRIVATE OpenSSL::Crypto)
    list(APPEND tos_components crypto)
endif()

if(TOS_ENABLE_HTTP)
    tos_find_curl()
    if(NOT CURL_FOUND)
        message(FATAL_ERROR "TOS_ENABLE_HTTP requires libcurl development files (CURL::libcurl)")
    endif()
    add_library(toshttp STATIC src/base/http.cpp)
    tos_component(toshttp http)
    target_link_libraries(toshttp PUBLIC tos::base PRIVATE CURL::libcurl)
    list(APPEND tos_components http)
endif()

if(TOS_ENABLE_APP)
    add_library(tosapp STATIC
        src/app/app.cpp
        src/app/context.cpp
        src/app/debug.cpp
        src/app/event_bus.cpp
        src/app/module_registry.cpp
        src/app/service_registry.cpp)
    tos_component(tosapp app)
    target_link_libraries(tosapp PUBLIC tos::base)
    list(APPEND tos_components app)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckDependencies.cmake")
if(TOS_INSTALL)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Install.cmake")
endif()

include(CTest)
add_subdirectory(third_party)
if(BUILD_TESTING)
    tos_instrument_target(gtest)
    tos_instrument_target(gtest_main)
    add_subdirectory(tests)
endif()
if(TOS_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
