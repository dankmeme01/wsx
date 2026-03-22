#pragma once
#include "Internal.hpp"
#include <span>

#ifdef WSX_ENABLE_ASYNC

namespace wsx {

/// Represents a connected websocket client, use `Client::connect` to create one.
/// This class is not thread safe - it is okay to call `send` and `recv` concurrently,
/// but you must not have multiple threads calling `send` at the same time, and same with `recv`.
class AsyncClient : public ClientBase {
public:
    /// Connects to a WS server using the given URL. The URL must begin with `ws://` or `wss://`.
    /// Valid URL formats include:
    /// - `ws://example.com`         (uses port 80)
    /// - `wss://example.com`        (uses port 443)
    /// - `wss://example.com:8443`   (explicit port)
    /// - `ws://192.168.0.10:8080`   (IP address with port)
    /// - `wss://192.168.0.10:8080`  (only works if server doesn't require SNI)
    /// - `ws://[2001:db8::1]:8080`  (IPv6)
    ///
    /// For more customizability, use the other `connect` overloads.
    static arc::Future<Result<AsyncClient>> connect(std::string_view url);

    /// Connects to a websocket server using the given options
    static arc::Future<Result<AsyncClient>> connect(const ClientConnectOptions& options);

    /// Sends a ping frame to the server with optional payload
    arc::Future<Result<void>> ping(std::span<const uint8_t> payload = {});

    /// Writes a message to the buffer and then flushes it, sending it to the peer.
    /// If you want to efficiently batch multiple messages, call `write` and `flush` instead.
    arc::Future<Result<void>> send(Message message);
    /// Writes a text message to the buffer and then flushes it, sending it to the peer.
    arc::Future<Result<void>> send(std::string_view text);
    /// Writes a binary message to the buffer and then flushes it, sending it to the peer.
    arc::Future<Result<void>> send(std::span<const uint8_t> binary);

    /// Writes a message to the buffer, but does NOT send it to the peer. Send it manually via `flush`.
    arc::Future<Result<void>> write(Message message);
    /// Writes a text message to the buffer, but does NOT send it to the peer. Send it manually via `flush`.
    arc::Future<Result<void>> write(std::string_view text);
    /// Writes a binary message to the buffer, but does NOT send it to the peer. Send it manually via `flush`.
    arc::Future<Result<void>> write(std::span<const uint8_t> binary);

    /// Flushes the internal buffer, sending all pending messages to the peer.
    arc::Future<Result<void>> flush();
    /// Flushes the internal buffer, attempting to send any pending data to the peer.
    /// This does not block and returns the amount of bytes successfully sent, or an error if no data can be sent.
    /// If there is no data to flush, returns Ok(0).
    Result<size_t> tryFlush();

    /// Receives a message from the server, blocking until data is available.
    /// If this returns an error, the error is fatal and the client will be disconnected. All future sends and recv calls will fail.
    /// If this returns a close message, the client will be disconnected. There is no need to send a close frame back, the library does it for you.
    arc::Future<Result<Message>> recv();

    /// Closes the connection. All future sends and recv calls will fail.
    /// This blocks until the server acknowledges the close frame by sending a close frame back, or until an error occurs.
    arc::Future<Result<>> close(uint16_t code = 1000, std::string_view reason = "");

    /// Closes the connection without waiting for an acknowledgment from the server.
    arc::Future<Result<>> closeNoAck(uint16_t code = 1000, std::string_view reason = "");

    /// Closes the connection synchronously, attempting to send a close frame if it's appropriate, without blocking.
    /// Like `closeNoAck`, this does not wait for a reply.
    Result<> closeSync(uint16_t code = 1000, std::string_view reason = "");

    /// Returns whether a connection is still active
    bool isConnected() const {
        return m_transport != nullptr;
    }

protected:
    std::unique_ptr<AsyncWsTransport> m_transport;

    AsyncClient(std::unique_ptr<AsyncWsTransport> transport) : m_transport(std::move(transport)) {}

    static arc::Future<Result<AsyncClient>> doHandshake(
        std::unique_ptr<AsyncWsTransport> transport,
        const ClientConnectOptions& options
    );

    arc::Future<Result<>> sendCloseFrame(uint16_t code, std::string_view reason);
    arc::Future<std::string> handleProtocolError(std::string err);
};

/// Shorthand for `AsyncClient::connect`, see that method for details and documentation.
inline auto connectAsync(std::string_view url) {
    return AsyncClient::connect(url);
}

}

#endif
