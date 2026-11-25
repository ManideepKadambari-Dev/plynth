#include "UpdateInternals.h"

#include <cstring>

namespace plynth::update_detail {

namespace {

// Reads a little endian number at an offset. False when it does not fit in the bytes.
template <class T>
bool readAt(std::string_view bytes, std::size_t offset, T& out)
{
    if (offset > bytes.size() || bytes.size() - offset < sizeof(T)) return false;
    std::memcpy(&out, bytes.data() + offset, sizeof(T));
    return true;
}

// The file offset of a relative address, through the section table. Empty when no section holds it.
std::optional<std::size_t> fileOffset(std::string_view bytes, std::size_t sections, unsigned count, std::uint32_t rva)
{
    for (unsigned i = 0; i < count; ++i) {
        const std::size_t at = sections + i * 40u;
        std::uint32_t virtualSize = 0, virtualAddress = 0, rawSize = 0, rawPointer = 0;
        if (!readAt(bytes, at + 8, virtualSize) || !readAt(bytes, at + 12, virtualAddress) ||
            !readAt(bytes, at + 16, rawSize) || !readAt(bytes, at + 20, rawPointer)) {
            return std::nullopt;
        }
        const std::uint32_t span = virtualSize > rawSize ? virtualSize : rawSize;
        if (rva >= virtualAddress && rva - virtualAddress < span) {
            const std::uint64_t offset = static_cast<std::uint64_t>(rawPointer) + (rva - virtualAddress);
            if (offset >= bytes.size()) return std::nullopt;
            return static_cast<std::size_t>(offset);
        }
    }
    return std::nullopt;
}

// The offset (from the resource root) of an entry of a resource directory: the one with the given id, or the
// first one when id is negative.
std::optional<std::uint32_t> directoryEntry(std::string_view bytes, std::size_t directory, long id)
{
    std::uint16_t named = 0, ids = 0;
    if (!readAt(bytes, directory + 12, named) || !readAt(bytes, directory + 14, ids)) return std::nullopt;
    const unsigned total = static_cast<unsigned>(named) + ids;
    for (unsigned i = 0; i < total; ++i) {
        std::uint32_t name = 0, offset = 0;
        if (!readAt(bytes, directory + 16 + i * 8u, name) || !readAt(bytes, directory + 20 + i * 8u, offset)) {
            return std::nullopt;
        }
        if (id < 0 || (!(name & 0x80000000u) && name == static_cast<std::uint32_t>(id))) return offset;
    }
    return std::nullopt;
}

}

// Finds RT_VERSION in the resources of a Windows program and reads the file version of its fixed part.
std::optional<Version> programVersion(std::string_view bytes)
{
    std::uint16_t mz = 0;
    std::uint32_t peOffset = 0, peSignature = 0;
    if (!readAt(bytes, 0, mz) || mz != 0x5a4d || !readAt(bytes, 0x3c, peOffset)) return std::nullopt;
    if (!readAt(bytes, peOffset, peSignature) || peSignature != 0x00004550) return std::nullopt;
    const std::size_t fileHeader = static_cast<std::size_t>(peOffset) + 4;
    std::uint16_t sectionCount = 0, optionalSize = 0, magic = 0;
    if (!readAt(bytes, fileHeader + 2, sectionCount) || !readAt(bytes, fileHeader + 16, optionalSize)) return std::nullopt;
    const std::size_t optionalHeader = fileHeader + 20;
    if (!readAt(bytes, optionalHeader, magic)) return std::nullopt;
    std::size_t directories = 0;
    if (magic == 0x10b) directories = optionalHeader + 96;
    else if (magic == 0x20b) directories = optionalHeader + 112;
    else return std::nullopt;
    std::uint32_t resourceRva = 0, resourceSize = 0;
    if (!readAt(bytes, directories + 2 * 8, resourceRva) || !readAt(bytes, directories + 2 * 8 + 4, resourceSize) ||
        resourceRva == 0) {
        return std::nullopt;
    }
    const std::size_t sections = optionalHeader + optionalSize;
    const auto root = fileOffset(bytes, sections, sectionCount, resourceRva);
    if (!root) return std::nullopt;

    // Three levels: the type (16 is RT_VERSION), then the first name, then the first language.
    std::size_t directory = *root;
    for (const long id : {16L, -1L}) {
        const auto entry = directoryEntry(bytes, directory, id);
        if (!entry || !(*entry & 0x80000000u)) return std::nullopt;
        directory = *root + (*entry & 0x7fffffffu);
    }
    const auto leaf = directoryEntry(bytes, directory, -1);
    if (!leaf || (*leaf & 0x80000000u)) return std::nullopt;
    directory = *root + *leaf;
    // directory now points at the data entry: the address and size of the version block.
    std::uint32_t dataRva = 0, dataSize = 0;
    if (!readAt(bytes, directory, dataRva) || !readAt(bytes, directory + 4, dataSize)) return std::nullopt;
    const auto data = fileOffset(bytes, sections, sectionCount, dataRva);
    if (!data || dataSize > bytes.size() - *data) return std::nullopt;
    const std::string_view block = bytes.substr(*data, dataSize);
    for (std::size_t at = 0; at + 52 <= block.size(); at += 4) {
        std::uint32_t signature = 0;
        readAt(block, at, signature);
        if (signature != 0xfeef04bdu) continue;
        std::uint32_t high = 0, low = 0;
        readAt(block, at + 8, high);
        readAt(block, at + 12, low);
        return Version{high >> 16, high & 0xffffu, low >> 16};
    }
    return std::nullopt;
}

}
