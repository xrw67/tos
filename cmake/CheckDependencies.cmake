# Check both source-level includes and CMake links. The crypto/HTTP implementations keep their
# existing source paths, but are not part of base and must never depend on Application.
file(GLOB_RECURSE tos_base_files CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/tos/base/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/base/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/base/*.cpp")
foreach(source IN LISTS tos_base_files)
    file(READ "${source}" contents)
    if(contents MATCHES "#[ \t]*include[ \t]*[<\"]tos/app/")
        message(FATAL_ERROR "Foundation cannot include Application: ${source}")
    endif()
endforeach()
get_target_property(tos_base_sources tosbase SOURCES)
file(GLOB tos_base_headers RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/tos/base/*.h")
list(REMOVE_ITEM tos_base_headers include/tos/base/crypto.h include/tos/base/certificate.h include/tos/base/http.h)
foreach(source IN LISTS tos_base_sources tos_base_headers)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/${source}" contents)
    if(contents MATCHES "#[ \t]*include[ \t]*[<\"]tos/base/(crypto|certificate|http)\\.h")
        message(FATAL_ERROR "base cannot include optional components: ${source}")
    endif()
endforeach()
foreach(component IN LISTS tos_components)
    foreach(property LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
        get_target_property(links tos${component} ${property})
        if(component STREQUAL "base" AND
           links MATCHES "tos(::)?(app|crypto|http)|OpenSSL::|CURL::")
            message(FATAL_ERROR "base has a forbidden dependency: ${links}")
        elseif(component STREQUAL "app" AND links MATCHES "tos(::)?(crypto|http)|OpenSSL::|CURL::")
            message(FATAL_ERROR "app has a forbidden dependency: ${links}")
        elseif(component MATCHES "^(crypto|http)$" AND links MATCHES "tos(::)?app")
            message(FATAL_ERROR "${component} cannot depend on Application")
        endif()
    endforeach()
endforeach()
