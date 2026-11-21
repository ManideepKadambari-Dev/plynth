#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace plynth {

// File formats a plan image may use. Detected from the first bytes of the file, not from its name.
enum class PlanImageFormat { Png, Jpeg, Bmp };

// Why a plan image was refused. None means the image is usable.
enum class PlanImageError {
    None,
    NotFound,
    CannotRead,
    Unsupported,
    Corrupt,
    Empty,
    TooLarge,
};

// The size and format of a plan image, read from its header without decoding the pixels.
struct PlanImageInfo {
    PlanImageFormat format = PlanImageFormat::Png;
    int width = 0;
    int height = 0;
};

// The answer from a probe: either usable info, or an error with a plain sentence saying why.
struct PlanImageProbe {
    PlanImageError error = PlanImageError::None;
    std::string message;
    PlanImageInfo info;

    bool ok() const { return error == PlanImageError::None; }
};

// Largest width or height accepted, in pixels.
inline constexpr int kMaxPlanImageSide = 16384;

// Largest pixel count accepted (width times height). About 8000 by 8000.
inline constexpr std::int64_t kMaxPlanImagePixels = 64'000'000;

// Checks a plan image file on disk: it exists, is a supported format, and its size is within the limits.
PlanImageProbe probePlanImage(const std::filesystem::path& file);

// Same checks on bytes already in memory, starting at the first byte of the file.
// May be the whole file or only its beginning. Too few bytes to find the size gives Corrupt.
PlanImageProbe probePlanImageBytes(std::span<const std::uint8_t> bytes);

// Lower case file name extensions offered in open dialogs and accepted on drop, without the dot.
std::span<const std::string_view> planImageExtensions();

// Says whether a file name ends in one of the accepted extensions, ignoring letter case.
bool hasPlanImageExtension(const std::filesystem::path& file);

// Short name of a format for display, for example "PNG".
std::string_view planImageFormatName(PlanImageFormat format);

}
