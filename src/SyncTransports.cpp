#include "SyncTransports.hpp"

namespace wsx {

/// TCP transport, unencrypted

TcpTransport::TcpTransport(qsox::TcpStream stream)
    : m_stream(std::move(stream))
{
    (void) m_stream.setNoDelay(true);
}

Result<std::unique_ptr<WsTransport>> TcpTransport::create(qsox::SocketAddress address) {
    auto result = qsox::TcpStream::connect(address);
    if (!result) {
        return Err(fmt::format("Connection to {} failed: {}", address.toString(), result.unwrapErr()));
    }

    return Ok(std::make_unique<TcpTransport>(std::move(result.unwrap())));
}

Result<size_t> TcpTransport::send(const void* data, size_t size) {
    return m_stream.send(data, size).mapErr([](auto err) {
        return fmt::format("TCP send failed: {}", err.message());
    });
}

Result<size_t> TcpTransport::receive(void* buf, size_t size) {
    return m_stream.receive(buf, size).mapErr([](auto err) {
        return fmt::format("TCP receive failed: {}", err.message());
    });
}

/// TLS transport
#ifdef WSX_ENABLE_TLS

TlsTransport::TlsTransport(xtls::TlsSocket stream)
    : m_stream(std::move(stream)) {}

Result<std::unique_ptr<WsTransport>> TlsTransport::create(qsox::SocketAddress address, std::shared_ptr<xtls::Context> tlsContext, std::string_view hostname) {
    auto result = xtls::TlsSocket::connect(address, tlsContext, std::string{hostname});
    if (!result) {
        return Err(fmt::format("TLS connection to {} failed: {}", address.toString(), result.unwrapErr().message));
    }

    return Ok(std::make_unique<TlsTransport>(std::move(result.unwrap())));
}

Result<size_t> TlsTransport::send(const void* data, size_t size) {
    return m_stream.send(data, size).mapErr([](auto err) {
        return fmt::format("TLS send failed: {}", err.message);
    });
}

Result<size_t> TlsTransport::receive(void* buf, size_t size) {
    return m_stream.receive(buf, size).mapErr([](auto err) {
        return fmt::format("TLS receive failed: {}", err.message);
    });
}

#endif

}
