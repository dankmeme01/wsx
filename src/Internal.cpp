#include <wsx/Internal.hpp>
#include "UrlParser.hpp"

#include <qsox/Resolver.hpp>
#include <random>
#include <base64.hpp>
#include "sha1.hpp"
#include "utf8.h"

#ifdef WSX_BUNDLE_CACERTS
#include <ca_bundle.h>
#endif

namespace wsx {

ClientBase::ClientBase() {}
ClientBase::~ClientBase() {}

Result<ClientConnectOptions> ClientConnectOptions::fromUrl(std::string_view url) {
    GEODE_UNWRAP_INTO(auto parts, wsx::parseUrl(url));

    auto addr = qsox::SocketAddress::any();
    addr.setPort(parts.port);

    // resolve hostname if needed
    if (parts.ip) {
        addr.setAddress(*parts.ip);
    } else {
        if (parts.hostname.empty()) {
            return Err("URL must contain a hostname or IP address");
        }
        auto result = qsox::resolver::resolve(std::string{parts.hostname});
        if (!result) {
            return Err(fmt::format("Could not resolve host '{}': {}", parts.hostname, result.unwrapErr()));
        }

        addr.setAddress(result.unwrap());
    }

    ClientConnectOptions opts {
        .path = parts.path,
        .hostname = parts.hostname,
        .address = addr,
    };
    if (parts.tls) {
#ifdef WSX_ENABLE_TLS
        GEODE_UNWRAP_INTO(opts.tlsContext, createContext());
#else
        return Err("wsx was not built with TLS support, cannot connect to wss:// URLs");
#endif
    }

    return Ok(std::move(opts));
}

static bool equalsIgnoreCase(std::string_view a, std::string_view b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](auto c1, auto c2) {
        return std::tolower((unsigned char)c1) == std::tolower((unsigned char)c2);
    });
}

static std::string nonceToKey(uint8_t(&nonce)[16]) {
    return base64::encode_into<std::string>(nonce, nonce + 16);
}

static std::string nonceToAcceptKey(uint8_t(&nonce)[16]) {
    auto key = nonceToKey(nonce);
    key += "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    // hash with sha1
    class SHA1 sha;
    sha.update(key);
    auto hash = sha.final();

    // base64 encode
    return base64::encode_into<std::string>(hash.begin(), hash.end());
}

std::string ClientBase::generateRequest(uint8_t(&nonce)[16], const ClientConnectOptions& options) {
    wsx::_genrandom(nonce, 16);
    auto encodedKey = nonceToKey(nonce);

    std::string hostname{options.hostname.empty() ? "localhost" : options.hostname};

    // if the port is non-standard, it must be included in the host header
    bool isStandard =
        options.address.port() == 0  // automatic
#ifdef WSX_ENABLE_TLS
        // tls connection to :443
        || (options.tlsContext && options.address.port() == 443)
        // non-tls connection to :80
        || (!options.tlsContext && options.address.port() == 80);
#else
        // non-tls connection to :80
        || options.address.port() == 80;
#endif

    if (!isStandard) {
        hostname += fmt::format(":{}", options.address.port());
    }

    std::string req =  fmt::format(
        "GET {} HTTP/1.1\r\n"
        "Host: {}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: {}\r\n"
        "Sec-WebSocket-Version: 13\r\n",
        options.path.empty() ? "/" : options.path,
        hostname,
        encodedKey
    );

    for (auto& [key, value] : options.headers) {
        req += fmt::format("{}: {}\r\n", key, value);
    }

    req += "\r\n";

    return req;
}

Result<ParsedHttpResponse> ClientBase::parseResponse(uint8_t(&nonce)[16], std::string_view response) {
    ParsedHttpResponse out;

    auto statusEnd = response.find("\r\n");
    if (statusEnd == std::string_view::npos) {
        return Err("Invalid response: no status line");
    }

    auto statusLine = response.substr(0, statusEnd);
    auto bodyStart = response.substr(statusEnd + 2);
    // skip "HTTP/1.1 "
    if (!statusLine.starts_with("HTTP/1.1 ")) {
        return Err("Invalid response: status line does not start with 'HTTP/1.1 '");
    }
    statusLine.remove_prefix(9);

    auto codeEnd = statusLine.find_first_of(" \r\n");
    if (codeEnd == std::string_view::npos) {
        return Err("Invalid response: status code not found");
    }

    auto codeStr = statusLine.substr(0, codeEnd);
    int code = 0;
    auto [ptr, ec] = std::from_chars(&*codeStr.begin(), &*codeStr.end(), code);
    if (ec != std::errc()) {
        return Err("Invalid response: status code is not a valid integer");
    }

    // finally, check the code
    if (code != 101) {
        return Err(fmt::format("Handshake failed: HTTP code {}", code));
    }

    // parse the headers
    while (true) {
        auto lineEnd = bodyStart.find("\r\n");
        if (lineEnd == std::string_view::npos) {
            return Err("Invalid response header");
        }

        auto line = bodyStart.substr(0, lineEnd);
        bodyStart.remove_prefix(lineEnd + 2);

        if (line.empty()) {
            break; // end of headers
        }

        auto colonPos = line.find(": ");
        if (colonPos == std::string_view::npos) {
            return Err("Invalid response header");
        }

        auto name = line.substr(0, colonPos);
        auto value = line.substr(colonPos + 2);
        out.headers.emplace_back(name, value);
    }

    // check the required headers
    bool hasUpgrade = false;
    bool hasConnection = false;
    bool hasAccept = false;

    for (auto& [key, value] : out.headers) {
        if (equalsIgnoreCase(key, "Upgrade") && equalsIgnoreCase(value, "websocket")) {
            hasUpgrade = true;
            continue;
        }

        if (equalsIgnoreCase(key, "Connection") && equalsIgnoreCase(value, "Upgrade")) {
            hasConnection = true;
            continue;
        }

        if (equalsIgnoreCase(key, "Sec-WebSocket-Accept")) {
            hasAccept = true;
            auto expectedAccept = nonceToAcceptKey(nonce);
            if (value != expectedAccept) {
                return Err("Handshake failed: invalid Sec-WebSocket-Accept value");
            }
        }
    }

    if (!hasUpgrade || !hasConnection || !hasAccept) {
        return Err("Handshake failed: missing required headers");
    }

    return Ok(std::move(out));
}

Message ClientBase::makeCloseFrame(uint16_t code, std::string_view reason) {
    if (reason.size() > 123) {
        reason = reason.substr(0, 123);
    }

    std::vector<uint8_t> payload(2 + reason.size());
    payload[0] = (code >> 8) & 0xFF;
    payload[1] = code & 0xFF;
    std::memcpy(payload.data() + 2, reason.data(), reason.size());

    return Message(Message::Type::Close, std::move(payload));
}

Result<std::optional<Message>> ClientBase::readFromBuffer() {
    auto res1 = _readAndReassembleMessage(m_rbuf, m_fragments);
    if (!res1) {
        return Err(std::move(res1).unwrapErr());
    }

    return Ok(std::move(res1).unwrap());
}

std::span<uint8_t> ClientBase::rwindow(size_t atLeast) {
    auto wnd = m_rbuf.writeWindow();
    if (wnd.size() < atLeast) {
        // reserve more space if needed, but error if too much space is already reserved
        if (m_rbuf.capacity() >= MAX_BUFFER_SIZE) {
            throw std::runtime_error("Buffer overflow: received data exceeds maximum buffer size");
        }

        m_rbuf.reserve(atLeast);
        wnd = m_rbuf.writeWindow();
    }

    return wnd;
}

std::pair<uint16_t, std::string_view> ClientBase::handleProtocolError(std::string_view err) {
    if (err.contains("Invalid UTF-8")) {
        return {1007, "Invalid UTF-8"};
    } else {
        return {1002, "Protocol violation"};
    }
}

#ifdef WSX_ENABLE_TLS

Result<std::shared_ptr<xtls::Context>> createContext() {
    auto res = xtls::Backend::get().createContext(xtls::ContextType::Client);
    if (!res) {
        return Err(fmt::format("Context creation failed: {}", res.unwrapErr().message));
    }

    auto ctx = std::move(res).unwrap();

#ifdef WSX_BUNDLE_CACERTS
    auto res2 = ctx->loadCACertsBlob(CA_BUNDLE_CONTENT);
#else
    auto res2 = ctx->loadSystemCACerts();
#endif

    if (!res2) {
        return Err(fmt::format("Failed to load CA certificates: {}", res2.unwrapErr().message));
    }

    return Ok(std::move(ctx));
}

#endif

void _genrandom(void* buf, size_t size) {
    std::random_device rd{};
    uint8_t* p = static_cast<uint8_t*>(buf);

    while (size > 0) {
        uint32_t randomValue = rd();
        size_t toCopy = std::min(size, sizeof(randomValue));
        std::memcpy(p, &randomValue, toCopy);
        p += toCopy;
        size -= toCopy;
    }
}

Result<> _writeMessage(wsx::CircularByteBuffer& buffer, const Message& message) {
    using enum Message::Type;

    GEODE_UNWRAP(message.validate());

    uint8_t header[14] = {0};
    size_t headerLen = 2;
    uint8_t& opcode = header[0];
    uint8_t& maskAndLen = header[1];

    switch (message.type()) {
        case Text: opcode = 0x1; break;
        case Binary: opcode = 0x2; break;
        case Close: opcode = 0x8; break;
        case Ping: opcode = 0x9; break;
        case Pong: opcode = 0xA; break;
        default: return Err("Invalid message type when encoding message");
    }
    opcode |= 0x80; // set FIN bit

    auto msgdata = message.data();
    uint64_t msglen = msgdata.size();

    // since we are a client implementation, all messages must be masked
    bool mask = true;
    if (mask) maskAndLen = 0x80;

    if (msglen < 126) {
        maskAndLen |= static_cast<uint8_t>(msglen);
    } else if (msglen <= 0xFFFF) {
        maskAndLen |= 126;
        header[2] = (msglen >> 8) & 0xFF;
        header[3] = msglen & 0xFF;
        headerLen += 2;
    } else {
        maskAndLen |= 127;
        for (size_t i = 0; i < 8; i++) {
            header[2 + i] = (msglen >> (56 - 8 * i)) & 0xFF;
        }
        headerLen += 8;
    }

    uint32_t maskingKey = 0;
    if (mask) {
        wsx::_genrandom(&maskingKey, sizeof(maskingKey));
        uint8_t* mkbytes = header + headerLen;
        std::memcpy(mkbytes, &maskingKey, sizeof(maskingKey));
        headerLen += 4;
    }

    buffer.write(header, headerLen);

    // now write the potentially masked payload
    if (mask) {
        // scratch space to avoid calling write a ton of times
        uint8_t scratch[1024];
        size_t scratchPos = 0;

        uint8_t maskBytes[4];
        std::memcpy(maskBytes, &maskingKey, sizeof(maskingKey));

        for (size_t i = 0; i < msglen; i++) {
            uint8_t maskedByte = msgdata[i] ^ maskBytes[i % 4];
            scratch[scratchPos++] = maskedByte;

            if (scratchPos == sizeof(scratch)) {
                buffer.write(scratch, scratchPos);
                scratchPos = 0;
            }
        }

        if (scratchPos > 0) {
            buffer.write(scratch, scratchPos);
        }
    } else {
        buffer.write(msgdata.data(), msglen);
    }

    return Ok();
}

Result<std::optional<Message>> _readOneMessage(wsx::CircularByteBuffer& buffer) {
    using enum Message::Type;

    size_t headerLen = 2;
    uint8_t shortHeader[2];

    if (buffer.size() < headerLen) {
        return Ok(std::nullopt);
    }

    buffer.peek(shortHeader, 2);

    bool fin = shortHeader[0] & 0x80;
    uint8_t opcode = shortHeader[0] & 0x0F;
    bool masked = shortHeader[1] & 0x80;
    uint64_t payloadLen = shortHeader[1] & 0x7F;

    uint8_t extendedPayloadLen[8] = {0};

    // rsv bits must not be set
    if (shortHeader[0] & 0x70) {
        return Err("Received frame with non-zero RSV bits");
    }

    // control frames must have payload size <= 125
    if ((opcode >= 0x8) && payloadLen > 125) {
        return Err("Received control frame with payload larger than 125 bytes");
    }

    // control frames must not be fragmented
    if ((opcode >= 0x8) && !fin) {
        return Err("Received fragmented control frame");
    }

    Message::Type type;
    switch (opcode) {
        case 0x0: type = Fragment; break;
        case 0x1: type = Text; break;
        case 0x2: type = Binary; break;
        case 0x8: type = Close; break;
        case 0x9: type = Ping; break;
        case 0xA: type = Pong; break;
        default: return Err("Received frame with invalid opcode");
    }

    // length of close frame must be either 0 or at least 2
    if (type == Message::Type::Close && payloadLen != 0 && payloadLen < 2) {
        return Err("Received close frame with invalid payload length");
    }

    if (payloadLen == 126) {
        headerLen += 2;
        if (buffer.size() < headerLen) {
            return Ok(std::nullopt);
        }

        buffer.peek(extendedPayloadLen, 2, 2);
        payloadLen = std::byteswap(*reinterpret_cast<uint16_t*>(extendedPayloadLen));
    } else if (payloadLen == 127) {
        headerLen += 8;
        if (buffer.size() < headerLen) {
            return Ok(std::nullopt);
        }

        buffer.peek(extendedPayloadLen, 8, 2);
        payloadLen = std::byteswap(*reinterpret_cast<uint64_t*>(extendedPayloadLen));
    }

    if (masked) {
        return Err("Received masked frame from server");
    }

    if (buffer.size() < headerLen + payloadLen) {
        return Ok(std::nullopt);
    }

    std::vector<uint8_t> payload(payloadLen);
    buffer.peek(payload.data(), payloadLen, headerLen);
    buffer.skip(headerLen + payloadLen);

    Message msg(type, std::move(payload));
    msg.setFinal(fin);

    return Ok(std::move(msg));
}

static Result<Message> reassemble(std::vector<Message>& fragments) {
    // first frame MUST be a non-control and non-fragment frame
    if (fragments.empty()) {
        return Err("No fragments to reassemble");
    }
    auto& first = fragments[0];
    if (first.isControl() || first.type() == Message::Type::Fragment) {
        return Err("First fragment must be a non-control, non-fragment frame");
    }

    Message::Type finalType = first.type();
    std::vector<uint8_t> data;

    for (size_t i = 0; i < fragments.size(); i++) {
        auto& fragment = fragments[i];
        if (i != 0 && fragment.type() != Message::Type::Fragment) {
            return Err("Invalid type in fragmented message");
        }

        auto fragdata = fragment.data();
        data.insert(data.end(), fragdata.begin(), fragdata.end());
    }
    fragments.clear();

    Message msg(finalType, std::move(data));
    GEODE_UNWRAP(msg.validate());

    return Ok(std::move(msg));
}

Result<std::optional<Message>> _readAndReassembleMessage(wsx::CircularByteBuffer& buffer, std::vector<Message>& fragments) {
    while (true) {
        GEODE_UNWRAP_INTO(auto one, _readOneMessage(buffer));

        // need more data?
        if (!one) return Ok(std::nullopt);

        Message msg = std::move(*one);

        // server must not send a new non-control message if there are already fragments buffered
        if (!fragments.empty() && !msg.isControl() && msg.type() != Message::Type::Fragment) {
            return Err("Received new non-control frame while a fragmented message is incomplete");
        }

        // is this a complete message that is NOT a fragment?
        if (msg.final() && msg.type() != Message::Type::Fragment) {
            GEODE_UNWRAP(msg.validate());
            return Ok(std::move(msg));
        }

        // is this the final fragment in a message?
        if (msg.final() && msg.type() == Message::Type::Fragment) {
            if (fragments.empty()) {
                return Err("Received final fragment but no fragments have been received");
            }

            // append this fragment to the previous ones and return the complete message
            fragments.push_back(std::move(msg));
            GEODE_UNWRAP_INTO(auto complete, reassemble(fragments));
            return Ok(std::move(complete));
        }

        // otherwise, this is either the first frame or a continuation
        // if it is a continuation, there must be at least one previous fragment
        if (msg.type() == Message::Type::Fragment && fragments.empty()) {
            return Err("Received fragment but the first frame has not been received");
        }

        fragments.push_back(std::move(msg));
        continue;
    }
}

bool isValidUtf8(std::string_view data) {
    return isValidUtf8(std::span((const uint8_t*)data.data(), data.size()));
}

bool isValidUtf8(std::span<const uint8_t> data) {
    return utf8::find_invalid(data.begin(), data.end()) == data.end();
}

static const auto VALID_CLOSE_CODES = std::to_array<uint16_t>({
    1000, 1001, 1002, 1003, 1007, 1008, 1009, 1010, 1011, 1015
});

Result<> Message::validate() const {
    if (this->isText()) {
        if (!isValidUtf8(this->data())) {
            return Err("Invalid UTF-8 in text message");
        }
    } else if (this->isControl()) {
        if (this->data().size() > 125) {
            return Err("Control frame payload cannot be larger than 125 bytes");
        }

        // if this is a close frame, validate the payload
        if (this->isClose()) {
            if (m_data.size() == 0) return Ok();

            std::span<const uint8_t> payload = this->data();
            uint16_t code = (payload[0] << 8) | payload[1];
            std::string_view reason((const char*)payload.data() + 2, payload.size() - 2);

            if (!isValidUtf8(reason)) {
                return Err("Invalid UTF-8 in close frame reason");
            }

            bool isRfcCode = std::find(VALID_CLOSE_CODES.begin(), VALID_CLOSE_CODES.end(), code) != VALID_CLOSE_CODES.end();
            if (!(isRfcCode || (code >= 3000 && code <= 4999))) {
                return Err(fmt::format("Invalid close code: {}", code));
            }
        }
    }

    return Ok();
}

}