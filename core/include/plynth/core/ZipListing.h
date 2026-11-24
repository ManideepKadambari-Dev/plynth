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
    // Never unpacked. The worker (importers/zip.py) has the same rule. Unsafe is: a leading slash, a colon
    // anywhere, a ".." part, a part ending in a dot or a space (a "." part alone is fine), or a part that names
    // a Windows device: CON, PRN, AUX, NUL, CONIN$, CONOUT$, COM or LPT with 1 to 9 or a superscript 1 to 3, in
    // any letter case, also with spaces or an ending after it ("nul.png"). Console.png and a..b.png are safe.
    bool unsafe = false;

    bool operator==(const ZipEntry&) const = default;
};

// The most entries read from one zip. More is BadFile, so a hostile file cannot use up memory.
inline constexpr std::size_t kMaxZipEntries = 100000;

struct ZipListing {
    Status status;
    std::vector<ZipEntry> entries; // in the order of the central directory
    // The listing failed on a name that is flagged as UTF-8 and is not. The Fab importers still score such a
    // zip, so their plan gives that sentence and no importer hands the zip to Blender.
    bool badName = false;
};

// Lists the entries of a zip file. NotFound when the file is missing, CannotRead when it cannot be opened,
// BadFile "This is not a zip file." when no end of central directory record is found, BadFile "The zip file
// is damaged." when the directory runs past the end of the file or a record is broken, BadFile when there are
// more than kMaxZipEntries entries. When a name flagged as UTF-8 is not valid UTF-8 (overlong forms and
// surrogates are not): BadFile "The zip file holds a file name that is not valid UTF-8.", badName and no
// entries. Every name and message returned is valid UTF-8, the zip's own path included. Never throws for a
// file's name or content.
ZipListing listZipEntries(const std::filesystem::path& file);

}
