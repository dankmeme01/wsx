#pragma once
#include "Message.hpp"
#include "CircularByteBuffer.hpp"
#include <Geode/Result.hpp>
#include <qsox/SocketAddress.hpp>
#include <qsox/TcpStream.hpp>
#include <memory>
#include <fmt/format.h>

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
    virtual Result<size_t> trySend(const void* buf, size_t size) = 0;
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

    /// Parses the given URL and extracts path, hostname and address into this struct.
    /// This function is blocking because it may need to resolve DNS, so it should not be called in async context directly.
    /// This populates all fields except headers, so setting a TLS context is also not necessary.
    static Result<ClientConnectOptions> fromUrl(std::string_view url);
};

class ClientBase {
protected:
    wsx::CircularByteBuffer m_rbuf;
    wsx::CircularByteBuffer m_wbuf;
    std::vector<Message> m_fragments;

    // maybe make this configurable later?
    static constexpr size_t MAX_BUFFER_SIZE = 128 * 1024 * 1024;
    static constexpr size_t MAX_HANDSHAKE_SIZE = 512 * 1024;

    ClientBase();
    ~ClientBase();

    ClientBase(const ClientBase&) = delete;
    ClientBase& operator=(const ClientBase&) = delete;
    ClientBase(ClientBase&&) noexcept = default;
    ClientBase& operator=(ClientBase&&) noexcept = default;

    static std::string generateRequest(uint8_t(&nonce)[16], const ClientConnectOptions& options);
    static Result<ParsedHttpResponse> parseResponse(uint8_t(&nonce)[16], std::string_view response);
    static Message makeCloseFrame(uint16_t code, std::string_view reason);

    Result<std::optional<Message>> readFromBuffer();
    std::span<uint8_t> rwindow(size_t atLeast);
    std::pair<uint16_t, std::string_view> handleProtocolError(std::string_view err);
};

#ifdef WSX_ENABLE_TLS
Result<std::shared_ptr<xtls::Context>> createContext();
#endif

void _genrandom(void* buf, size_t size);
Result<> _writeMessage(wsx::CircularByteBuffer& buffer, const Message& message);

/// Returns nullopt if more data is required. Any other error should be treated as fatal.
Result<std::optional<Message>> _readOneMessage(wsx::CircularByteBuffer& buffer);
/// Returns nullopt if more data is required. Any other error should be treated as fatal.
Result<std::optional<Message>> _readAndReassembleMessage(wsx::CircularByteBuffer& buffer, std::vector<Message>& fragments);

bool isValidUtf8(std::string_view data);
bool isValidUtf8(std::span<const uint8_t> data);

}
