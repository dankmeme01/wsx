cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT CURRENT_CPM_VERSION)
    include(cmake/get_cpm.cmake)
endif()

project(wsx)
option(WSX_ENABLE_TLS "Enable TLS" OFF)
option(WSX_ENABLE_ASYNC "Enable async client api" OFF)
option(WSX_BUILD_MBEDTLS "Build MbedTLS as crypto backend" OFF)
option(WSX_BUNDLE_CACERTS "Bundle CA certificates" ON)
option(WSX_BUILD_EXAMPLES "Build examples" OFF)

file(GLOB SOURCES CONFIGURE_DEPENDS "src/*.cpp")
add_library(wsx STATIC ${SOURCES})
target_include_directories(wsx PUBLIC "include")

if (WIN32)
    target_compile_definitions(wsx PUBLIC NOMINMAX=1)
endif()

# Link arc if async api is enabled
if (WSX_ENABLE_ASYNC)
    if (NOT TARGET arc)
        CPMAddPackage("gh:dankmeme01/arc@1.5.4")
    endif()
    target_link_libraries(wsx PUBLIC arc)
    target_compile_definitions(wsx PUBLIC WSX_ENABLE_ASYNC)
endif()

# if user chose the mbedtls option, use it in xtls
if (WSX_BUILD_MBEDTLS)
    set(WSX_ENABLE_TLS ON)
endif()

# Add xtls if not already added
if (WSX_ENABLE_TLS AND NOT TARGET xtls)
    CPMAddPackage(
        URI "gh:dankmeme01/xtls#4d0af76"
        OPTIONS
            "XTLS_ENABLE_MBEDTLS ${WSX_BUILD_MBEDTLS}"
            "XTLS_ENABLE_SOCKET ON"
            "XTLS_ENABLE_ARC_SOCKET ${WSX_ENABLE_ASYNC}"
    )
endif()

# Link xtls if TLS is enabled
if (WSX_ENABLE_TLS)
    target_link_libraries(wsx PUBLIC xtls)
    target_compile_definitions(wsx PUBLIC WSX_ENABLE_TLS)
endif()

# Link required dependencies (Result, qsox, base64, fmt)
if (NOT TARGET GeodeResult)
    CPMAddPackage("gh:geode-sdk/result@1.4.1")
endif()
if (NOT TARGET qsox)
    CPMAddPackage("gh:dankmeme01/qsox#1cd9dd7")
endif()
if (NOT TARGET base64)
    CPMAddPackage(
        URI "gh:tobiaslocker/base64#8d96a2a"
        OPTIONS "BASE64_ENABLE_TESTING OFF"
    )
endif()
if (NOT TARGET fmt)
    CPMAddPackage("gh:fmtlib/fmt#12.1.0")
endif()
target_link_libraries(wsx PUBLIC GeodeResult qsox base64 fmt)

# Bundle CA certificates
if (WSX_BUNDLE_CACERTS)
    CPMAddPackage("gh:geode-sdk/net_libs@8.19.0-10")
    target_compile_definitions(wsx PUBLIC WSX_BUNDLE_CACERTS)
    target_link_libraries(wsx PRIVATE ca-bundle)
endif()

# Examples
if (WSX_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
