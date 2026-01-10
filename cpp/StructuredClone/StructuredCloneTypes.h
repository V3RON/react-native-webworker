#pragma once

#include <cstdint>
#include <vector>
#include <string>

namespace webworker {

/**
 * Type tags for the binary serialization format.
 * Each serialized value starts with one of these type tags.
 */
enum class CloneType : uint8_t {
    // Primitives (0x00-0x0F)
    Undefined       = 0x00,
    Null            = 0x01,
    BoolTrue        = 0x02,
    BoolFalse       = 0x03,
    Int32           = 0x04,  // 4 bytes, little-endian
    Double          = 0x05,  // 8 bytes, IEEE 754
    BigInt          = 0x06,  // 1 byte sign + 4 bytes length + N bytes magnitude
    String          = 0x07,  // 4 bytes length + UTF-8 bytes

    // Objects (0x10-0x1F)
    Object          = 0x10,  // 4 bytes property count + [key-value pairs]
    Array           = 0x11,  // 4 bytes length + [elements]
    Date            = 0x12,  // 8 bytes timestamp (as double)
    RegExp          = 0x13,  // 4 bytes pattern length + pattern + 4 bytes flags length + flags
    Map             = 0x14,  // 4 bytes size + [key-value pairs]
    Set             = 0x15,  // 4 bytes size + [values]

    // Error types (0x16-0x1F)
    Error           = 0x16,  // 4 bytes name length + name + 4 bytes message length + message
    EvalError       = 0x17,
    RangeError      = 0x18,
    ReferenceError  = 0x19,
    SyntaxError     = 0x1A,
    TypeError       = 0x1B,
    URIError        = 0x1C,

    // Binary data types (0x20-0x2F)
    ArrayBuffer         = 0x20,  // 4 bytes length + raw bytes
    DataView            = 0x21,  // buffer serialized inline + 4 bytes offset + 4 bytes length
    Int8Array           = 0x22,  // buffer serialized inline + 4 bytes offset + 4 bytes length
    Uint8Array          = 0x23,
    Uint8ClampedArray   = 0x24,
    Int16Array          = 0x25,
    Uint16Array         = 0x26,
    Int32Array          = 0x27,
    Uint32Array         = 0x28,
    Float32Array        = 0x29,
    Float64Array        = 0x2A,
    BigInt64Array       = 0x2B,
    BigUint64Array      = 0x2C,

    // References (0xF0+)
    ObjectRef       = 0xF0,  // 4 bytes reference ID (for circular references)
};

/**
 * Serialization constants
 */
namespace CloneConstants {
    // Maximum recursion depth to prevent stack overflow
    constexpr size_t MAX_DEPTH = 1000;

    // Maximum total serialized size (100 MB)
    constexpr size_t MAX_SIZE = 100 * 1024 * 1024;

    // Magic header for format validation (optional)
    constexpr uint32_t MAGIC_HEADER = 0x53434C4E;  // "SCLN"
    constexpr uint8_t FORMAT_VERSION = 1;
}

/**
 * Holds serialized data from the structured clone algorithm.
 */
struct SerializedData {
    std::vector<uint8_t> buffer;

    bool empty() const { return buffer.empty(); }

    size_t size() const { return buffer.size(); }

    const uint8_t* data() const { return buffer.data(); }
};

/**
 * Helper class for writing binary data during serialization.
 */
class WriteBuffer {
public:
    void writeU8(uint8_t value) {
        data_.push_back(value);
    }

    void writeU32(uint32_t value) {
        data_.push_back(static_cast<uint8_t>(value & 0xFF));
        data_.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        data_.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
        data_.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    }

    void writeI32(int32_t value) {
        writeU32(static_cast<uint32_t>(value));
    }

    void writeDouble(double value) {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&value);
        for (size_t i = 0; i < sizeof(double); ++i) {
            data_.push_back(bytes[i]);
        }
    }

    void writeBytes(const uint8_t* bytes, size_t length) {
        data_.insert(data_.end(), bytes, bytes + length);
    }

    void writeString(const std::string& str) {
        writeU32(static_cast<uint32_t>(str.size()));
        data_.insert(data_.end(), str.begin(), str.end());
    }

    std::vector<uint8_t> take() {
        return std::move(data_);
    }

    size_t size() const { return data_.size(); }

private:
    std::vector<uint8_t> data_;
};

/**
 * Helper class for reading binary data during deserialization.
 * Uses pointer + size internally to avoid dangling reference issues.
 */
class ReadBuffer {
public:
    explicit ReadBuffer(const std::vector<uint8_t>& data)
        : dataPtr_(data.data()), size_(data.size()), offset_(0) {}

    explicit ReadBuffer(const uint8_t* data, size_t size)
        : dataPtr_(data), size_(size), offset_(0) {}

    uint8_t readU8() {
        if (offset_ >= size_) {
            throw std::runtime_error("ReadBuffer: unexpected end of data");
        }
        return dataPtr_[offset_++];
    }

    uint32_t readU32() {
        if (offset_ + 4 > size_) {
            throw std::runtime_error("ReadBuffer: unexpected end of data");
        }
        const uint8_t* ptr = dataPtr_ + offset_;
        offset_ += 4;
        return static_cast<uint32_t>(ptr[0]) |
               (static_cast<uint32_t>(ptr[1]) << 8) |
               (static_cast<uint32_t>(ptr[2]) << 16) |
               (static_cast<uint32_t>(ptr[3]) << 24);
    }

    int32_t readI32() {
        return static_cast<int32_t>(readU32());
    }

    double readDouble() {
        if (offset_ + sizeof(double) > size_) {
            throw std::runtime_error("ReadBuffer: unexpected end of data");
        }
        double value;
        std::memcpy(&value, dataPtr_ + offset_, sizeof(double));
        offset_ += sizeof(double);
        return value;
    }

    std::string readString() {
        uint32_t length = readU32();
        if (offset_ + length > size_) {
            throw std::runtime_error("ReadBuffer: unexpected end of data");
        }
        std::string result(reinterpret_cast<const char*>(dataPtr_ + offset_), length);
        offset_ += length;
        return result;
    }

    void readBytes(uint8_t* dest, size_t length) {
        if (offset_ + length > size_) {
            throw std::runtime_error("ReadBuffer: unexpected end of data");
        }
        std::memcpy(dest, dataPtr_ + offset_, length);
        offset_ += length;
    }

    bool hasMore() const {
        return offset_ < size_;
    }

    size_t remaining() const {
        return size_ - offset_;
    }

    size_t position() const {
        return offset_;
    }

private:
    const uint8_t* dataPtr_ = nullptr;
    size_t size_ = 0;
    size_t offset_ = 0;
};

} // namespace webworker
