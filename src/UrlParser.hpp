#pragma once
#include <Geode/Result.hpp>
#include <qsox/SocketAddress.hpp>
#include <string_view>
#include <charconv>
#include <stdint.h>

namespace wsx {

struct ParsedUrl {
    std::string_view hostname;
    std::string_view path;
    std::optional<qsox::IpAddress> ip;
    uint16_t port;
    bool tls;
};

inline geode::Result<ParsedUrl> parseUrl(std::string_view url) {
    ParsedUrl out{};
    out.tls = url.starts_with("wss://");
    if (!out.tls && !url.starts_with("ws://")) {
        return geode::Err("URL must start with ws:// or wss://");
    }

    url.remove_prefix(out.tls ? 6 : 5);

    // split into host/ip and path
    auto slashPos = url.find('/');
    if (slashPos != std::string_view::npos) {
        out.path = url.substr(slashPos);
        url = url.substr(0, slashPos);
    }

    if (url.empty()) {
        return geode::Err("URL must contain a hostname or IP address");
    }

    // try to parse as a socket address
    auto res = qsox::SocketAddress::parse(url);
    if (res) {
        auto addr = res.unwrap();
        out.ip = addr.ip();
        out.port = addr.port();
        return geode::Ok(out);
    }

    // then try to parse as an IP address, and use a default port
    auto res2 = qsox::IpAddress::parse(std::string{url});
    if (res2) {
        out.ip = res2.unwrap();
        out.port = out.tls ? 443 : 80;
        return geode::Ok(out);
    }

    // otherwise, assume it's a domain name with an optional port
    auto colonPos = url.find(':');
    if (colonPos != std::string_view::npos) {
        std::string_view portStr = url.substr(colonPos + 1);
        url = url.substr(0, colonPos);

        uint16_t port;
        auto [ptr, ec] = std::from_chars(&*portStr.begin(), &*portStr.end(), port);
        if (ec == std::errc{}) {
            out.port = port;
        } else {
            return geode::Err("Invalid port number");
        }
    }

    out.hostname = url;
    if (!out.port) {
        out.port = out.tls ? 443 : 80;
    }
    return geode::Ok(out);
}

}