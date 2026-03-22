#include "AsyncTransports.hpp"

#ifdef WSX_ENABLE_ASYNC

namespace wsx {

/// TCP transport, unencrypted

AsyncTcpTransport::AsyncTcpTransport(arc::TcpStream stream)
    : m_stream(std::move(stream))
{
    (void) m_stream.setNoDelay(true);
}

Future<Result<std::unique_ptr<AsyncWsTransport>>> AsyncTcpTransport::create(qsox::SocketAddress address) {
    auto result = co_await arc::TcpStream::connect(address);
    if (!result) {
        co_return Err(fmt::format("Connection to {} failed: {}", address.toString(), result.unwrapErr()));
    }

    co_return Ok(std::make_unique<AsyncTcpTransport>(std::move(result.unwrap())));
}

Future<Result<size_t>> AsyncTcpTransport::send(const void* data, size_t size) {
    co_return (co_await m_stream.send(data, size)).mapErr([](auto err) {
        return fmt::format("TCP send failed: {}", err.message());
    });
}

Future<Result<size_t>> AsyncTcpTransport::receive(void* buf, size_t size) {
    co_return (co_await m_stream.receive(buf, size)).mapErr([](auto err) {
        return fmt::format("TCP receive failed: {}", err.message());
    });
}

Result<size_t> AsyncTcpTransport::trySend(const void* buf, size_t size) {
    auto res = m_stream.inner().send(buf, size);
    if (!res) {
        return Err(fmt::format("TCP trySend failed: {}", res.unwrapErr().message()));
    }
    return Ok(res.unwrap());
}

/// TLS transport
#ifdef WSX_ENABLE_TLS

AsyncTlsTransport::AsyncTlsTransport(xtls::AsyncTlsSocket stream)
    : m_stream(std::move(stream)) {}

Future<Result<std::unique_ptr<AsyncWsTransport>>> AsyncTlsTransport::create(qsox::SocketAddress address, std::shared_ptr<xtls::Context> tlsContext, std::string_view hostname) {
    auto result = co_await xtls::AsyncTlsSocket::connect(address, tlsContext, std::string{hostname});
    if (!result) {
        co_return Err(fmt::format("TLS connection to {} failed: {}", address.toString(), result.unwrapErr().message));
    }

    co_return Ok(std::make_unique<AsyncTlsTransport>(std::move(result.unwrap())));
}

Future<Result<size_t>> AsyncTlsTransport::send(const void* data, size_t size) {
    co_return (co_await m_stream.send(data, size)).mapErr([](auto err) {
        return fmt::format("TLS send failed: {}", err.message);
    });
}

Future<Result<size_t>> AsyncTlsTransport::receive(void* buf, size_t size) {
    co_return (co_await m_stream.receive(buf, size)).mapErr([](auto err) {
        return fmt::format("TLS receive failed: {}", err.message);
    });
}

Result<size_t> AsyncTlsTransport::trySend(const void* buf, size_t size) {
    auto res = m_stream.trySend(buf, size);
    if (!res) {
        return Err(fmt::format("TLS trySend failed: {}", res.unwrapErr().message));
    }
    return Ok(res.unwrap());
}

#endif

}

#endif