#pragma once
#include <wsx/Internal.hpp>

#ifdef WSX_ENABLE_ASYNC
#include <arc/net/TcpStream.hpp>

using arc::Future;

namespace wsx {

class AsyncTcpTransport : public AsyncWsTransport {
public:
    static Future<Result<std::unique_ptr<AsyncWsTransport>>> create(qsox::SocketAddress address);
    AsyncTcpTransport(arc::TcpStream stream);

    Future<Result<size_t>> send(const void* data, size_t size);
    Future<Result<size_t>> receive(void* buf, size_t size);
    Result<size_t> trySend(const void* buf, size_t size);

private:
    arc::TcpStream m_stream;
};

#ifdef WSX_ENABLE_TLS
class AsyncTlsTransport : public AsyncWsTransport {
public:
    static Future<Result<std::unique_ptr<AsyncWsTransport>>> create(
        qsox::SocketAddress address,
        std::shared_ptr<xtls::Context> tlsContext,
        std::string_view hostname
    );
    AsyncTlsTransport(xtls::AsyncTlsSocket stream);

    Future<Result<size_t>> send(const void* data, size_t size);
    Future<Result<size_t>> receive(void* buf, size_t size);
    Result<size_t> trySend(const void* buf, size_t size);

private:
    xtls::AsyncTlsSocket m_stream;
};
#endif

}

#endif
