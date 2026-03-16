cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT CURRENT_CPM_VERSION)
    include(cmake/get_cpm.cmake)
endif()

project(wsx)
option(WSX_ENABLE_TLS "Enable TLS" OFF)
option(WSX_ENABLE_ASYNC "Enable async client api" OFF)
option(WSX_BUILD_WOLFSSL "Build wolfSSL as crypto backend" OFF)
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

# if BUILD_WOLFSSL is set, enable xtls and add wolfssl as a dependency
if (WSX_BUILD_WOLFSSL)
    CPMAddPackage(
        URI "gh:wolfSSL/wolfssl@5.8.4-stable"
        OPTIONS "WOLFSSL_HARDEN OFF"
                "WOLFSSL_OLD_TLS OFF"
                "WOLFSSL_OPENSSLEXTRA ON"
                "WOLFSSL_ALPN ON"
                "WOLFSSL_SNI ON"
                "WOLFSSL_TLSV12 ON"
                "WOLFSSL_SESSION_TICKET ON"
                "WOLFSSL_INSTALL OFF"
                "WOLFSSL_CRYPT_TESTS OFF"
                "WOLFSSL_EXAMPLES OFF"
                "WOLFSSL_ALT_CERT_CHAINS ON"
                "WOLFSSL_DEBUG ON"
                "BUILD_SHARED_LIBS OFF"
    )

    get_target_property(WOLFSSL_INCLUDE_DIRS2 wolfssl INCLUDE_DIRECTORIES)
    set(WOLFSSL_INCLUDE_DIRS ${WOLFSSL_INCLUDE_DIRS2} CACHE STRING "" FORCE)

    if (MSVC)
        target_compile_options(wolfssl PRIVATE /w)
    else()
        target_compile_options(wolfssl PRIVATE -w)
    endif()

    set(WSX_ENABLE_TLS ON)
endif()

# Add xtls if not already added
if (WSX_ENABLE_TLS AND NOT TARGET xtls)
    CPMAddPackage(
        URI "gh:dankmeme01/xtls#228eebd"
        OPTIONS
            "XTLS_ENABLE_WOLFSSL ${WSX_BUILD_WOLFSSL}"
            "XTLS_ENABLE_SOCKET ON"
            "XTLS_ENABLE_ASYNC_SOCKET ${WSX_ENABLE_ASYNC}"
    )
endif()

# Link xtls if TLS is enabled
if (WSX_ENABLE_TLS)
    target_link_libraries(wsx PUBLIC xtls)
    target_compile_definitions(wsx PUBLIC WSX_ENABLE_TLS)
endif()

# Link required dependencies (Result, qsox, base64)
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
target_link_libraries(wsx PUBLIC GeodeResult qsox base64)

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
