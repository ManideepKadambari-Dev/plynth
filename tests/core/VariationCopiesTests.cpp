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
        options.links[std::string(kUnrealLinkId)].kit = true;
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
        CHECK(engineLinkOptions(readExportOptions(made.folders[i]).options, kUnrealLinkId).kit);
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

TEST_CASE("parseSeedList reads seeds, ranges and commas", "[core][variation]")
{
    const auto seeds = [](std::string_view text) {
        const SeedListRead read = parseSeedList(text);
        requireOk(read.status);
        return read.seeds;
    };
    REQUIRE(seeds("7") == std::vector<std::uint32_t>{7});
    REQUIRE(seeds("1,2,3") == std::vector<std::uint32_t>{1, 2, 3});
    REQUIRE(seeds("1-5") == std::vector<std::uint32_t>{1, 2, 3, 4, 5});
    REQUIRE(seeds("1-5,9") == std::vector<std::uint32_t>{1, 2, 3, 4, 5, 9});
    REQUIRE(seeds(" 1-3 , 9 ,12 - 14 ") == std::vector<std::uint32_t>{1, 2, 3, 9, 12, 13, 14});
    REQUIRE(seeds("\t4\t,5") == std::vector<std::uint32_t>{4, 5});
    REQUIRE(seeds("9,1,5") == std::vector<std::uint32_t>{9, 1, 5});
    REQUIRE(seeds("5-5") == std::vector<std::uint32_t>{5});
    REQUIRE(seeds("0") == std::vector<std::uint32_t>{0});
    REQUIRE(seeds("4294967295") == std::vector<std::uint32_t>{4294967295u});
    REQUIRE(seeds("4294967294-4294967295") == std::vector<std::uint32_t>{4294967294u, 4294967295u});
    REQUIRE(seeds("007") == std::vector<std::uint32_t>{7});
}

TEST_CASE("parseSeedList refuses wrong parts with the part named", "[core][variation]")
{
    const auto refused = [](std::string_view text) {
        const SeedListRead read = parseSeedList(text);
        REQUIRE(read.status.code == ErrorCode::InvalidValue);
        REQUIRE(read.seeds.empty());
        return read.status.message;
    };
    REQUIRE(refused("") == "Give at least one seed.");
    REQUIRE(refused("   ") == "Give at least one seed.");
    REQUIRE(refused("abc") == "The seed 'abc' is not a whole number from 0 to 4294967295.");
    REQUIRE(refused("1,,2") == "The seed '' is not a whole number from 0 to 4294967295.");
    REQUIRE(refused("1,") == "The seed '' is not a whole number from 0 to 4294967295.");
    REQUIRE(refused(",1") == "The seed '' is not a whole number from 0 to 4294967295.");
    REQUIRE(refused("-5") == "The seed '-5' is not a whole number from 0 to 4294967295.");
    REQUIRE(refused("+5") == "The seed '+5' is not a whole number from 0 to 4294967295.");
    REQUIRE(refused("1.5") == "The seed '1.5' is not a whole number from 0 to 4294967295.");
    REQUIRE(refused("1 2") == "The seed '1 2' is not a whole number from 0 to 4294967295.");
    REQUIRE(refused("4294967296") == "The seed '4294967296' is not a whole number from 0 to 4294967295.");
    REQUIRE(refused("99999999999999999999999") ==
            "The seed '99999999999999999999999' is not a whole number from 0 to 4294967295.");
    const std::string range = "' must be two whole numbers from 0 to 4294967295, the first not above the second.";
    REQUIRE(refused("5-3") == "The range '5-3" + range);
    REQUIRE(refused("5-") == "The range '5-" + range);
    REQUIRE(refused("1-2-3") == "The range '1-2-3" + range);
    REQUIRE(refused("a-3") == "The range 'a-3" + range);
    REQUIRE(refused("1-4294967296") == "The range '1-4294967296" + range);
    REQUIRE(refused("2, 5 - x") == "The range '5 - x" + range);
}

TEST_CASE("parseSeedList refuses repeats and more seeds than one run makes", "[core][variation]")
{
    REQUIRE(parseSeedList("1,1").status.message == "The seed 1 is given twice.");
    REQUIRE(parseSeedList("1-3,2").status.message == "The seed 2 is given twice.");
    REQUIRE(parseSeedList("5,3-6").status.message == "The seed 5 is given twice.");

    const std::string tooMany = "At most " + std::to_string(kMaxVariationCopies) + " copies can be made at once.";
    const SeedListRead full = parseSeedList("1-" + std::to_string(kMaxVariationCopies));
    requireOk(full.status);
    REQUIRE(full.seeds.size() == kMaxVariationCopies);
    REQUIRE(parseSeedList("1-" + std::to_string(kMaxVariationCopies + 1)).status.message == tooMany);
    REQUIRE(parseSeedList("1-" + std::to_string(kMaxVariationCopies) + ",500").status.message == tooMany);
    // A huge range is refused at once, without counting it out.
    const SeedListRead huge = parseSeedList("0-4294967295");
    REQUIRE(huge.status.message == tooMany);
    REQUIRE(huge.seeds.empty());

    // What it accepts, makeVariationCopies accepts as well.
    std::string many;
    for (std::size_t i = 0; i < kMaxVariationCopies; ++i) many += (i == 0 ? "" : ",") + std::to_string(i * 3);
    const SeedListRead list = parseSeedList(many);
    requireOk(list.status);
    REQUIRE(list.seeds.size() == kMaxVariationCopies);
    REQUIRE(list.seeds.back() == (kMaxVariationCopies - 1) * 3);
}
