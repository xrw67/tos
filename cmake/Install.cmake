include(CMakePackageConfigHelpers)
foreach(directory CMAKE_INSTALL_LIBDIR CMAKE_INSTALL_INCLUDEDIR CMAKE_INSTALL_DATADIR)
    if(IS_ABSOLUTE "${${directory}}")
        message(FATAL_ERROR "${directory} must be relative for a relocatable tos package")
    endif()
endforeach()
set(tos_package_dir "${CMAKE_INSTALL_LIBDIR}/cmake/tos")
foreach(component IN LISTS tos_components)
    install(TARGETS tos${component} EXPORT tos-${component}-targets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    install(EXPORT tos-${component}-targets FILE tos-${component}-targets.cmake
        NAMESPACE tos:: DESTINATION "${tos_package_dir}")
endforeach()
install(DIRECTORY include/tos/base/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/tos/base"
    FILES_MATCHING PATTERN "*.h"
    PATTERN "crypto.h" EXCLUDE PATTERN "certificate.h" EXCLUDE PATTERN "http.h" EXCLUDE)
install(DIRECTORY include/tos/vendor DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/tos")
if(TOS_ENABLE_CRYPTO)
    install(FILES include/tos/base/crypto.h include/tos/base/certificate.h
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/tos/base")
endif()
if(TOS_ENABLE_HTTP)
    install(FILES include/tos/base/http.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/tos/base")
endif()
if(TOS_ENABLE_APP)
    install(DIRECTORY include/tos/app DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/tos"
        FILES_MATCHING PATTERN "*.h")
endif()
install(FILES LICENSE third_party/README.md DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/tos")
configure_package_config_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/tosConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/tosConfig.cmake" INSTALL_DESTINATION "${tos_package_dir}")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/tosConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}" COMPATIBILITY SameMinorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/tosConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/tosConfigVersion.cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/TosCURL.cmake" DESTINATION "${tos_package_dir}")
