cmake_minimum_required(VERSION 3.24)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

project(tos LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_TESTING "Build tests" ${PROJECT_IS_TOP_LEVEL})
option(TOS_BUILD_EXAMPLES "Build tos examples" ${PROJECT_IS_TOP_LEVEL})

if(MSVC)
    # 使用UNICODE字符集
    add_definitions(-D_UNICODE -DUNICODE)

    # 禁止代码优化
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /Od /DNDEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Od /DNDEBUG")

    # Release也生成pdb文件
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /Zi")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE
        "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE
        "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")

    add_compile_options(
        /utf-8 # 编译时使用UTF-8编码
        /MP # 开启多线程编译
        /W3 # 启用项目既定警告等级
        /WX # 将警告视为错误
        /DNOMINMAX # 禁用标准库提供的max和min
        /DWIN32_LEAN_AND_MEAN # 减少头文件包含
        /D_CRT_SECURE_NO_WARNINGS # 禁用安全函数
        /D_SCL_SECURE_NO_WARNINGS #
    )
endif()

if(LINUX)
    # 设置RPATH: $ORIGIN表示App同级目录
    set(CMAKE_INSTALL_RPATH "$ORIGIN")

    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0 -rdynamic -g")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -rdynamic -g")

    include(ccache) # 加载ccache
endif()

# Keep compiler diagnostics fatal on every supported platform. Linux already uses this policy;
# Apple Clang and other GNU-compatible compilers must receive the same quality gate.
if(MSVC)
    # /W4 and /WX are configured above with the Windows toolchain options.
elseif(CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
    add_compile_options(-Wall -Wextra -Werror)
endif()

add_library(tos INTERFACE)
add_library(tos::tos ALIAS tos)
target_include_directories(tos INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(tos INTERFACE cxx_std_17)

find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto)
find_package(Threads REQUIRED)

add_library(tos_crypto STATIC src/crypto.cpp)
add_library(tos::crypto ALIAS tos_crypto)
target_include_directories(tos_crypto PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(tos_crypto PUBLIC cxx_std_17)
target_link_libraries(tos_crypto PUBLIC OpenSSL::Crypto)

add_library(tos_logging STATIC src/logging.cpp)
add_library(tos::logging ALIAS tos_logging)
target_include_directories(tos_logging PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(tos_logging PUBLIC cxx_std_17)
target_link_libraries(tos_logging PUBLIC Threads::Threads)

add_library(tos_filesystem STATIC src/filesystem.cpp)
add_library(tos::filesystem ALIAS tos_filesystem)
target_include_directories(tos_filesystem PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(tos_filesystem PUBLIC cxx_std_17)

add_library(tos_process STATIC src/process.cpp)
add_library(tos::process ALIAS tos_process)
target_include_directories(tos_process PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(tos_process PUBLIC cxx_std_17)
target_link_libraries(tos_process PUBLIC tos_filesystem Threads::Threads)

# Preserve the existing aggregate consumer target while adding the compiled
# crypto component required by the public crypto API.
target_link_libraries(tos_logging PUBLIC tos_filesystem)
target_link_libraries(tos INTERFACE tos_crypto tos_filesystem tos_logging tos_process)

include(CTest)

add_subdirectory(third_party)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

if(TOS_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
