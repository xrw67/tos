# Config packages preserve static curl's transitive dependencies. System packages may instead
# provide only headers/libraries, in which case use CMake's FindCURL module.
function(tos_find_curl)
    find_package(CURL QUIET CONFIG)
    if(NOT CURL_FOUND)
        find_package(CURL QUIET MODULE)
    endif()
    if(CURL_FOUND AND TARGET CURL::libcurl)
        set(CURL_FOUND TRUE PARENT_SCOPE)
    else()
        set(CURL_FOUND FALSE PARENT_SCOPE)
    endif()
endfunction()
