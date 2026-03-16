#pragma once
#include "Message.hpp"
#include "CircularByteBuffer.hpp"
#include <Geode/Result.hpp>
#include <qsox/SocketAddress.hpp>
#include <qsox/TcpStream.hpp>

#ifdef WSX_ENABLE_TLS
# include <xtls/xtls.hpp>
#endif

#ifdef WSX_ENABLE_ASYNC
# include <arc/future/Future.hpp>
#endif

namespace wsx {

using geode::Result;
using geode::Ok;
using geode::Err;

class WsTransport {
public:
    virtual ~WsTransport() = default;

    virtual Result<size_t> send(const void* data, size_t size) = 0;
    virtual Result<size_t> receive(void* buf, size_t size) = 0;
};

#ifdef WSX_ENABLE_ASYNC
class AsyncWsTransport {
public:
    virtual ~AsyncWsTransport() = default;

    virtual arc::Future<Result<size_t>> send(const void* data, size_t size) = 0;
    virtual arc::Future<Result<size_t>> receive(void* buf, size_t size) = 0;
};
#endif

struct ParsedHttpResponse {
    std::vector<std::pair<std::string_view, std::string_view>> headers;
};

struct ClientConnectOptions {
    std::string_view path;
    std::string_view hostname;
    qsox::SocketAddress address;
    std::vector<std::pair<std::string, std::string>> headers;
#ifdef WSX_ENABLE_TLS
    std::shared_ptr<xtls::Context> tlsContext;
#endif
};

class ClientBase {
protected:
    qn::CircularByteBuffer m_rbuf;
    qn::CircularByteBuffer m_wbuf;
    std::vector<Message> m_fragments;

    ClientBase();
    ~ClientBase();

    ClientBase(const ClientBase&) = delete;
    ClientBase& operator=(const ClientBase&) = delete;
    ClientBase(ClientBase&&) noexcept = default;
    ClientBase& operator=(ClientBase&&) noexcept = default;

    static std::string generateRequest(uint8_t(&nonce)[16], const ClientConnectOptions& options);
    static Result<ParsedHttpResponse> parseResponse(uint8_t(&nonce)[16], std::string_view response);
};

#ifdef WSX_ENABLE_TLS
Result<std::shared_ptr<xtls::Context>> createContext();
#endif

void _genrandom(void* buf, size_t size);
Result<> _writeMessage(qn::CircularByteBuffer& buffer, const Message& message);

/// Returns nullopt if more data is required. Any other error should be treated as fatal.
Result<std::optional<Message>> _readOneMessage(qn::CircularByteBuffer& buffer);
/// Returns nullopt if more data is required. Any other error should be treated as fatal.
Result<std::optional<Message>> _readAndReassembleMessage(qn::CircularByteBuffer& buffer, std::vector<Message>& fragments);

}
