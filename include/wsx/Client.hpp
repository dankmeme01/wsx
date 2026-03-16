#pragma once
#include "Internal.hpp"
#include <span>

namespace wsx {

/// Represents a connected websocket client, use `Client::connect` to create one.
/// This class is not thread safe - it is okay to call `send` and `recv` concurrently,
/// but you must not have multiple threads calling `send` at the same time, and same with `recv`.
class Client : public ClientBase {
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
    static Result<Client> connect(std::string_view url);

    /// Connects to a websocket server using the given options
    static Result<Client> connect(const ClientConnectOptions& options);

    /// Sends a ping frame to the server with optional payload
    Result<void> ping(std::span<const uint8_t> payload = {});

    /// Writes a message to the buffer and then flushes it, sending it to the peer.
    /// If you want to efficiently batch multiple messages, call `write` and `flush` instead.
    Result<void> send(Message&& message);
    /// Writes a text message to the buffer and then flushes it, sending it to the peer.
    Result<void> send(std::string_view text);
    /// Writes a binary message to the buffer and then flushes it, sending it to the peer.
    Result<void> send(std::span<const uint8_t> binary);

    /// Writes a message to the buffer, but does NOT send it to the peer. Send it manually via `flush`.
    Result<void> write(Message&& message);
    /// Writes a text message to the buffer, but does NOT send it to the peer. Send it manually via `flush`.
    Result<void> write(std::string_view text);
    /// Writes a binary message to the buffer, but does NOT send it to the peer. Send it manually via `flush`.
    Result<void> write(std::span<const uint8_t> binary);

    /// Flushes the internal buffer, sending all pending messages to the peer.
    Result<void> flush();

    /// Receives a message from the server, blocking until data is available.
    /// If this returns an error, the error is fatal and the client will be disconnected.
    /// All future sends and recv calls will fail.
    Result<Message> recv();

    /// Closes the connection. All future sends and recv calls will fail.
    /// This blocks until the server acknowledges the close frame by sending a close frame back, or until an error occurs.
    Result<> close(uint16_t code = 1000, std::string_view reason = "");

    /// Closes the connection without waiting for an acknowledgment from the server.
    Result<> closeNoAck(uint16_t code = 1000, std::string_view reason = "");

protected:
    std::unique_ptr<WsTransport> m_transport;

    Client(std::unique_ptr<WsTransport> transport) : m_transport(std::move(transport)) {}

    static Result<Client> doHandshake(
        std::unique_ptr<WsTransport> transport,
        const ClientConnectOptions& options
    );

    Result<> sendCloseFrame(uint16_t code, std::string_view reason);
};

/// Shorthand for `Client::connect`, see that method for details and documentation.
inline auto connect(std::string_view url) {
    return Client::connect(url);
}

}