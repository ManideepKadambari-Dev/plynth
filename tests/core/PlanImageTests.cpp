#include <catch2/catch_test_macros.hpp>

#include "plynth/core/PlanImage.h"

#include <process.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace plynth;
using Bytes = std::vector<std::uint8_t>;

namespace {

// Appends a number as big endian bytes.
void putBe(Bytes& b, std::uint32_t v, int count) {
    for (int i = count - 1; i >= 0; --i) b.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

// Appends a number as little endian bytes.
void putLe(Bytes& b, std::uint32_t v, int count) {
    for (int i = 0; i < count; ++i) b.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

// Builds the start of a PNG file with the given size.
Bytes makePng(std::uint32_t w, std::uint32_t h) {
    Bytes b = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    putBe(b, 13, 4);
    b.insert(b.end(), {'I', 'H', 'D', 'R'});
    putBe(b, w, 4);
    putBe(b, h, 4);
    b.insert(b.end(), {8, 0, 0, 0, 0});
    return b;
}

// Builds the start of a JPEG with a filler segment of the given size before the frame header.
Bytes makeJpeg(std::uint16_t w, std::uint16_t h, std::size_t padding = 0, std::uint8_t sof = 0xC0) {
    Bytes b = {0xFF, 0xD8};
    while (padding > 0) {
        const std::size_t part = std::min<std::size_t>(padding, 60000);
        b.insert(b.end(), {0xFF, 0xE1});
        putBe(b, static_cast<std::uint32_t>(part + 2), 2);
        b.insert(b.end(), part, 0x11);
        padding -= part;
    }
    b.insert(b.end(), {0xFF, 0xFF, sof});
    putBe(b, 11, 2);
    b.push_back(8);
    putBe(b, h, 2);
    putBe(b, w, 2);
    b.insert(b.end(), {1, 1, 0x11, 0});
    return b;
}

// Builds a BMP header with the given DIB header size and size fields.
Bytes makeBmp(std::int32_t w, std::int32_t h, std::uint32_t dib = 40) {
    Bytes b = {'B', 'M'};
    putLe(b, 100, 4);
    putLe(b, 0, 4);
    putLe(b, 54, 4);
    putLe(b, dib, 4);
    if (dib == 12) {
        putLe(b, static_cast<std::uint32_t>(w), 2);
        putLe(b, static_cast<std::uint32_t>(h), 2);
        putLe(b, 1, 2);
        putLe(b, 24, 2);
    } else {
        putLe(b, static_cast<std::uint32_t>(w), 4);
        putLe(b, static_cast<std::uint32_t>(h), 4);
        putLe(b, 1, 2);
        putLe(b, 24, 2);
        b.insert(b.end(), dib - 16, 0);
    }
    return b;
}

// Writes bytes to a file in the temp folder and removes it at the end of the scope.
struct TempFile {
    std::filesystem::path path;
    TempFile(const std::string& name, const Bytes& bytes, std::size_t extraZeros = 0) {
        path = std::filesystem::temp_directory_path() / ("plynth_test_" + std::to_string(_getpid()) + "_" + name);
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        const Bytes zeros(64 * 1024, 0);
        for (std::size_t done = 0; done < extraZeros; done += zeros.size()) {
            out.write(reinterpret_cast<const char*>(zeros.data()), static_cast<std::streamsize>(zeros.size()));
        }
    }
    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

}

TEST_CASE("PlanImage: PNG size is read from the header", "[planimage]") {
    const auto r = probePlanImageBytes(makePng(2400, 1600));
    REQUIRE(r.ok());
    CHECK(r.info.format == PlanImageFormat::Png);
    CHECK(r.info.width == 2400);
    CHECK(r.info.height == 1600);
}

TEST_CASE("PlanImage: PNG with a damaged header block is Corrupt", "[planimage]") {
    auto b = makePng(10, 10);
    b[13] = 'X';
    CHECK(probePlanImageBytes(b).error == PlanImageError::Corrupt);
}

TEST_CASE("PlanImage: PNG cut short is Corrupt", "[planimage]") {
    auto b = makePng(10, 10);
    b.resize(20);
    const auto r = probePlanImageBytes(b);
    CHECK(r.error == PlanImageError::Corrupt);
    CHECK_FALSE(r.message.empty());
}

TEST_CASE("PlanImage: JPEG size is read from the frame header", "[planimage]") {
    const auto r = probePlanImageBytes(makeJpeg(1920, 1080));
    REQUIRE(r.ok());
    CHECK(r.info.format == PlanImageFormat::Jpeg);
    CHECK(r.info.width == 1920);
    CHECK(r.info.height == 1080);
}

TEST_CASE("PlanImage: JPEG skips a large metadata segment and fill bytes", "[planimage]") {
    const auto r = probePlanImageBytes(makeJpeg(800, 600, 100000));
    REQUIRE(r.ok());
    CHECK(r.info.width == 800);
    CHECK(r.info.height == 600);
}

TEST_CASE("PlanImage: JPEG progressive frame marker is accepted", "[planimage]") {
    const auto r = probePlanImageBytes(makeJpeg(640, 480, 0, 0xC2));
    REQUIRE(r.ok());
    CHECK(r.info.width == 640);
}

TEST_CASE("PlanImage: JPEG with no frame header is Corrupt", "[planimage]") {
    Bytes b = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x04, 0x00, 0x00};
    CHECK(probePlanImageBytes(b).error == PlanImageError::Corrupt);
    Bytes endOnly = {0xFF, 0xD8, 0xFF, 0xD9};
    CHECK(probePlanImageBytes(endOnly).error == PlanImageError::Corrupt);
}

TEST_CASE("PlanImage: JPEG cut inside the frame header is Corrupt", "[planimage]") {
    auto b = makeJpeg(100, 100);
    b.resize(b.size() - 8);
    CHECK(probePlanImageBytes(b).error == PlanImageError::Corrupt);
}

TEST_CASE("PlanImage: BMP with a 40 byte header", "[planimage]") {
    const auto r = probePlanImageBytes(makeBmp(300, 200));
    REQUIRE(r.ok());
    CHECK(r.info.format == PlanImageFormat::Bmp);
    CHECK(r.info.width == 300);
    CHECK(r.info.height == 200);
}

TEST_CASE("PlanImage: BMP top down height is made positive", "[planimage]") {
    const auto r = probePlanImageBytes(makeBmp(300, -200));
    REQUIRE(r.ok());
    CHECK(r.info.height == 200);
}

TEST_CASE("PlanImage: BMP with a 12 byte header", "[planimage]") {
    const auto r = probePlanImageBytes(makeBmp(64, 32, 12));
    REQUIRE(r.ok());
    CHECK(r.info.width == 64);
    CHECK(r.info.height == 32);
}

TEST_CASE("PlanImage: BMP with a 124 byte header", "[planimage]") {
    const auto r = probePlanImageBytes(makeBmp(50, 60, 124));
    REQUIRE(r.ok());
    CHECK(r.info.width == 50);
    CHECK(r.info.height == 60);
}

TEST_CASE("PlanImage: BMP with an odd header size or cut short is Corrupt", "[planimage]") {
    CHECK(probePlanImageBytes(makeBmp(10, 10, 20)).error == PlanImageError::Corrupt);
    auto b = makeBmp(10, 10);
    b.resize(24);
    CHECK(probePlanImageBytes(b).error == PlanImageError::Corrupt);
}

TEST_CASE("PlanImage: unknown bytes are Unsupported", "[planimage]") {
    const std::string text = "hello, this is plain text";
    const Bytes b(text.begin(), text.end());
    const auto r = probePlanImageBytes(b);
    CHECK(r.error == PlanImageError::Unsupported);
    CHECK_FALSE(r.message.empty());
}

TEST_CASE("PlanImage: no bytes is Empty and a lone signature byte is Corrupt", "[planimage]") {
    CHECK(probePlanImageBytes({}).error == PlanImageError::Empty);
    const Bytes one = {0x89};
    CHECK(probePlanImageBytes(one).error == PlanImageError::Corrupt);
}

TEST_CASE("PlanImage: zero width or height is Empty", "[planimage]") {
    CHECK(probePlanImageBytes(makePng(0, 100)).error == PlanImageError::Empty);
    CHECK(probePlanImageBytes(makePng(100, 0)).error == PlanImageError::Empty);
    CHECK(probePlanImageBytes(makeBmp(0, 5)).error == PlanImageError::Empty);
    CHECK(probePlanImageBytes(makeJpeg(0, 5)).error == PlanImageError::Empty);
}

TEST_CASE("PlanImage: limits are inclusive", "[planimage]") {
    CHECK(probePlanImageBytes(makePng(kMaxPlanImageSide, 1)).ok());
    CHECK(probePlanImageBytes(makePng(8000, 8000)).ok());
    CHECK(probePlanImageBytes(makePng(kMaxPlanImageSide + 1, 1)).error == PlanImageError::TooLarge);
}

TEST_CASE("PlanImage: a side over the limit names both sizes", "[planimage]") {
    const auto r = probePlanImageBytes(makePng(20000, 3000));
    CHECK(r.error == PlanImageError::TooLarge);
    CHECK(r.message == "The image is 20000 x 3000 pixels. The largest side allowed is 16384 pixels.");
}

TEST_CASE("PlanImage: too many pixels is TooLarge", "[planimage]") {
    const auto r = probePlanImageBytes(makePng(10000, 10000));
    CHECK(r.error == PlanImageError::TooLarge);
    CHECK(r.message.find("10000 x 10000") != std::string::npos);
    CHECK(r.message.find("64000000") != std::string::npos);
}

TEST_CASE("PlanImage: a huge PNG size does not overflow", "[planimage]") {
    CHECK(probePlanImageBytes(makePng(0xFFFFFFFFu, 0xFFFFFFFFu)).error == PlanImageError::TooLarge);
}

TEST_CASE("PlanImage: the format comes from the bytes, not the name", "[planimage]") {
    TempFile f("wrongname.bmp", makePng(40, 30));
    const auto r = probePlanImage(f.path);
    REQUIRE(r.ok());
    CHECK(r.info.format == PlanImageFormat::Png);
}

TEST_CASE("PlanImage: file probe reports a missing file", "[planimage]") {
    const auto r = probePlanImage(std::filesystem::temp_directory_path() / "plynth_test_does_not_exist.png");
    CHECK(r.error == PlanImageError::NotFound);
    CHECK_FALSE(r.message.empty());
}

TEST_CASE("PlanImage: file probe reports a folder as unreadable", "[planimage]") {
    const auto r = probePlanImage(std::filesystem::temp_directory_path());
    CHECK(r.error == PlanImageError::CannotRead);
}

TEST_CASE("PlanImage: file probe reports a zero byte file as Empty", "[planimage]") {
    TempFile f("zero.png", {});
    CHECK(probePlanImage(f.path).error == PlanImageError::Empty);
}

TEST_CASE("PlanImage: file probe reads a JPEG with a metadata block larger than one chunk", "[planimage]") {
    TempFile f("bigexif.jpg", makeJpeg(1234, 567, 300000));
    const auto r = probePlanImage(f.path);
    REQUIRE(r.ok());
    CHECK(r.info.format == PlanImageFormat::Jpeg);
    CHECK(r.info.width == 1234);
    CHECK(r.info.height == 567);
}

TEST_CASE("PlanImage: file probe of a large file works from the header", "[planimage]") {
    TempFile f("large.png", makePng(5000, 4000), 20 * 1024 * 1024);
    const auto r = probePlanImage(f.path);
    REQUIRE(r.ok());
    CHECK(r.info.width == 5000);
    CHECK(r.info.height == 4000);
}

TEST_CASE("PlanImage: file probe of a truncated JPEG is Corrupt", "[planimage]") {
    auto b = makeJpeg(100, 100, 100000);
    b.resize(70000);
    TempFile f("cut.jpg", b);
    CHECK(probePlanImage(f.path).error == PlanImageError::Corrupt);
}

TEST_CASE("PlanImage: extensions list and matching", "[planimage]") {
    const auto list = planImageExtensions();
    REQUIRE(list.size() == 4);
    CHECK(list[0] == "png");
    CHECK(list[1] == "jpg");
    CHECK(list[2] == "jpeg");
    CHECK(list[3] == "bmp");

    CHECK(hasPlanImageExtension("plan.png"));
    CHECK(hasPlanImageExtension("PLAN.PNG"));
    CHECK(hasPlanImageExtension("a/b/Plan.JpEg"));
    CHECK(hasPlanImageExtension("x.jpg"));
    CHECK(hasPlanImageExtension("x.BMP"));
    CHECK_FALSE(hasPlanImageExtension("notes.txt"));
    CHECK_FALSE(hasPlanImageExtension("png"));
    CHECK_FALSE(hasPlanImageExtension("plan.png.txt"));
    CHECK_FALSE(hasPlanImageExtension("plan."));
    CHECK_FALSE(hasPlanImageExtension(""));
}

TEST_CASE("PlanImage: format names", "[planimage]") {
    CHECK(planImageFormatName(PlanImageFormat::Png) == "PNG");
    CHECK(planImageFormatName(PlanImageFormat::Jpeg) == "JPEG");
    CHECK(planImageFormatName(PlanImageFormat::Bmp) == "BMP");
}
