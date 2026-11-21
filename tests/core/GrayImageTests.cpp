#include "TestImages.h"
#include "TestSupport.h"

#include <plynth/core/detection/GrayImage.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <vector>

using namespace plynth;
using namespace plynth::test;

namespace {

// Writes bytes to a file.
void writeFile(const std::filesystem::path& file, const std::vector<std::uint8_t>& bytes)
{
    std::ofstream out(file, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

}

TEST_CASE("loadGrayImage reads the sample plan", "[core][detection][image]") {
    const GrayImageLoad load = loadGrayImage(std::filesystem::path(PLYNTH_SAMPLES_DIR) / "plans" / "apartment-01.png");
    requireOk(load.status);
    REQUIRE(load.image);
    REQUIRE(load.image->width == 2400);
    REQUIRE(load.image->height == 1600);
    REQUIRE(load.image->pixels.size() == 2400u * 1600u);
    // White paper in the corner, black wall ink inside the top left corner of the building.
    REQUIRE(load.image->pixels[0] == 255);
    REQUIRE(load.image->pixels[static_cast<std::size_t>(310) * 2400 + 510] == 0);
}

TEST_CASE("loadGrayImage refuses what it cannot read", "[core][detection][image]") {
    TempDir dir("grayimage");
    SECTION("a missing file is not found") {
        const GrayImageLoad load = loadGrayImage(dir.path() / "nothing.png");
        REQUIRE(load.status.code == ErrorCode::NotFound);
        REQUIRE_FALSE(load.image);
    }
    SECTION("a text file is an image error") {
        const auto file = dir.path() / "notes.png";
        std::ofstream(file) << "this is not an image, only some words in a file";
        const GrayImageLoad load = loadGrayImage(file);
        REQUIRE(load.status.code == ErrorCode::ImageError);
        REQUIRE_FALSE(load.status.message.empty());
        REQUIRE_FALSE(load.image);
    }
    SECTION("a file cut short is an image error") {
        auto bytes = makePngBytes(8, 8, 1, std::vector<std::uint8_t>(64, 100));
        bytes.resize(bytes.size() - 30);
        const auto file = dir.path() / "short.png";
        writeFile(file, bytes);
        const GrayImageLoad load = loadGrayImage(file);
        REQUIRE(load.status.code == ErrorCode::ImageError);
        REQUIRE_FALSE(load.image);
    }
}

TEST_CASE("loadGrayImage turns colour and transparency into grey", "[core][detection][image]") {
    TempDir dir("grayimage-colour");
    SECTION("transparent pixels count as white paper") {
        const std::vector<std::uint8_t> rgba = {
            0, 0, 0, 0,      // fully transparent black
            0, 0, 0, 255,    // opaque black
            255, 0, 0, 255,  // opaque red
            0, 0, 0, 128,    // half transparent black
        };
        const auto file = dir.path() / "alpha.png";
        writeFile(file, makePngBytes(2, 2, 4, rgba));
        const GrayImageLoad load = loadGrayImage(file);
        requireOk(load.status);
        REQUIRE(load.image->width == 2);
        REQUIRE(load.image->height == 2);
        REQUIRE(load.image->pixels[0] == 255);
        REQUIRE(load.image->pixels[1] == 0);
        REQUIRE(load.image->pixels[2] >= 74);
        REQUIRE(load.image->pixels[2] <= 78);
        REQUIRE(load.image->pixels[3] >= 125);
        REQUIRE(load.image->pixels[3] <= 129);
    }
    SECTION("grey images keep their levels") {
        const auto file = dir.path() / "grey.png";
        writeFile(file, makePngBytes(3, 1, 1, {0, 100, 255}));
        const GrayImageLoad load = loadGrayImage(file);
        requireOk(load.status);
        REQUIRE(load.image->pixels == std::vector<std::uint8_t>{0, 100, 255});
    }
    SECTION("colour is mixed down to grey") {
        const auto file = dir.path() / "colour.png";
        writeFile(file, makePngBytes(2, 1, 3, {0, 255, 0, 255, 255, 255}));
        const GrayImageLoad load = loadGrayImage(file);
        requireOk(load.status);
        REQUIRE(load.image->pixels[0] >= 148);
        REQUIRE(load.image->pixels[0] <= 152);
        REQUIRE(load.image->pixels[1] == 255);
    }
}

TEST_CASE("loadGrayImage works with a path of non ASCII letters", "[core][detection][image]") {
    TempDir dir("grayimage-ünï");
    const auto file = dir.path() / std::filesystem::path(u8"plan-ÄÖ.png");
    writeFile(file, makePngBytes(4, 2, 1, {0, 10, 20, 30, 40, 50, 60, 70}));
    const GrayImageLoad load = loadGrayImage(file);
    requireOk(load.status);
    REQUIRE(load.image->width == 4);
    REQUIRE(load.image->height == 2);
    REQUIRE(load.image->pixels[7] == 70);
}
