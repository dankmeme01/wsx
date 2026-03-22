#include <wsx/CircularByteBuffer.hpp>
#include <cstring>
#include <cassert>
#include <cstdint>
#include <algorithm>
#include <stdexcept>

#define WSX_DEBUG_ASSERT(cond) assert(cond)
#define WSX_ASSERT(cond) assert(cond)

namespace wsx {

CircularByteBuffer::CircularByteBuffer() : CircularByteBuffer(0) {}

CircularByteBuffer::CircularByteBuffer(size_t cap) {
    if (cap == 0) {
        return;
    }

    m_data = new uint8_t[cap];
    m_start = m_data;
    m_end = m_data;
    m_endAlloc = m_data + cap;
    m_size = 0;
}

CircularByteBuffer::CircularByteBuffer(const CircularByteBuffer& other) {
    *this = other;
}

CircularByteBuffer& CircularByteBuffer::operator=(const CircularByteBuffer& other) {
    if (this != &other) {
        delete[] m_data;

        size_t cap = other.capacity();
        m_data = new uint8_t[cap];
        m_endAlloc = m_data + cap;

        m_start = m_data + std::distance(other.m_data, other.m_start);
        m_end = m_data + std::distance(other.m_data, other.m_end);
        m_size = other.m_size;

        std::memcpy(m_data, other.m_data, cap);
    }

    return *this;
}

CircularByteBuffer::CircularByteBuffer(CircularByteBuffer&& other) noexcept {
    *this = std::move(other);
}

CircularByteBuffer& CircularByteBuffer::operator=(CircularByteBuffer&& other) noexcept {
    if (this != &other) {
        delete[] m_data;

        m_data = other.m_data;
        m_start = other.m_start;
        m_end = other.m_end;
        m_endAlloc = other.m_endAlloc;
        m_size = other.m_size;

        other.m_data = nullptr;
        other.m_start = nullptr;
        other.m_end = nullptr;
        other.m_endAlloc = nullptr;
        other.m_size = 0;
    }

    return *this;
}

CircularByteBuffer::~CircularByteBuffer() {
    delete[] m_data;
}

void CircularByteBuffer::clear() {
    m_start = m_data;
    m_end = m_data;
    m_size = 0;
}

void CircularByteBuffer::reserve(size_t extra) {
    this->growUntilAtLeast(this->capacity() + extra);

    WSX_DEBUG_ASSERT(this->writeWindow().size() >= extra);
}

void CircularByteBuffer::reserveUntil(size_t cap) {
    this->growUntilAtLeast(cap);

    WSX_DEBUG_ASSERT(this->capacity() >= cap);
}

size_t CircularByteBuffer::capacity() const {
    return m_endAlloc - m_data;
}

size_t CircularByteBuffer::size() const {
    return m_size;
}

bool CircularByteBuffer::empty() const {
    return m_size == 0;
}

void CircularByteBuffer::write(const void* data, size_t len) {
    return this->write(std::span{(const uint8_t*)data, len});
}

void CircularByteBuffer::write(std::span<const uint8_t> data) {
    if (data.empty()) {
        return; // nothing to write
    }

    size_t remSpace = this->capacity() - this->size();

    if (data.size() > remSpace) {
        this->growUntilAtLeast(this->size() + data.size());
    }

    WSX_DEBUG_ASSERT(this->capacity() >= this->size() + data.size());

    auto wnd1 = this->writeWindow();
    size_t write1Len = std::min<size_t>(data.size(), wnd1.size());

    std::memcpy(wnd1.data(), data.data(), write1Len);
    this->advanceWrite(write1Len);

    if (write1Len < data.size()) {
        // wrapping around
        auto wnd2 = this->writeWindow();
        size_t write2Len = data.size() - write1Len;

        WSX_DEBUG_ASSERT(wnd2.size() >= write2Len && "CircularByteBuffer::write: not enough space in the second window");

        std::memcpy(wnd2.data(), data.data() + write1Len, write2Len);
        this->advanceWrite(write2Len);
    }
}

std::span<uint8_t> CircularByteBuffer::writeWindow() {
    size_t size = m_start < m_end ?
            m_endAlloc - m_end
            : m_start - m_end;

    if (size == 0) {
        // this means m_start == m_end, which means buffer is either full or empty.
        if (m_size == 0) {
            WSX_ASSERT(m_start == m_data);
            WSX_ASSERT(m_end == m_data);

            return std::span<uint8_t>{
                m_data,
                this->capacity()
            };
        } else {
            // buffer is full
            return std::span<uint8_t>{m_end, 0};
        }
    }

    return std::span<uint8_t>{
        m_end,
        size
    };
}

void CircularByteBuffer::advanceWrite(size_t len) {
    if (len > this->capacity() - this->size()) {
        throw std::out_of_range("CircularByteBuffer::advanceWrite called with len > available space");
    }

    m_end += len;
    if (m_end >= m_endAlloc) {
        m_end -= m_endAlloc - m_data; // wrap around
    }

    m_size += len;
}

void CircularByteBuffer::read(void* dest, size_t len) {
    if (dest) {
        this->peek(dest, len);
    }

    this->skip(len);
}

void CircularByteBuffer::peek(void* dest, size_t len, size_t skip) const {
    if (len == 0) return;

    WSX_ASSERT(dest && "CircularByteBuffer::peek called with null destination");

    auto bufs = this->peek(len, skip);
    std::memcpy(dest, bufs.first.data(), bufs.first.size());

    if (!bufs.second.empty()) {
        std::memcpy((uint8_t*)dest + bufs.first.size(), bufs.second.data(), bufs.second.size());
    }
}

CircularByteBuffer::WrappedRead CircularByteBuffer::peek(size_t len, size_t skip) const {
    if (len + skip > this->size()) {
        throw std::out_of_range("CircularByteBuffer::peek called with len + skip > size()");
    }

    WrappedRead out{};

    // handle simple case
    if (m_start + skip < m_end) {
        out.first = std::span<const uint8_t>{
            m_start + skip,
            len
        };
        return out;
    }

    const uint8_t* start = m_start + skip;
    if (start >= m_endAlloc) {
        start = m_data + (start - m_endAlloc); // wrap around
    }

    out.first = std::span<const uint8_t>{
        start,
        std::min<size_t>(len, m_endAlloc - start)
    };

    size_t remaining = len - out.first.size();

    if (remaining > 0) {
        // wrap around
        out.second = std::span<const uint8_t>{
            m_data,
            remaining
        };
    }

    WSX_DEBUG_ASSERT(out.first.size() + out.second.size() == len);
    WSX_DEBUG_ASSERT(out.first.data() >= m_data && out.first.data() < m_endAlloc);
    WSX_DEBUG_ASSERT(out.second.empty() || (out.second.data() >= m_data && out.second.data() < m_endAlloc));

    return out;
}

void CircularByteBuffer::skip(size_t len) {
    if (len > this->size()) {
        throw std::out_of_range("CircularByteBuffer::skip called with len > size()");
    }

    m_start += len;
    if (m_start >= m_endAlloc) {
        m_start -= m_endAlloc - m_data; // wrap around
    }

    m_size -= len;

    // if size is 0, reset start and end to the beginning of the buffer
    if (m_size == 0) {
        m_start = m_data;
        m_end = m_data;
    }
}

void CircularByteBuffer::growUntilAtLeast(size_t newcap) {
    size_t curcap = this->capacity();

    if (curcap == 0) {
        // start with a reasonable default capacity
        curcap = 64;
    }

    while (curcap < newcap) {
        curcap *= 2;
    }

    this->growTo(curcap);
}

void CircularByteBuffer::growTo(size_t newCap) {
    auto newData = new uint8_t[newCap];

    // Reallocation may sound like a simple idea, but we need to handle the circular nature of the buffer.
    // we have four cases:
    // 1. m_start < m_end, we can just copy the data as is, and new space will be at the end
    // 2. m_start > m_end, we need to copy the data in two parts, so that new space will be in the middle
    // 3. m_start == m_end AND m_size == 0, here integrity of m_start and m_end is not important and they can even be reset to m_data
    // 4. m_start == m_end AND m_size == capacity(), just like 2, the new space has to be in the middle.

    // for simplicity sake:
    // * 1 and 3 will be handled the same way, just like 2 and 4.
    // * this function *always* makes the buffer contigous,
    // for cases 2 and 4 rather than expanding space in the middle, it will just rearrange the data to be contiguous.
    // in all cases, m_start will be set to the beginning of the allocated memory.

    if (m_start < m_end || (m_start == m_end && m_size == 0)) {
        // case 1 and 3
        std::memcpy(newData, m_start, m_size);
    } else {
        // case 2 and 4
        size_t startPartSize = m_end - m_data;
        size_t endPartSize = m_endAlloc - m_start;

        std::memcpy(newData, m_start, endPartSize);
        std::memcpy(newData + endPartSize, m_data, startPartSize);
    }

    m_start = newData;
    m_end = newData + m_size;
    m_endAlloc = newData + newCap;

    delete[] m_data;
    m_data = newData;
}

}