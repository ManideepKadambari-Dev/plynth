#pragma once

#include <plynth/core/detection/GrayImage.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace plynth::test {

// A blank white image to draw test plans on, 100 pixels per metre in the tests that use it.
inline GrayImage whiteCanvas(int width, int height)
{
    GrayImage img;
    img.width = width;
    img.height = height;
    img.pixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 255);
    return img;
}

// Fills the box from (x0, y0) up to but not including (x1, y1) with one grey value.
inline void fillRect(GrayImage& img, int x0, int y0, int x1, int y1, std::uint8_t value = 0)
{
    for (int y = std::max(0, y0); y < std::min(img.height, y1); ++y) {
        for (int x = std::max(0, x0); x < std::min(img.width, x1); ++x) {
            img.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(img.width) + static_cast<std::size_t>(x)] = value;
        }
    }
}

// Fills a bar around the line from (x0, y0) to (x1, y1) with the given thickness, ends cut square to the line.
inline void fillBar(GrayImage& img, double x0, double y0, double x1, double y1, double thickness, std::uint8_t value = 0)
{
    const double dx = x1 - x0;
    const double dy = y1 - y0;
    const double len = std::hypot(dx, dy);
    const double ux = dx / len;
    const double uy = dy / len;
    const int minX = static_cast<int>(std::floor(std::min(x0, x1) - thickness));
    const int maxX = static_cast<int>(std::ceil(std::max(x0, x1) + thickness));
    const int minY = static_cast<int>(std::floor(std::min(y0, y1) - thickness));
    const int maxY = static_cast<int>(std::ceil(std::max(y0, y1) + thickness));
    for (int y = std::max(0, minY); y <= std::min(img.height - 1, maxY); ++y) {
        for (int x = std::max(0, minX); x <= std::min(img.width - 1, maxX); ++x) {
            const double px = x + 0.5 - x0;
            const double py = y + 0.5 - y0;
            const double along = px * ux + py * uy;
            const double across = -px * uy + py * ux;
            if (along >= 0.0 && along <= len && std::abs(across) <= thickness / 2.0) {
                img.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(img.width) + static_cast<std::size_t>(x)] = value;
            }
        }
    }
}

// The bytes of a PNG file. colorType is the PNG color type (0 grey, 2 colour, 3 palette, 4 grey with alpha,
// 6 colour with alpha), rows holds each scanline's bytes without the filter byte, palette is the PLTE data and
// transparency the tRNS data (both left out when empty). Uses uncompressed deflate blocks so the test needs no
// compression library.
inline std::vector<std::uint8_t> makePngRaw(int width, int height, int bitDepth, int colorType,
                                            const std::vector<std::uint8_t>& rows,
                                            const std::vector<std::uint8_t>& palette = {},
                                            const std::vector<std::uint8_t>& transparency = {})
{
    std::vector<std::uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    const auto be32 = [](std::vector<std::uint8_t>& v, std::uint32_t x) {
        v.push_back(static_cast<std::uint8_t>(x >> 24));
        v.push_back(static_cast<std::uint8_t>(x >> 16));
        v.push_back(static_cast<std::uint8_t>(x >> 8));
        v.push_back(static_cast<std::uint8_t>(x));
    };
    const auto crc = [](const std::vector<std::uint8_t>& data) {
        std::uint32_t c = 0xFFFFFFFFu;
        for (std::uint8_t b : data) {
            c ^= b;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? (c >> 1) ^ 0xEDB88320u : (c >> 1);
        }
        return c ^ 0xFFFFFFFFu;
    };
    const auto chunk = [&](const std::string& tag, const std::vector<std::uint8_t>& data) {
        be32(out, static_cast<std::uint32_t>(data.size()));
        std::vector<std::uint8_t> body(tag.begin(), tag.end());
        body.insert(body.end(), data.begin(), data.end());
        out.insert(out.end(), body.begin(), body.end());
        be32(out, crc(body));
    };

    std::vector<std::uint8_t> header;
    be32(header, static_cast<std::uint32_t>(width));
    be32(header, static_cast<std::uint32_t>(height));
    header.push_back(static_cast<std::uint8_t>(bitDepth));
    header.push_back(static_cast<std::uint8_t>(colorType));
    header.push_back(0);
    header.push_back(0);
    header.push_back(0);
    chunk("IHDR", header);
    if (!palette.empty()) chunk("PLTE", palette);
    if (!transparency.empty()) chunk("tRNS", transparency);

    const std::size_t rowBytes = rows.size() / static_cast<std::size_t>(height);
    std::vector<std::uint8_t> raw;
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), rows.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y) * rowBytes),
                   rows.begin() + static_cast<std::ptrdiff_t>((static_cast<std::size_t>(y) + 1) * rowBytes));
    }
    std::vector<std::uint8_t> z = {0x78, 0x01};
    std::size_t at = 0;
    do {
        const std::size_t n = std::min<std::size_t>(65535, raw.size() - at);
        const bool last = at + n >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<std::uint8_t>(n & 0xFF));
        z.push_back(static_cast<std::uint8_t>(n >> 8));
        z.push_back(static_cast<std::uint8_t>(~n & 0xFF));
        z.push_back(static_cast<std::uint8_t>((~n >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(at), raw.begin() + static_cast<std::ptrdiff_t>(at + n));
        at += n;
    } while (at < raw.size());
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (std::uint8_t v : raw) {
        a = (a + v) % 65521u;
        b = (b + a) % 65521u;
    }
    be32(z, (b << 16) | a);
    chunk("IDAT", z);
    chunk("IEND", {});
    return out;
}

// The bytes of an 8 bit PNG with 1 (grey), 3 (colour) or 4 (colour with alpha) channels, row after row.
inline std::vector<std::uint8_t> makePngBytes(int width, int height, int channels, const std::vector<std::uint8_t>& samples)
{
    return makePngRaw(width, height, 8, channels == 1 ? 0 : (channels == 3 ? 2 : 6), samples);
}

}
