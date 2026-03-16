#pragma once
#include <wsx/Internal.hpp>

namespace wsx {

class TcpTransport : public WsTransport {
public:
    static Result<std::unique_ptr<WsTransport>> create(qsox::SocketAddress address);
    TcpTransport(qsox::TcpStream stream);

    Result<size_t> send(const void* data, size_t size);
    Result<size_t> receive(void* buf, size_t size);

private:
    qsox::TcpStream m_stream;
};

#ifdef WSX_ENABLE_TLS
class TlsTransport : public WsTransport {
public:
    static Result<std::unique_ptr<WsTransport>> create(
        qsox::SocketAddress address,
        std::shared_ptr<xtls::Context> tlsContext,
        std::string_view hostname
    );
    TlsTransport(xtls::TlsSocket stream);

    Result<size_t> send(const void* data, size_t size);
    Result<size_t> receive(void* buf, size_t size);

private:
    xtls::TlsSocket m_stream;
};
#endif

}
