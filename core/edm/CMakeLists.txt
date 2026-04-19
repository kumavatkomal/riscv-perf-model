project(edm)
include(FetchContent)

set(SUPPORTED_BACKENDS "pegasus" "whisper")

if(DEFAULT_BACKEND AND NOT DEFAULT_BACKEND IN_LIST SUPPORTED_BACKENDS AND NOT DEFAULT_BACKEND STREQUAL "all")
    message(FATAL_ERROR "Backend '${DEFAULT_BACKEND}' not found. Try: ${SUPPORTED_BACKENDS} or 'all'")
endif()

set(EDM_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/EDMFactory.cpp)

## pegasus
if(DEFAULT_BACKEND STREQUAL "all" OR DEFAULT_BACKEND STREQUAL "pegasus" OR NOT DEFAULT_BACKEND)

    set(PEGASUS_ADAPTER_DIR ${CMAKE_SOURCE_DIR}/core/edm/adapters/Pegasus)
    set(PEGASUS_SOURCE_DIR ${PEGASUS_ADAPTER_DIR}/pegasus)

    message(STATUS "EDM: Fetching Pegasus into ${CMAKE_SOURCE_DIR}/core/edm/adapters/Pegasus/pegasus")

    set(PEGASUS_USE_PARENT_MAVIS    TRUE CACHE BOOL "" FORCE)
    set(PEGASUS_USE_PARENT_STF_LIB  TRUE CACHE BOOL "" FORCE)
    set(FetchContent_Quiet FALSE)

    FetchContent_Declare(pegasus
        GIT_REPOSITORY https://github.com/sparcians/pegasus.git
        GIT_TAG        main
        GIT_SHALLOW    TRUE
        USES_TERMINAL_DOWNLOAD TRUE
        USES_TERMINAL_UPDATE   TRUE
        SOURCE_DIR  ${CMAKE_SOURCE_DIR}/core/edm/adapters/Pegasus/pegasus
    )

    FetchContent_GetProperties(pegasus)
    if(NOT pegasus_POPULATED)
        FetchContent_Populate(pegasus)
        message(STATUS "EDM: Pegasus source dir: ${pegasus_SOURCE_DIR}")
        include("${pegasus_SOURCE_DIR}/stf_lib/cmake/lto.cmake")
        add_subdirectory(${pegasus_SOURCE_DIR} ${pegasus_BINARY_DIR})
    endif()

    list(APPEND EDM_SOURCES ${PEGASUS_ADAPTER_DIR}/Pegasus.cpp)
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

    # list(APPEND EDM_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/adapters/Whisper/Whisper.cpp)
endif()

add_library(edm STATIC ${EDM_SOURCES})

target_compile_features(edm PUBLIC cxx_std_17)
target_include_directories(edm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

## pegasus
if(DEFAULT_BACKEND STREQUAL "all" OR DEFAULT_BACKEND STREQUAL "pegasus" OR NOT DEFAULT_BACKEND)
    target_include_directories(edm PUBLIC
        ${PEGASUS_ADAPTER_DIR}
        ${PEGASUS_SOURCE_DIR}
        ${PEGASUS_SOURCE_DIR}/cosim
        ${PEGASUS_SOURCE_DIR}/include
    )
    target_link_libraries(edm PUBLIC pegasuslibs)
    target_compile_definitions(edm PUBLIC PEGASUS_AVAILABLE)
endif()

## whisper
if(DEFAULT_BACKEND STREQUAL "all" OR DEFAULT_BACKEND STREQUAL "whisper" OR NOT DEFAULT_BACKEND)
    target_link_libraries(edm PUBLIC whisper)
    target_compile_definitions(edm PUBLIC WHISPER_AVAILABLE)
endif()
