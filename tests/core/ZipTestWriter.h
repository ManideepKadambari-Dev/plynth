#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

namespace plynth::test {

// One file of a zip made by the test writer.
struct ZipTestEntry {
    std::string name;
    std::string data;
    bool utf8 = true;       // sets the UTF-8 name flag
    bool encrypted = false; // sets the encryption flag only; the data stays plain
};

inline std::uint32_t zipCrc32(const std::string& data)
{
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    std::uint32_t crc = 0xFFFFFFFFu;
    for (const unsigned char byte : data) crc = table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

inline void putLe(std::string& out, std::uint64_t value, int bytes)
{
    for (int i = 0; i < bytes; ++i) out += static_cast<char>((value >> (8 * i)) & 0xFF);
}

// The bytes of a zip that stores every entry without compression. A claimed entry count other than the real
// one is written in a ZIP64 end record, so a test can reach the entry limit without writing every entry.
inline std::string zipBytes(const std::vector<ZipTestEntry>& entries, std::optional<std::uint64_t> claimedCount = std::nullopt)
{
    std::string body;
    std::string directory;
    for (const auto& e : entries) {
        const std::uint16_t flags = static_cast<std::uint16_t>((e.utf8 ? 0x0800 : 0) | (e.encrypted ? 1 : 0));
        const std::uint32_t crc = zipCrc32(e.data);
        const std::uint32_t offset = static_cast<std::uint32_t>(body.size());
        putLe(body, 0x04034B50, 4);
        putLe(body, 20, 2);
        putLe(body, flags, 2);
        putLe(body, 0, 2);
        putLe(body, 0, 2);
        putLe(body, 0x21, 2);
        putLe(body, crc, 4);
        putLe(body, e.data.size(), 4);
        putLe(body, e.data.size(), 4);
        putLe(body, e.name.size(), 2);
        putLe(body, 0, 2);
        body += e.name;
        body += e.data;

        putLe(directory, 0x02014B50, 4);
        putLe(directory, 20, 2);
        putLe(directory, 20, 2);
        putLe(directory, flags, 2);
        putLe(directory, 0, 2);
        putLe(directory, 0, 2);
        putLe(directory, 0x21, 2);
        putLe(directory, crc, 4);
        putLe(directory, e.data.size(), 4);
        putLe(directory, e.data.size(), 4);
        putLe(directory, e.name.size(), 2);
        putLe(directory, 0, 2);
        putLe(directory, 0, 2);
        putLe(directory, 0, 2);
        putLe(directory, 0, 2);
        putLe(directory, 0, 4);
        putLe(directory, offset, 4);
        directory += e.name;
    }

    std::string out = body;
    const std::uint64_t dirOffset = out.size();
    out += directory;
    const std::uint64_t count = claimedCount.value_or(entries.size());
    if (claimedCount) {
        const std::uint64_t recordPos = out.size();
        putLe(out, 0x06064B50, 4);
        putLe(out, 44, 8);
        putLe(out, 45, 2);
        putLe(out, 45, 2);
        putLe(out, 0, 4);
        putLe(out, 0, 4);
        putLe(out, count, 8);
        putLe(out, count, 8);
        putLe(out, directory.size(), 8);
        putLe(out, dirOffset, 8);
        putLe(out, 0x07064B50, 4);
        putLe(out, 0, 4);
        putLe(out, recordPos, 8);
        putLe(out, 1, 4);
    }
    putLe(out, 0x06054B50, 4);
    putLe(out, 0, 2);
    putLe(out, 0, 2);
    putLe(out, claimedCount ? 0xFFFF : count, 2);
    putLe(out, claimedCount ? 0xFFFF : count, 2);
    putLe(out, claimedCount ? 0xFFFFFFFFu : directory.size(), 4);
    putLe(out, claimedCount ? 0xFFFFFFFFu : dirOffset, 4);
    putLe(out, 0, 2);
    return out;
}

// Writes a stored zip to a file.
inline void writeZipFile(const std::filesystem::path& path, const std::vector<ZipTestEntry>& entries,
                         std::optional<std::uint64_t> claimedCount = std::nullopt)
{
    std::ofstream out(path, std::ios::binary);
    const std::string bytes = zipBytes(entries, claimedCount);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// A zip of small files with the given names; a name ending in "/" is a folder.
inline std::vector<ZipTestEntry> zipEntriesNamed(std::initializer_list<const char*> names)
{
    std::vector<ZipTestEntry> entries;
    for (const char* name : names) entries.push_back(ZipTestEntry{name, std::string(name).back() == '/' ? "" : "x", true, false});
    return entries;
}

}
