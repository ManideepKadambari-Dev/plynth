#include "SquareFixtures.h"
#include "TestImages.h"
#include "TestSupport.h"

#include <plynth/core/ExportGroups.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/VariationCopies.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace plynth;
using namespace plynth::test;
namespace fs = std::filesystem;

namespace {

// Every file under a folder with its bytes, by path relative to the folder.
std::map<std::string, std::string> filesOf(const fs::path& folder)
{
    std::map<std::string, std::string> out;
    for (const auto& entry : fs::recursive_directory_iterator(folder)) {
        if (!entry.is_regular_file()) continue;
        std::ifstream in(entry.path(), std::ios::binary);
        out[fs::relative(entry.path(), folder).generic_string()] =
            std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
    return out;
}

// A saved project named Flat with a plan image and export options, in folder/Flat.
struct SavedProject {
    explicit SavedProject(const std::string& tag) : tmp(tag)
    {
        folder = tmp.path() / "Flat";
        const fs::path image = tmp.path() / "plan.png";
        const std::vector<std::uint8_t> png = makePngBytes(4, 4, 1, std::vector<std::uint8_t>(16, 255));
        std::ofstream(image, std::ios::binary).write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
        project = squareProjectWithWalls();
        project.settings.name = "Flat";
        project.plan.floors[0].image = pathToUtf8(image);
        requireOk(saveProject(project, folder, {}));
        ExportOptions options;
        options.kit.enabled = true;
        requireOk(writeExportOptions(folder, options));
        out = tmp.path() / "street";
    }

    TempDir tmp;
    fs::path folder;
    fs::path out;
    Project project;
};

}

TEST_CASE("variation copies get their seed, name, images and export options", "[variation][copies]")
{
    SavedProject s("copies-made");
    const auto before = filesOf(s.folder);
    const std::vector<std::uint32_t> seeds = {1, 2, 3};
    const VariationCopies made = makeVariationCopies(s.project, s.folder, seeds, s.out);
    requireOk(made.status);
    REQUIRE(made.folders.size() == 3);
    for (std::size_t i = 0; i < seeds.size(); ++i) {
        INFO(made.folders[i].string());
        CHECK(made.folders[i].filename() == fs::path("Flat_v" + std::to_string(seeds[i])));
        const ProjectLoad load = loadProject(made.folders[i]);
        requireOk(load.status);
        CHECK(load.project.settings.variation.seed == seeds[i]);
        CHECK(load.project.settings.name == "Flat v" + std::to_string(seeds[i]));
        CHECK(load.project.plan == [&] {
            Plan p = s.project.plan;
            p.floors[0].image = load.project.plan.floors[0].image;
            return p;
        }());
        REQUIRE(load.project.plan.floors[0].image.has_value());
        CHECK(fs::is_regular_file(resolveImagePath(made.folders[i], *load.project.plan.floors[0].image)));
        CHECK(readExportOptions(made.folders[i]).options.kit.enabled);
        CHECK_FALSE(fs::exists(made.folders[i] / "assets"));
    }
    CHECK(filesOf(s.folder) == before);
}

TEST_CASE("variation copies refuse before writing anything", "[variation][copies]")
{
    SavedProject s("copies-refused");
    fs::create_directories(s.out / "Flat_v2");
    const std::vector<std::uint32_t> clash = {1, 2};
    CHECK(makeVariationCopies(s.project, s.folder, clash, s.out).status.code == ErrorCode::AlreadyExists);
    CHECK_FALSE(fs::exists(s.out / "Flat_v1"));

    std::vector<std::uint32_t> many;
    for (std::uint32_t i = 1; i <= 101; ++i) many.push_back(i);
    CHECK(makeVariationCopies(s.project, s.folder, many, s.out).status.code == ErrorCode::InvalidValue);
    const std::vector<std::uint32_t> twice = {4, 4};
    CHECK(makeVariationCopies(s.project, s.folder, twice, s.out).status.code == ErrorCode::InvalidValue);
    CHECK(makeVariationCopies(s.project, s.folder, std::vector<std::uint32_t>{}, s.out).status.code == ErrorCode::InvalidValue);
    const std::vector<std::uint32_t> one = {5};
    CHECK(makeVariationCopies(s.project, s.tmp.path() / "unsaved", one, s.out).status.code == ErrorCode::NotAProject);
    CHECK_FALSE(fs::exists(s.out / "Flat_v4"));
    CHECK_FALSE(fs::exists(s.out / "Flat_v5"));
}
