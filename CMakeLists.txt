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

find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto)
find_package(Threads REQUIRED)

add_library(
    tosbase
    STATIC
    src/base/crypto.cpp
    src/base/filesystem.cpp
    src/base/logging.cpp
    src/base/process.cpp
    src/base/registry.cpp
    src/base/scheduler.cpp
    src/base/thread_pool.cpp)
add_library(tos::base ALIAS tosbase)
target_include_directories(tosbase PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(tosbase PUBLIC cxx_std_17)
target_link_libraries(tosbase PUBLIC OpenSSL::Crypto Threads::Threads)
if(WIN32)
    target_link_libraries(tosbase PUBLIC Advapi32)
endif()

add_library(
    tosapp
    STATIC
    src/app/app.cpp
    src/app/context.cpp
    src/app/debug.cpp
    src/app/event_bus.cpp
    src/app/module_registry.cpp
    src/app/service_registry.cpp)
add_library(tos::app ALIAS tosapp)
target_include_directories(tosapp PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(tosapp PUBLIC cxx_std_17)
target_link_libraries(tosapp PUBLIC tos::base)

include(CTest)

add_subdirectory(third_party)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

if(TOS_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
