#ifdef WSX_ENABLE_ASYNC

#include <wsx/AsyncClient.hpp>
#include "AsyncTransports.hpp"

using namespace arc;

namespace wsx {

Future<Result<AsyncClient>> AsyncClient::connect(std::string_view url) {
    auto res = co_await arc::spawnBlocking<Result<ClientConnectOptions>>([url] {
        return ClientConnectOptions::fromUrl(url);
    });

    GEODE_CO_UNWRAP_INTO(auto opts, res);
    co_return co_await connect(opts);
}

Future<Result<AsyncClient>> AsyncClient::connect(const ClientConnectOptions& options) {
    std::unique_ptr<AsyncWsTransport> transport;
#ifdef WSX_ENABLE_TLS
    if (options.tlsContext) {
        GEODE_CO_UNWRAP_INTO(transport, co_await AsyncTlsTransport::create(options.address, options.tlsContext, options.hostname));
    }
#endif
    if (!transport) {
        GEODE_CO_UNWRAP_INTO(transport, co_await AsyncTcpTransport::create(options.address));
    }
    co_return co_await doHandshake(std::move(transport), options);
}

Future<Result<AsyncClient>> AsyncClient::doHandshake(std::unique_ptr<AsyncWsTransport> transport, const ClientConnectOptions& options) {
    uint8_t nonce[16];
    auto req = generateRequest(nonce, options);
    GEODE_CO_UNWRAP(co_await transport->send(req.data(), req.size()));

    AsyncClient client(std::move(transport));
    size_t httpSize = 0;

    while (true) {
        auto wnd = client.rwindow(4096);
        size_t recvd = GEODE_CO_UNWRAP(co_await client.m_transport->receive(wnd.data(), wnd.size()));
        client.m_rbuf.advanceWrite(recvd);

        auto wrpread = client.m_rbuf.peek(client.m_rbuf.size());

        std::string_view view{(const char*)wrpread.first.data(), wrpread.size()};

        // stop once we have received the full HTTP response headers (indicated by \r\n\r\n)
        auto pos = view.find("\r\n\r\n");
        if (pos != std::string_view::npos) {
            httpSize = pos + 4;
            break;
        }
    }

    std::string httpResponse(httpSize, '\0');
    client.m_rbuf.read(httpResponse.data(), httpSize);
    GEODE_CO_UNWRAP(parseResponse(nonce, httpResponse));

    co_return Ok(std::move(client));
}

Future<Result<void>> AsyncClient::ping(std::span<const uint8_t> payload) {
    return send(Message(Message::Type::Ping, std::vector(payload.begin(), payload.end())));
}

Future<Result<void>> AsyncClient::send(Message message) {
    if (!m_transport) co_return Err("Connection is closed");

    GEODE_CO_UNWRAP(co_await write(std::move(message)));
    co_return co_await flush();
}

Future<Result<void>> AsyncClient::send(std::string_view text) {
    return send(Message(text));
}

Future<Result<void>> AsyncClient::send(std::span<const uint8_t> binary) {
    return send(Message(binary));
}

Future<Result<void>> AsyncClient::write(Message message) {
    if (!m_transport) co_return Err("Connection is closed");

    co_return _writeMessage(m_wbuf, message);
}

Future<Result<void>> AsyncClient::write(std::string_view text) {
    return write(Message(text));
}

Future<Result<void>> AsyncClient::write(std::span<const uint8_t> binary) {
    return write(Message(binary));
}

Future<Result<void>> AsyncClient::flush() {
    if (!m_transport) co_return Err("Connection is closed");

    while (!m_wbuf.empty()) {
        auto wrp = m_wbuf.peek(m_wbuf.size());
        GEODE_CO_UNWRAP_INTO(auto sent, co_await m_transport->send(wrp.first.data(), wrp.first.size()));
        m_wbuf.skip(sent);
    }
    co_return Ok();
}

Result<size_t> AsyncClient::tryFlush() {
    if (!m_transport) return Err("Connection is closed");

    size_t sent = 0;

    while (!m_wbuf.empty()) {
        auto wrp = m_wbuf.peek(m_wbuf.size());
        GEODE_UNWRAP_INTO(auto bytes, m_transport->trySend(wrp.first.data(), wrp.first.size()));
        sent += bytes;
        m_wbuf.skip(bytes);
    }

    return Ok(sent);
}

Future<Result<Message>> AsyncClient::recv() {
    if (!m_transport) co_return Err("Connection is closed");

    while (true) {
        auto res = readFromBuffer();
        if (!res) {
            co_return Err(co_await this->handleProtocolError(std::move(res).unwrapErr()));
        }

        auto opt = std::move(res).unwrap();
        if (opt) {
            if (opt->isClose()) {
                GEODE_CO_UNWRAP(co_await this->sendCloseFrame(1000, ""));
                m_transport.reset();
            } else if (opt->isPing()) {
                // send a pong message
                Message msg(Message::Type::Pong, std::move(*opt).data());
                GEODE_CO_UNWRAP(co_await this->send(std::move(msg)));
                continue;
            } else if (opt->isPong()) {
                // ignore
                continue;
            }

            co_return Ok(std::move(*opt));
        }

        // read from the socket
        auto wnd = this->rwindow(4096);
        size_t recvd = GEODE_CO_UNWRAP(co_await m_transport->receive(wnd.data(), wnd.size()));
        m_rbuf.advanceWrite(recvd);
    }
}

Future<Result<>> AsyncClient::close(uint16_t code, std::string_view reason) {
    GEODE_CO_UNWRAP(co_await sendCloseFrame(code, reason));

    // wait for a response close frame
    while (true) {
        auto res = co_await this->recv();
        if (!res) {
            m_transport.reset();
            co_return Err(std::move(res).unwrapErr());
        }

        if (res.unwrap().isClose()) {
            break;
        }
    }

    m_transport.reset();
    co_return Ok();
}

Future<Result<>> AsyncClient::closeNoAck(uint16_t code, std::string_view reason) {
    GEODE_CO_UNWRAP(co_await sendCloseFrame(code, reason));
    m_transport.reset();
    co_return Ok();
}

Result<> AsyncClient::closeSync(uint16_t code, std::string_view reason) {
    if (!m_transport) return Err("Connection is already closed");

    auto frame = makeCloseFrame(code, reason);
    GEODE_UNWRAP(_writeMessage(m_wbuf, frame));

    auto res = this->tryFlush();
    m_transport.reset();
    GEODE_UNWRAP(res);

    return Ok();
}

Future<Result<>> AsyncClient::sendCloseFrame(uint16_t code, std::string_view reason) {
    if (!m_transport) co_return Err("Connection is already closed");
    auto res = co_await this->send(makeCloseFrame(code, reason));
    if (!res) {
        m_transport.reset();
        co_return Err(fmt::format("failed to send close frame: {}", res.unwrapErr()));
    }
    co_return Ok();
}

Future<std::string> AsyncClient::handleProtocolError(std::string err) {
    // send a close frame
    if (m_transport) {
        auto [code, reason] = ClientBase::handleProtocolError(err);
        auto res = co_await this->closeNoAck(code, reason);
        if (!res) {
            co_return fmt::format("Protocol error: {}. Sending a close frame also failed: {}", err, res.unwrapErr());
        }
        co_return fmt::format("Protocol violation by the server: {}", err);
    }
    co_return err;
}


}

#endif
