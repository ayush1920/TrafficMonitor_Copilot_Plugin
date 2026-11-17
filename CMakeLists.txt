cmake_minimum_required(VERSION 3.21)

set(COPILOT_USAGE_VERSION "1.0.0" CACHE STRING "CopilotUsage plugin version")

project(CopilotUsage VERSION "${COPILOT_USAGE_VERSION}" LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/CopilotUsageVersion.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/CopilotUsageVersion.h"
    @ONLY
)

set(TRAFFICMONITOR_INCLUDE_DIR "" CACHE PATH "Directory containing TrafficMonitor PluginInterface.h")

if(NOT TRAFFICMONITOR_INCLUDE_DIR)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include/PluginInterface.h")
        set(TRAFFICMONITOR_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../TrafficMonitor_repo/include/PluginInterface.h")
        set(TRAFFICMONITOR_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../TrafficMonitor_repo/include")
    else()
        message(FATAL_ERROR "PluginInterface.h not found. Set TRAFFICMONITOR_INCLUDE_DIR.")
    endif()
endif()

add_library(CopilotUsageCore STATIC
    src/CopilotUsageCore.cpp
    src/JsonLite.cpp
    src/CopilotUsageConfig.cpp
    src/CopilotUsageFetcher.cpp
)

target_include_directories(CopilotUsageCore PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

target_compile_definitions(CopilotUsageCore PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

if(MSVC)
    target_compile_options(CopilotUsageCore PRIVATE /utf-8)
endif()

target_link_libraries(CopilotUsageCore PUBLIC
    advapi32
    shlwapi
    winhttp
)

add_library(CopilotUsage SHARED
    src/CopilotUsagePlugin.cpp
    src/CopilotUsageOptions.cpp
    src/CopilotUsage.rc
)

target_include_directories(CopilotUsage PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
    "${TRAFFICMONITOR_INCLUDE_DIR}"
)

target_compile_definitions(CopilotUsage PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

if(MSVC)
    target_compile_options(CopilotUsage PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
endif()

target_link_libraries(CopilotUsage PRIVATE
    CopilotUsageCore
    advapi32
    comdlg32
    shlwapi
    winhttp
)

set_target_properties(CopilotUsage PROPERTIES
    PREFIX ""
    OUTPUT_NAME "CopilotUsage"
)

add_executable(CopilotUsageTests
    tests/CopilotUsageTests.cpp
)

target_compile_definitions(CopilotUsageTests PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

if(MSVC)
    target_compile_options(CopilotUsageTests PRIVATE /utf-8)
endif()

target_link_libraries(CopilotUsageTests PRIVATE
    CopilotUsageCore
)

enable_testing()
add_test(NAME CopilotUsageTests COMMAND CopilotUsageTests)
