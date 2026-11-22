#pragma once

#include "Status.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace plynth {

// Reading the list of files inside a zip without unpacking it. Only the zip's central
// directory is read, so nothing is decompressed and no zip library is needed. Unpacking stays with the
// Blender worker (blender/plynth/importers/zip.py), which uses the same names, so core can decide which
// entries the worker should take out. Handles ZIP64 archives and entries.

// One file or folder inside a zip.
struct ZipEntry {
    // The path inside the zip with forward slashes, as Python's zipfile gives it: UTF-8 when the entry's
    // UTF-8 flag (bit 11) is set, otherwise decoded from code page 437.
    std::string name;
    std::uint64_t size = 0;           // unpacked size in bytes
    std::uint64_t compressedSize = 0;
    bool folder = false;              // the name ends with "/"
    bool encrypted = false;           // bit 0 of the flags; the worker cannot unpack it
    bool unsafe = false;              // an absolute path, a drive letter or a ".." part; never unpacked

    bool operator==(const ZipEntry&) const = default;
};

// The most entries read from one zip. More is BadFile, so a hostile file cannot use up memory.
inline constexpr std::size_t kMaxZipEntries = 100000;

struct ZipListing {
    Status status;
    std::vector<ZipEntry> entries; // in the order of the central directory
};

// Lists the entries of a zip file. NotFound when the file is missing, CannotRead when it cannot be opened,
// BadFile "This is not a zip file." when no end of central directory record is found, BadFile "The zip file
// is damaged." when the directory runs past the end of the file or a record is broken, BadFile when there are
// more than kMaxZipEntries entries.
ZipListing listZipEntries(const std::filesystem::path& file);

}
