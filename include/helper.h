#pragma once
#include <string>
#include <cstdint>
using namespace std;

// Helper.h should contain the structs, functions, constants, etc. that are used more than one time in the project. For example, the structs that are used in both server and client, the constants that are used in all three files. 

struct FuncEntry
{
    string funcName;
    int64_t byteOffsetInResolveBin; // where this function's FUNC header record sits
};
struct PendingPatch
{
    int64_t byteOffsetOfOffsetField; // where in resolve.bin to seek back and overwrite
    string targetFuncName;
};
class Crc32
{
    uint32_t crc;

public:
    Crc32()
    {
        crc = 0xFFFFFFFF;
    }
    void update(const char *buf, size_t n)
    {
        for (size_t i = 0; i < n; i++)
        {
            crc ^= (uint8_t)buf[i];
            for (int j = 0; j < 8; j++)
            {
                if (crc & 1)
                    crc = (crc >> 1) ^ 0xEDB88320;
                else
                    crc = crc >> 1;
            }
        }
    }
    uint32_t finish() const
    {
        return ~crc;
    }
};

inline void crcToBytes(uint32_t v, char out[4])
{
    out[0] = (char)(v >> 24);
    out[1] = (char)(v >> 16);
    out[2] = (char)(v >> 8);
    out[3] = (char)v;
}

class TrailerChecker
{
    Crc32 crc;
    string held;

public:
    string feed(const char *buf, size_t n)
    {
        held.append(buf, n);
        if (held.size() <= 4)
            return "";
        size_t releaseCount = held.size() - 4;
        string released = held.substr(0, releaseCount);
        held.erase(0, releaseCount);
        crc.update(released.data(), released.size());
        return released;
    }
    bool verify() const
    {
        if (held.size() != 4)
            return false;
        char expected[4];
        crcToBytes(crc.finish(), expected);
        return held.compare(0, 4, expected, 4) == 0;
    }
    const string &trailer() const
    {
        return held;
    }
};
