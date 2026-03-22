#pragma once

#include <span>
#include <stdint.h>

namespace wsx {

// Circular byte buffer implementation.
// This buffer allows efficient amortized read/write operations, which are a simple memcpy,
// while also efficiently storing the data and only reallocating when the reader is significantly behind the writer.
// Due to this, random access is not easy, `peek(size_t)` may return 2 spans if the requested data wraps around the end of the buffer.

class CircularByteBuffer {
public:
    CircularByteBuffer();
    CircularByteBuffer(size_t capacity);

    CircularByteBuffer(const CircularByteBuffer&);
    CircularByteBuffer& operator=(const CircularByteBuffer&);
    CircularByteBuffer(CircularByteBuffer&&) noexcept;
    CircularByteBuffer& operator=(CircularByteBuffer&&) noexcept;

    ~CircularByteBuffer();

    // Clears the buffer, but does not deallocate memory.
    void clear();

    // Reserves extra capacity in the buffer.
    // After calling this, `capacity()` will be greater than or equal to previous value of `capacity()` + `extraCap`.
    // There is also a guarantee that `writeWindow().size()` will be at least `extraCap` bytes after this call.
    void reserve(size_t extraCap);
    void reserveUntil(size_t cap);

    size_t capacity() const;
    size_t size() const;
    bool empty() const;

    // Appends more data to the end of the buffer. This will reallocate if `size() + len > capacity()`.
    void write(const void* data, size_t len);
    void write(std::span<const uint8_t> data);

    // Returns the window where the next write can happen.
    // This allows zero-copy writes, for example calling `recv` on a socket to write directly into the buffer.
    // Make sure to call `advanceWrite(size)` after writing to the window.
    std::span<uint8_t> writeWindow();

    void advanceWrite(size_t len);

    // Reads the next unread data from the buffer. Throws if `len > size()`.
    void read(void* dest, size_t len);
    // Like `read`, but does not remove data from the buffer.
    void peek(void* dest, size_t len, size_t skip = 0) const;

    struct WrappedRead {
        std::span<const uint8_t> first;
        std::span<const uint8_t> second;

        inline size_t size() const {
            return first.size() + second.size();
        }

        inline void skip(size_t len) {
            if (len <= first.size()) {
                first = first.subspan(len);
                return;
            }

            len -= first.size();
            first = std::span<const uint8_t>{};

            if (len <= second.size()) {
                second = second.subspan(len);
            } else {
                second = std::span<const uint8_t>{};
            }
        }
    };

    // Returns a span of the next unread data. If the data wraps around the end of the buffer, it will return two spans,
    // otherwise `.second` will be empty.
    // Throws if `len > size()`.
    WrappedRead peek(size_t len, size_t skip = 0) const;

    // Skips the next `len` bytes in the buffer. Throws if `len > size()`.
    // Identical to `read(nullptr, len)`.
    void skip(size_t len);

private:
    uint8_t* m_data = nullptr;
    uint8_t* m_start = nullptr;
    uint8_t* m_end = nullptr;
    uint8_t* m_endAlloc = nullptr;
    size_t m_size = 0;

    void growUntilAtLeast(size_t capacity);
    void growTo(size_t newCap);
};

} // namespace wsx
