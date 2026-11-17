#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace zzj
{
namespace SSH
{
// Utility class for SSH protocol byte sequence operations
class ByteBuffer
{
   public:
    std::vector<uint8_t> data;
    size_t readPos = 0;

    // Write 32-bit integer (Big Endian)
    void writeUint32(uint32_t value)
    {
        data.push_back((value >> 24) & 0xFF);
        data.push_back((value >> 16) & 0xFF);
        data.push_back((value >> 8) & 0xFF);
        data.push_back(value & 0xFF);
    }

    // Write byte array (with length prefix)
    void writeString(const std::vector<uint8_t> &str)
    {
        writeUint32(static_cast<uint32_t>(str.size()));
        data.insert(data.end(), str.begin(), str.end());
    }

    void writeString(const std::string &str)
    {
        writeUint32(static_cast<uint32_t>(str.size()));
        data.insert(data.end(), str.begin(), str.end());
    }

    void writeString(const char *str)
    {
        size_t len = strlen(str);
        writeUint32(static_cast<uint32_t>(len));
        data.insert(data.end(), str, str + len);
    }

    // Write single byte
    void writeByte(uint8_t byte) { data.push_back(byte); }

    // Read 32-bit integer (Big Endian)
    uint32_t readUint32()
    {
        if (readPos + 4 > data.size()) return 0;
        uint32_t value = (data[readPos] << 24) | (data[readPos + 1] << 16) |
                         (data[readPos + 2] << 8) | data[readPos + 3];
        readPos += 4;
        return value;
    }

    // Read byte array (with length prefix)
    std::vector<uint8_t> readString()
    {
        uint32_t len = readUint32();
        if (readPos + len > data.size()) return {};

        std::vector<uint8_t> result(data.begin() + readPos, data.begin() + readPos + len);
        readPos += len;
        return result;
    }

    // Read single byte
    uint8_t readByte()
    {
        if (readPos >= data.size()) return 0;
        return data[readPos++];
    }
};

}  // namespace SSH
}  // namespace zzj

