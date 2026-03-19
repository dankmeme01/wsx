#pragma once
#include <Geode/Result.hpp>
#include <vector>
#include <stdint.h>
#include <string_view>
#include <stdexcept>
#include <span>

namespace wsx {

struct Message {
    enum class Type {
        Invalid,
        Fragment,
        Text,
        Binary,
        Ping,
        Pong,
        Close,
    };

    Message() = default;
    Message(const Message&) = default;
    Message& operator=(const Message&) = default;
    Message(Message&&) = default;
    Message& operator=(Message&&) = default;

    Message(std::string_view text) : m_data(text.begin(), text.end()), m_type(Type::Text) {}
    Message(std::span<const uint8_t> binary) : m_data(binary.begin(), binary.end()), m_type(Type::Binary) {}
    Message(std::vector<uint8_t>&& binary) : m_data(std::move(binary)), m_type(Type::Binary) {}

    Message(Type type, std::vector<uint8_t>&& data) : m_data(std::move(data)), m_type(type) {}

    bool isText() const { return m_type == Type::Text; }
    bool isBinary() const { return m_type == Type::Binary; }
    bool isPing() const { return m_type == Type::Ping; }
    bool isPong() const { return m_type == Type::Pong; }
    bool isControl() const { return isPing() || isPong() || isClose(); }
    bool isClose() const { return m_type == Type::Close; }

    /// Returns the inner data, valid for all message types
    std::span<const uint8_t> data() const {
        return std::span<const uint8_t>(this->m_data.data(), this->m_data.size());
    }

    /// Returns the inner data, valid for all message types
    std::vector<uint8_t> data() && {
        return std::move(m_data);
    }

    Type type() const {
        return m_type;
    }

    bool final() const {
        return m_final;
    }

    void setFinal(bool final) {
        m_final = final;
    }

    std::string_view text() const {
        if (!isText()) {
            throw std::runtime_error("Message is not text");
        }
        return std::string_view((const char*)m_data.data(), m_data.size());
    }

    std::span<const uint8_t> binary() const {
        if (!isBinary()) {
            throw std::runtime_error("Message is not binary");
        }
        return std::span<const uint8_t>(m_data.data(), m_data.size());
    }

    std::vector<uint8_t> binary() && {
        if (!isBinary()) {
            throw std::runtime_error("Message is not binary");
        }
        return std::move(m_data);
    }

    uint16_t closeCode() const {
        if (!isClose()) {
            throw std::runtime_error("Message is not close");
        }
        if (m_data.size() < 2) {
            return 1005;
        }
        return uint16_t(m_data[0] << 8) + m_data[1];
    }

    std::string_view closeReason() const {
        if (!isClose()) {
            throw std::runtime_error("Message is not close");
        }
        if (m_data.size() < 2) {
            return "";
        }
        return std::string_view((const char*)m_data.data() + 2, m_data.size() - 2);
    }

    /// Performs certain validations and returns an error if the message is invalid. Specifically:
    /// - check if Text frame is valid UTF-8
    /// - check if control message payload is <= 125 bytes
    geode::Result<> validate() const;

private:
    std::vector<uint8_t> m_data;
    Type m_type = Type::Invalid;
    bool m_final = true;
};


}
