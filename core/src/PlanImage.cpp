#include "plynth/core/PlanImage.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace plynth {

namespace {

struct Parsed {
    PlanImageProbe probe;
    // True when more bytes from the file could change the answer.
    bool needMore = false;
};

// Builds a failed result carrying a plain sentence.
Parsed fail(PlanImageError error, std::string message, bool needMore = false) {
    Parsed p;
    p.probe.error = error;
    p.probe.message = std::move(message);
    p.needMore = needMore;
    return p;
}

// Builds the result for a file that ran out of bytes before its size was found.
Parsed truncated(bool needMore) {
    return fail(PlanImageError::Corrupt, "The image file is damaged or cut short, so its size cannot be read.", needMore);
}

// Reads a big endian number of the given byte count.
std::int64_t readBe(std::span<const std::uint8_t> b, std::size_t at, std::size_t count) {
    std::int64_t v = 0;
    for (std::size_t i = 0; i < count; ++i) v = (v << 8) | b[at + i];
    return v;
}

// Reads a little endian number of the given byte count.
std::int64_t readLe(std::span<const std::uint8_t> b, std::size_t at, std::size_t count) {
    std::int64_t v = 0;
    for (std::size_t i = count; i > 0; --i) v = (v << 8) | b[at + i - 1];
    return v;
}

// Checks the size against the limits and builds the final result.
Parsed finish(PlanImageFormat format, std::int64_t w, std::int64_t h) {
    if (w <= 0 || h <= 0) return fail(PlanImageError::Empty, "The image has no pixels: its width or height is zero.");
    const std::string size = std::to_string(w) + " x " + std::to_string(h);
    if (w > kMaxPlanImageSide || h > kMaxPlanImageSide) {
        return fail(PlanImageError::TooLarge, "The image is " + size + " pixels. The largest side allowed is "
                                                  + std::to_string(kMaxPlanImageSide) + " pixels.");
    }
    if (w * h > kMaxPlanImagePixels) {
        return fail(PlanImageError::TooLarge, "The image is " + size + " pixels. The most pixels allowed is "
                                                  + std::to_string(kMaxPlanImagePixels) + " in total.");
    }
    Parsed p;
    p.probe.info.format = format;
    p.probe.info.width = static_cast<int>(w);
    p.probe.info.height = static_cast<int>(h);
    return p;
}

// Says whether the bytes start with the signature, allowing for fewer bytes than the signature has.
bool startsWith(std::span<const std::uint8_t> b, std::span<const std::uint8_t> sig) {
    const std::size_t n = std::min(b.size(), sig.size());
    return std::equal(b.begin(), b.begin() + static_cast<std::ptrdiff_t>(n), sig.begin());
}

// Reads the size from a PNG header.
Parsed parsePng(std::span<const std::uint8_t> b) {
    if (b.size() < 24) return truncated(true);
    const std::array<std::uint8_t, 4> ihdr = {'I', 'H', 'D', 'R'};
    if (!std::equal(ihdr.begin(), ihdr.end(), b.begin() + 12)) {
        return fail(PlanImageError::Corrupt, "The PNG file is damaged: its header block is missing.");
    }
    return finish(PlanImageFormat::Png, readBe(b, 16, 4), readBe(b, 20, 4));
}

// Reads the size from a JPEG by walking its segments to the first start of frame.
Parsed parseJpeg(std::span<const std::uint8_t> b) {
    std::size_t pos = 2;
    while (true) {
        if (pos >= b.size()) return truncated(true);
        if (b[pos] != 0xFF) return fail(PlanImageError::Corrupt, "The JPEG file is damaged: a segment marker is out of place.");
        while (pos < b.size() && b[pos] == 0xFF) ++pos;
        if (pos >= b.size()) return truncated(true);
        const std::uint8_t marker = b[pos++];

        if (marker == 0x00) return fail(PlanImageError::Corrupt, "The JPEG file is damaged: a segment marker is out of place.");
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD8)) continue;
        if (marker == 0xD9 || marker == 0xDA) {
            return fail(PlanImageError::Corrupt, "The JPEG file is damaged: it has no size information.");
        }

        if (pos + 2 > b.size()) return truncated(true);
        const std::int64_t length = readBe(b, pos, 2);
        if (length < 2) return fail(PlanImageError::Corrupt, "The JPEG file is damaged: a segment has an invalid length.");

        const bool isFrame = marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC;
        if (isFrame) {
            if (length < 7) return fail(PlanImageError::Corrupt, "The JPEG file is damaged: its frame header is too short.");
            if (pos + 7 > b.size()) return truncated(true);
            return finish(PlanImageFormat::Jpeg, readBe(b, pos + 5, 2), readBe(b, pos + 3, 2));
        }
        pos += static_cast<std::size_t>(length);
    }
}

// Reads the size from a BMP header, for both the old and the common header layouts.
Parsed parseBmp(std::span<const std::uint8_t> b) {
    if (b.size() < 18) return truncated(true);
    const std::int64_t dibSize = readLe(b, 14, 4);
    if (dibSize == 12) {
        if (b.size() < 22) return truncated(true);
        return finish(PlanImageFormat::Bmp, readLe(b, 18, 2), readLe(b, 20, 2));
    }
    if (dibSize < 40) return fail(PlanImageError::Corrupt, "The BMP file is damaged: its header size is not valid.");
    if (b.size() < 26) return truncated(true);
    const auto w = static_cast<std::int32_t>(readLe(b, 18, 4));
    const auto h = static_cast<std::int32_t>(readLe(b, 22, 4));
    if (w < 0) return fail(PlanImageError::Corrupt, "The BMP file is damaged: its width is negative.");
    const std::int64_t height = h < 0 ? -static_cast<std::int64_t>(h) : h;
    return finish(PlanImageFormat::Bmp, w, height);
}

// Finds the format from the first bytes and reads the size.
Parsed parse(std::span<const std::uint8_t> b) {
    if (b.empty()) return fail(PlanImageError::Empty, "The file is empty.");
    static constexpr std::array<std::uint8_t, 8> pngSig = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    static constexpr std::array<std::uint8_t, 2> jpegSig = {0xFF, 0xD8};
    static constexpr std::array<std::uint8_t, 2> bmpSig = {'B', 'M'};

    if (startsWith(b, pngSig)) return b.size() < pngSig.size() ? truncated(true) : parsePng(b);
    if (startsWith(b, jpegSig)) return b.size() < jpegSig.size() ? truncated(true) : parseJpeg(b);
    if (startsWith(b, bmpSig)) return b.size() < bmpSig.size() ? truncated(true) : parseBmp(b);
    return fail(PlanImageError::Unsupported, "This file is not a PNG, JPEG or BMP image.");
}

}

// Checks a plan image file on disk, reading only as much of it as needed to find the size.
PlanImageProbe probePlanImage(const std::filesystem::path& file) {
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) {
        PlanImageProbe p;
        p.error = ec ? PlanImageError::CannotRead : PlanImageError::NotFound;
        p.message = ec ? "The file cannot be read." : "The file was not found.";
        return p;
    }
    if (std::filesystem::is_directory(file, ec)) {
        PlanImageProbe p;
        p.error = PlanImageError::CannotRead;
        p.message = "This is a folder, not an image file.";
        return p;
    }

    std::ifstream in(file, std::ios::binary);
    if (!in) {
        PlanImageProbe p;
        p.error = PlanImageError::CannotRead;
        p.message = "The file cannot be opened. It may be in use or protected.";
        return p;
    }

    std::vector<std::uint8_t> buffer;
    std::size_t chunk = 64 * 1024;
    while (true) {
        const std::size_t old = buffer.size();
        buffer.resize(old + chunk);
        in.read(reinterpret_cast<char*>(buffer.data() + old), static_cast<std::streamsize>(chunk));
        const auto got = static_cast<std::size_t>(in.gcount());
        buffer.resize(old + got);
        const bool atEnd = got < chunk;

        if (in.bad()) {
            PlanImageProbe p;
            p.error = PlanImageError::CannotRead;
            p.message = "The file cannot be read.";
            return p;
        }

        Parsed parsed = parse(buffer);
        if (!parsed.needMore || atEnd) return std::move(parsed.probe);
        chunk *= 2;
    }
}

// Same checks on bytes already in memory.
PlanImageProbe probePlanImageBytes(std::span<const std::uint8_t> bytes) {
    return parse(bytes).probe;
}

// Lists the file name extensions offered in open dialogs.
std::span<const std::string_view> planImageExtensions() {
    static constexpr std::array<std::string_view, 4> list = {"png", "jpg", "jpeg", "bmp"};
    return list;
}

// Says whether a file name ends in an accepted extension, ignoring letter case.
bool hasPlanImageExtension(const std::filesystem::path& file) {
    const auto ext = file.extension().u8string();
    if (ext.size() < 2) return false;
    std::string lower;
    for (std::size_t i = 1; i < ext.size(); ++i) {
        const auto c = static_cast<unsigned char>(ext[i]);
        lower.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : static_cast<char>(c));
    }
    const auto list = planImageExtensions();
    return std::find(list.begin(), list.end(), std::string_view(lower)) != list.end();
}

// Gives the short display name of a format.
std::string_view planImageFormatName(PlanImageFormat format) {
    switch (format) {
    case PlanImageFormat::Png: return "PNG";
    case PlanImageFormat::Jpeg: return "JPEG";
    case PlanImageFormat::Bmp: return "BMP";
    }
    return "";
}

}
