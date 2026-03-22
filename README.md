# wsx

A modern C++23 WebSockets client, with the following features:
* TLS library agnostic, wsx uses [xtls](https://github.com/dankmeme01/xtls), which has built-in OpenSSL, wolfSSL and MbedTLS backends and makes it easy to implement a TLS backend with the library of your preference
* Synchronous (blocking) API and (optional) asynchronous API based on [Arc](https://github.com/dankmeme01/arc)

# Usage

All the examples below assume CPM is available.

## Sync + No TLS

Simplest configuration, requires no extra setup.

```cmake
CPMAddPackage("gh:dankmeme01/wsx#main")
```

```cpp
#include <wsx/Client.hpp>
#include <print>

int main() {
    auto result = wsx::connect("ws://localhost:8080");
    if (!result) {
        std::println("Connection failed: {}", result.unwrapErr());
        return 1;
    }

    wsx::Client client = std::move(result).unwrap();
    client.send("hi :)");

    auto result2 = client.recv();
    if (!result2) {
        std::println("Recv failed: {}", result2.unwrapErr());
        return 1;
    }

    wsx::Message msg = std::move(result2).unwrap();
    if (msg.isText()) {
        std::println("Received: {}", msg.text());
    }
}
```

## TLS via static MbedTLS

This is the simplest way of getting `wss://` support, by using the [MbedTLS](https://github.com/Mbed-TLS/mbedtls) library.

```cmake
CPMAddPackage(
    URI "gh:dankmeme01/wsx#main"
    OPTIONS "WSX_BUILD_MBEDTLS ON"
)
```

## TLS via xtls

If the static MbedTLS option does not suffice, you can use the [xtls](https://github.com/dankmeme01/xtls) library and manually choose between one of the built-in backends (such as OpenSSL or wolfSSL), or implement your own backend with a different TLS library.

Here's an example of how you could build this library using OpenSSL for TLS. Unlike MbedTLS, this uses `find_package` to find OpenSSL in your system. This means you likely will need to specify `OPENSSL_ROOT_DIR` when cross compiling.

```cmake
CPMAddPackage(
    URI "gh:dankmeme01/xtls#12695fe"
    OPTIONS "XTLS_ENABLE_OPENSSL ON"
            "XTLS_ENABLE_SOCKET ON" # socket must be enabled
)

CPMAddPackage(
    URI "gh:dankmeme01/wsx#main"
    OPTIONS "WSX_ENABLE_TLS ON"
)
```

## Async

The example below shows how to enable async support (with TLS) and how to use the async client API

```cmake
CPMAddPackage(
    URI "gh:dankmeme01/wsx#main"
    OPTIONS "WSX_ENABLE_ASYNC ON"
            "WSX_BUILD_MBEDTLS ON"
)
```

**If using custom TLS, `XTLS_ENABLE_ARC_SOCKET` MUST be enabled in xtls!**

```cpp
#include <wsx/AsyncClient.hpp>
#include <arc/prelude.hpp>

arc::Future<> amain() {
    auto result = co_await wsx::connectAsync("wss://localhost:8080");

    if (!result) {
        std::println("Connection failed: {}", result.unwrapErr());
        co_return;
    }

    wsx::AsyncClient client = std::move(result).unwrap();
    co_await client.send("hi :)");

    auto result2 = co_await client.recv();
    if (!result2) {
        std::println("Recv failed: {}", result2.unwrapErr());
        co_return;
    }

    wsx::Message msg = std::move(result2).unwrap();
    if (msg.isText()) {
        std::println("Received: {}", msg.text());
    }
}

ARC_DEFINE_MAIN(amain);
```
