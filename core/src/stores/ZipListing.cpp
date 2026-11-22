#include <plynth/core/ZipListing.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <system_error>

namespace plynth {

namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kEocdSig = 0x06054b50;
constexpr std::uint32_t kZip64LocatorSig = 0x07064b50;
constexpr std::uint32_t kZip64EocdSig = 0x06064b50;
constexpr std::uint32_t kCentralSig = 0x02014b50;
constexpr std::uint64_t kMaxDirectoryBytes = 512ull * 1024 * 1024;

constexpr std::array<char32_t, 128> kCp437High = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7, 0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9, 0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA, 0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F, 0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B, 0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4, 0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248, 0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0};

// Adds one code point to a UTF-8 string.
void appendUtf8(std::string& out, char32_t cp)
{
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// Turns code page 437 bytes into UTF-8.
std::string cp437ToUtf8(const char* bytes, std::size_t n)
{
    std::string out;
    for (std::size_t i = 0; i < n; ++i) {
        const auto b = static_cast<unsigned char>(bytes[i]);
        if (b < 0x80) out += static_cast<char>(b);
        else appendUtf8(out, kCp437High[b - 0x80]);
    }
    return out;
}

std::uint16_t u16(const unsigned char* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }

std::uint32_t u32(const unsigned char* p)
{
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) | (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint64_t u64(const unsigned char* p) { return static_cast<std::uint64_t>(u32(p)) | (static_cast<std::uint64_t>(u32(p + 4)) << 32); }

ZipListing damaged() { return {fail(ErrorCode::BadFile, "The zip file is damaged."), {}}; }

// Reads bytes at an offset. False when the file is too short.
bool readAt(std::ifstream& in, std::uint64_t offset, std::vector<unsigned char>& buffer, std::size_t count)
{
    buffer.resize(count);
    in.clear();
    in.seekg(static_cast<std::streamoff>(offset));
    in.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(count));
    return static_cast<std::size_t>(in.gcount()) == count;
}

// Says whether a name could write outside the folder it is unpacked into.
bool isUnsafeName(const std::string& name)
{
    if (name.empty()) return false;
    if (name[0] == '/') return true;
    if (name.size() >= 2 && name[1] == ':' && std::isalpha(static_cast<unsigned char>(name[0]))) return true;
    std::size_t start = 0;
    while (start <= name.size()) {
        std::size_t end = name.find('/', start);
        if (end == std::string::npos) end = name.size();
        if (name.compare(start, end - start, "..") == 0) return true;
        start = end + 1;
    }
    return false;
}

}

ZipListing listZipEntries(const fs::path& file)
{
    std::error_code ec;
    if (!fs::exists(file, ec)) return {fail(ErrorCode::NotFound, "The zip file does not exist: " + file.string()), {}};
    std::ifstream in(file, std::ios::binary);
    if (!in) return {fail(ErrorCode::CannotRead, "The zip file cannot be opened: " + file.string()), {}};
    const std::uint64_t fileSize = fs::file_size(file, ec);
    if (ec) return {fail(ErrorCode::CannotRead, "The zip file cannot be read: " + file.string()), {}};

    const auto notZip = [] { return ZipListing{fail(ErrorCode::BadFile, "This is not a zip file."), {}}; };
    if (fileSize < 22) return notZip();

    const std::uint64_t tailSize = std::min<std::uint64_t>(fileSize, 22 + 65535);
    const std::uint64_t tailStart = fileSize - tailSize;
    std::vector<unsigned char> tail;
    if (!readAt(in, tailStart, tail, static_cast<std::size_t>(tailSize)))
        return {fail(ErrorCode::CannotRead, "The zip file cannot be read: " + file.string()), {}};

    std::size_t eocd = tail.size();
    for (std::size_t i = tail.size() - 21; i-- > 0;) {
        if (u32(&tail[i]) == kEocdSig && i + 22 + u16(&tail[i + 20]) <= tail.size()) {
            eocd = i;
            break;
        }
    }
    if (eocd == tail.size()) return notZip();

    std::uint64_t count = u16(&tail[eocd + 10]);
    std::uint64_t dirSize = u32(&tail[eocd + 12]);
    std::uint64_t dirOffset = u32(&tail[eocd + 16]);
    const std::uint64_t eocdPos = tailStart + eocd;

    if (count == 0xFFFF || dirSize == 0xFFFFFFFFull || dirOffset == 0xFFFFFFFFull) {
        if (eocdPos < 20) return damaged();
        std::vector<unsigned char> locator;
        if (!readAt(in, eocdPos - 20, locator, 20) || u32(locator.data()) != kZip64LocatorSig) return damaged();
        const std::uint64_t recordPos = u64(&locator[8]);
        std::vector<unsigned char> record;
        if (recordPos > fileSize || !readAt(in, recordPos, record, 56) || u32(record.data()) != kZip64EocdSig) return damaged();
        count = u64(&record[32]);
        dirSize = u64(&record[40]);
        dirOffset = u64(&record[48]);
    }

    if (count > kMaxZipEntries) return {fail(ErrorCode::BadFile, "The zip file holds too many files."), {}};
    if (dirOffset > fileSize || dirSize > fileSize - dirOffset || dirSize > kMaxDirectoryBytes || count * 46 > dirSize) return damaged();

    std::vector<unsigned char> dir;
    if (!readAt(in, dirOffset, dir, static_cast<std::size_t>(dirSize))) return damaged();

    ZipListing result;
    std::size_t pos = 0;
    for (std::uint64_t n = 0; n < count; ++n) {
        if (pos + 46 > dir.size() || u32(&dir[pos]) != kCentralSig) return damaged();
        const unsigned char* h = &dir[pos];
        const std::uint16_t flags = u16(h + 8);
        std::uint64_t compressed = u32(h + 20);
        std::uint64_t size = u32(h + 24);
        const std::size_t nameLen = u16(h + 28);
        const std::size_t extraLen = u16(h + 30);
        const std::size_t commentLen = u16(h + 32);
        if (pos + 46 + nameLen + extraLen + commentLen > dir.size()) return damaged();
        const char* nameBytes = reinterpret_cast<const char*>(h + 46);

        const unsigned char* extra = h + 46 + nameLen;
        std::size_t e = 0;
        while (e + 4 <= extraLen) {
            const std::uint16_t id = u16(extra + e);
            const std::size_t len = u16(extra + e + 2);
            if (e + 4 + len > extraLen) return damaged();
            if (id == 0x0001) {
                std::size_t at = e + 4;
                const std::size_t end = e + 4 + len;
                if (size == 0xFFFFFFFFull) {
                    if (at + 8 > end) return damaged();
                    size = u64(extra + at);
                    at += 8;
                }
                if (compressed == 0xFFFFFFFFull) {
                    if (at + 8 > end) return damaged();
                    compressed = u64(extra + at);
                }
            }
            e += 4 + len;
        }

        ZipEntry entry;
        std::size_t used = nameLen;
        const void* nul = std::memchr(nameBytes, 0, nameLen);
        if (nul) used = static_cast<std::size_t>(static_cast<const char*>(nul) - nameBytes);
        entry.name = (flags & 0x0800) ? std::string(nameBytes, used) : cp437ToUtf8(nameBytes, used);
        std::replace(entry.name.begin(), entry.name.end(), '\\', '/');
        entry.size = size;
        entry.compressedSize = compressed;
        entry.folder = !entry.name.empty() && entry.name.back() == '/';
        entry.encrypted = (flags & 1) != 0;
        entry.unsafe = isUnsafeName(entry.name);
        result.entries.push_back(std::move(entry));
        pos += 46 + nameLen + extraLen + commentLen;
    }
    return result;
}

}
