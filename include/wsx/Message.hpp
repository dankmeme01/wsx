#pragma once
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
    bool isControl() const { return isPing() || isPong(); }
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

private:
    std::vector<uint8_t> m_data;
    Type m_type = Type::Invalid;
    bool m_final = true;
};


}
