#pragma once

#include "../Status.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace plynth {

// A plan image as grey levels, one byte per pixel, row after row from the top left corner.
// 0 is black ink and 255 is white paper. pixels holds width * height bytes.
struct GrayImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;

    bool operator==(const GrayImage&) const = default;
};

// The answer from loading a plan image: the grey image, or an error saying why not.
struct GrayImageLoad {
    Status status;
    std::shared_ptr<const GrayImage> image; // null on error
};

// Reads a plan image file and turns it into grey levels. Colours are mixed down to grey and transparent
// parts count as white paper. The file is checked with probePlanImage first, so the same formats and size
// limits apply. The file is read into memory and decoded from there, so paths with any characters work.
// NotFound when the file is missing, ImageError with a plain sentence when it is refused or cannot be decoded.
GrayImageLoad loadGrayImage(const std::filesystem::path& file);

}
