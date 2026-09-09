if(BUILD_TESTING)
    set(BUILD_GMOCK OFF)
    set(INSTALL_GTEST OFF)
    if(MSVC)
        # Keep GoogleTest on the same runtime as the consuming test executable.
        set(gtest_force_shared_crt ON)
    endif()

    add_subdirectory(googletest EXCLUDE_FROM_ALL)
endif()
