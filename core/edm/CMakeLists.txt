project(edm)
include(FetchContent)

add_library(edm INTERFACE) ## TODO: this interface is used when there are only .hpp files present in the directory for building
# TODO: Komal : if not done at the time of writing the cpp file/ implementation, change the INTERFACE to STATIC 

target_include_directories(edm INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

set(SUPPORTED_BACKENDS "pegasus" "whisper")

if(DEFAULT_BACKEND AND NOT DEFAULT_BACKEND IN_LIST SUPPORTED_BACKENDS AND NOT DEFAULT_BACKEND STREQUAL "all")
    message(FATAL_ERROR "Backend '${DEFAULT_BACKEND}' not found. Try: ${SUPPORTED_BACKENDS} or 'all'")
endif()

## pegasus
if(DEFAULT_BACKEND STREQUAL "all" OR DEFAULT_BACKEND STREQUAL "pegasus" OR NOT DEFAULT_BACKEND)
    message(STATUS "EDM: Fetching Pegasus into ${CMAKE_BINARY_DIR}/_deps/pegasus-src")
    
    FetchContent_Declare(pegasus
        GIT_REPOSITORY https://github.com/sparcians/pegasus.git
        GIT_TAG        main
        GIT_SHALLOW    TRUE
        USES_TERMINAL_DOWNLOAD TRUE 
        USES_TERMINAL_UPDATE   TRUE
    )
    FetchContent_MakeAvailable(pegasus)
    target_link_libraries(edm INTERFACE pegasus)
    target_compile_definitions(edm INTERFACE PEGASUS_AVAILABLE)
endif()

## whisper
if(DEFAULT_BACKEND STREQUAL "all" OR DEFAULT_BACKEND STREQUAL "whisper" OR NOT DEFAULT_BACKEND)
    message(STATUS "EDM: Fetching Whisper into ${CMAKE_BINARY_DIR}/_deps/whisper-src")
    
    FetchContent_Declare(whisper
        GIT_REPOSITORY https://github.com/tenstorrent/whisper.git
        GIT_TAG        master
        GIT_SHALLOW    TRUE
        GIT_PROGRESS  TRUE
    )
    FetchContent_MakeAvailable(whisper)
    target_link_libraries(edm INTERFACE whisper)
    target_compile_definitions(edm INTERFACE WHISPER_AVAILABLE)
endif()