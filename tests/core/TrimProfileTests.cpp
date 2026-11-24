#include "SchemaSupport.h"
#include "TestSupport.h"

#include <plynth/core/FinishSettings.h>
#include <plynth/core/OpeningShapes.h>
#include <plynth/core/TrimProfiles.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace plynth;
using Catch::Matchers::WithinAbs;
using plynth::test::TempDir;

namespace {

// A profile that only this test file adds, to show a profile is one file.
class TestStepProfile final : public TrimProfile {
public:
    std::string_view id() const override { return "test_step"; }
    std::string_view name() const override { return "Test step"; }
    int sortKey() const override { return 900; }
    bool builtIn() const override { return true; }
    std::vector<Vec2> outline(double height, double depth) const override
    {
        return {{0, 0}, {depth, 0}, {depth, height / 2}, {depth / 2, height / 2}, {depth / 2, height}, {0, height}};
    }
};

const TrimProfileRegistrar testRegistrar(std::make_unique<TestStepProfile>());

// The cross product of a to b and a to c.
double crossOf(Vec2 a, Vec2 b, Vec2 c)
{
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

// Says whether two segments that share no end cross or touch.
bool segmentsMeet(Vec2 a, Vec2 b, Vec2 c, Vec2 d)
{
    const double d1 = crossOf(a, b, c), d2 = crossOf(a, b, d), d3 = crossOf(c, d, a), d4 = crossOf(c, d, b);
    return ((d1 > 1e-15 && d2 < -1e-15) || (d1 < -1e-15 && d2 > 1e-15))
        && ((d3 > 1e-15 && d4 < -1e-15) || (d3 < -1e-15 && d4 > 1e-15));
}

// Says whether a closed outline has no two edges crossing.
bool isSimple(const std::vector<Vec2>& p)
{
    const std::size_t n = p.size();
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 2; j < n; ++j) {
            if (i == 0 && j == n - 1) continue;
            if (segmentsMeet(p[i], p[(i + 1) % n], p[j], p[(j + 1) % n])) return false;
        }
    }
    return true;
}

// Checks an outline at a size: enough points, simple, anticlockwise with y up, filling its box.
void checkOutline(const TrimProfile& profile, double height, double depth)
{
    INFO(profile.id() << " at " << height << " by " << depth);
    const std::vector<Vec2> out = profile.outline(height, depth);
    REQUIRE(out.size() >= 3);
    CHECK(isSimple(out));
    CHECK(signedArea(out) > 0.0);
    double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
    for (const Vec2& p : out) {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }
    CHECK_THAT(minX, WithinAbs(0.0, 1e-9));
    CHECK_THAT(maxX, WithinAbs(depth, 1e-9));
    CHECK_THAT(minY, WithinAbs(0.0, 1e-9));
    CHECK_THAT(maxY, WithinAbs(height, 1e-9));
    for (std::size_t i = 0; i < out.size(); ++i) {
        CHECK(distance(out[i], out[(i + 1) % out.size()]) > 1e-9);
    }
}

// Writes text to a file.
void writeFile(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream out(path, std::ios::binary);
    out << text;
}

// A profile file's text with an id and an outline.
std::string profileText(const std::string& id, const std::string& outline = "[[0,0],[1,0],[1,0.5],[0.5,1],[0,1]]")
{
    return "{\"format\": \"plynth-trim-profile\", \"version\": 1, \"id\": \"" + id + "\", \"name\": \"Profile " + id
         + "\", \"outline\": " + outline + "}";
}

}

TEST_CASE("The six built-in profiles are registered in order", "[trimprofiles]")
{
    std::vector<std::string> builtIn;
    for (const TrimProfile* p : allTrimProfiles()) {
        if (p->builtIn() && p->sortKey() < 900) builtIn.emplace_back(p->id());
    }
    CHECK(builtIn == std::vector<std::string>{"square", "bevel", "rounded", "classic", "cove", "round"});
    for (const char* id : {"square", "bevel", "rounded", "classic", "cove", "round"}) {
        REQUIRE(findTrimProfile(id) != nullptr);
        CHECK_FALSE(findTrimProfile(id)->name().empty());
    }
    CHECK(findTrimProfile("ogee") == nullptr);
}

TEST_CASE("Every profile outline is simple, anticlockwise and fills its box", "[trimprofiles]")
{
    for (const TrimProfile* p : allTrimProfiles()) {
        checkOutline(*p, 0.10, 0.015);
        checkOutline(*p, 0.08, 0.08);
        checkOutline(*p, 0.005, 0.5);
    }
}

TEST_CASE("A profile in its own file registers without touching other files", "[trimprofiles]")
{
    const TrimProfile* p = findTrimProfile("test_step");
    REQUIRE(p != nullptr);
    CHECK(p->name() == "Test step");
    CHECK_FALSE(registerTrimProfile(std::make_unique<TestStepProfile>()));
    const auto all = allTrimProfiles();
    CHECK(std::find(all.begin(), all.end(), p) != all.end());
}

TEST_CASE("Trim and light kind names go both ways", "[trimprofiles]")
{
    for (TrimKind k : {TrimKind::Skirting, TrimKind::Cornice, TrimKind::Casing, TrimKind::Sill, TrimKind::Railing}) {
        CHECK(trimKindFromName(trimKindName(k)) == k);
    }
    CHECK(trimKindName(TrimKind::Skirting) == "skirting");
    CHECK(trimKindName(TrimKind::Sill) == "sill");
    CHECK_FALSE(trimKindFromName("Skirting").has_value());
    CHECK_FALSE(trimKindFromName("").has_value());
    for (LightKind k : {LightKind::Ceiling, LightKind::Wall, LightKind::Standing}) {
        CHECK(lightKindFromName(lightKindName(k)) == k);
    }
    CHECK(lightKindName(LightKind::Standing) == "standing");
    CHECK_FALSE(lightKindFromName("spot").has_value());
}

TEST_CASE("Trim settings have the agreed defaults and ranges", "[trimprofiles]")
{
    const TrimSettings defaults;
    CHECK(checkTrimSettings(defaults).ok());
    CHECK(defaults.skirting == TrimStyle{true, "classic", 0.10, 0.015});
    CHECK(defaults.cornice == TrimStyle{true, "cove", 0.08, 0.08});
    CHECK(defaults.casing == TrimStyle{true, "bevel", 0.07, 0.018});
    CHECK(defaults.sill == TrimStyle{true, "rounded", 0.03, 0.04});
    CHECK(defaults.railing == TrimStyle{true, "round", 0.05, 0.05});
    CHECK(defaults.railingHeight == 0.9);
    for (const TrimStyle& s : {defaults.skirting, defaults.cornice, defaults.casing, defaults.sill, defaults.railing}) {
        CHECK(findTrimProfile(s.profile) != nullptr);
    }

    TrimSettings s = defaults;
    trimStyle(s, TrimKind::Casing).height = 0.005;
    trimStyle(s, TrimKind::Casing).depth = 0.5;
    s.railingHeight = 0.5;
    CHECK(checkTrimSettings(s).ok());
    s.railingHeight = 1.5;
    CHECK(checkTrimSettings(s).ok());

    auto refused = [&](auto change) {
        TrimSettings t = defaults;
        change(t);
        CHECK(checkTrimSettings(t).code == ErrorCode::InvalidValue);
    };
    refused([](TrimSettings& t) { t.skirting.height = 0.0049; });
    refused([](TrimSettings& t) { t.cornice.depth = 0.5001; });
    refused([](TrimSettings& t) { t.sill.height = std::numeric_limits<double>::infinity(); });
    refused([](TrimSettings& t) { t.railingHeight = 0.49; });
    refused([](TrimSettings& t) { t.railingHeight = 1.51; });
    refused([](TrimSettings& t) { t.casing.profile = "Bevel"; });
    refused([](TrimSettings& t) { t.casing.profile = ""; });
    // A profile this machine does not have is still a well formed setting.
    TrimSettings other = defaults;
    other.casing.profile = "ogee";
    CHECK(checkTrimSettings(other).ok());
    CHECK(&trimStyle(other, TrimKind::Railing) == &other.railing);
}

TEST_CASE("New light markers start at their kind's height", "[trimprofiles]")
{
    CHECK_THAT(defaultLightHeight(LightKind::Ceiling, 3.0), WithinAbs(2.9, 1e-12));
    CHECK_THAT(defaultLightHeight(LightKind::Ceiling, 2.4), WithinAbs(2.3, 1e-12));
    CHECK_THAT(defaultLightHeight(LightKind::Wall, 3.0), WithinAbs(1.8, 1e-12));
    CHECK_THAT(defaultLightHeight(LightKind::Standing, 3.0), WithinAbs(1.4, 1e-12));
    CHECK_THAT(defaultLightHeight(LightKind::Wall, 1.5), WithinAbs(1.4, 1e-12));
}

TEST_CASE("A profile folder registers good files and refuses bad ones", "[trimprofiles]")
{
    TempDir dir("trim-profiles");
    writeFile(dir.path() / "a_good.profile.json", profileText("folder_good"));
    writeFile(dir.path() / "b_taken.profile.json", profileText("classic"));
    writeFile(dir.path() / "c_broken.profile.json", "{not json");
    writeFile(dir.path() / "d_crossing.profile.json", profileText("folder_crossing", "[[0,0],[1,1],[1,0],[0,1]]"));
    writeFile(dir.path() / "e_small.profile.json", profileText("folder_small", "[[0,0],[0.5,0],[0.5,1],[0,1]]"));
    writeFile(dir.path() / "f_shape.shape.json",
              "{\"format\": \"plynth-shape\", \"version\": 1, \"id\": \"folder_shape\", \"name\": \"S\", "
              "\"outline\": [[0,0],[1,0],[1,1],[0,1]]}");
    writeFile(dir.path() / "g_clockwise.profile.json", profileText("folder_clockwise", "[[0,0],[0,1],[1,1],[1,0]]"));

    const TrimProfileFolderLoad load = registerTrimProfileFolder(dir.path());
    CHECK(load.registered == std::vector<std::string>{"folder_good", "folder_clockwise"});
    REQUIRE(load.refused.size() == 4);
    CHECK(load.refused[0].first == "b_taken.profile.json");
    CHECK(load.refused[0].second.code == ErrorCode::AlreadyExists);
    CHECK(load.refused[1].second.code == ErrorCode::BadFile);
    CHECK(load.refused[2].second.code == ErrorCode::BadFile);
    CHECK(load.refused[3].first == "e_small.profile.json");

    const TrimProfile* good = findTrimProfile("folder_good");
    REQUIRE(good != nullptr);
    CHECK_FALSE(good->builtIn());
    CHECK(good->sortKey() == kCustomProfileSortKey);
    checkOutline(*good, 0.1, 0.02);
    const std::vector<Vec2> stretched = good->outline(0.1, 0.02);
    CHECK_THAT(stretched[2].x, WithinAbs(0.02, 1e-12));
    CHECK_THAT(stretched[2].y, WithinAbs(0.05, 1e-12));
    REQUIRE(findTrimProfile("folder_clockwise") != nullptr);
    checkOutline(*findTrimProfile("folder_clockwise"), 0.1, 0.02);

    // The shape loader and the profile loader each read only their own files.
    CHECK(findTrimProfile("folder_shape") == nullptr);
    const ShapeFolderLoad shapes = registerShapeFolder(dir.path());
    CHECK(shapes.registered == std::vector<std::string>{"folder_shape"});
    CHECK(shapes.refused.empty());

    CHECK(registerTrimProfileFolder(dir.path() / "missing").registered.empty());
}

TEST_CASE("Profile files match their schema", "[trimprofiles][schema]")
{
    CHECK(plynth::test::schemaProblemForText("trim-profile.schema.json", profileText("schema_check")) == "");
    CHECK(plynth::test::schemaProblemForText("trim-profile.schema.json", profileText("Bad-Id")) != "");
    TempDir dir("trim-profile-file");
    writeFile(dir.path() / "x.profile.json", profileText("file_check"));
    const TrimProfileFile file = readTrimProfileFile(dir.path() / "x.profile.json");
    CHECK(file.status.ok());
    CHECK(file.id == "file_check");
    CHECK(file.name == "Profile file_check");
    CHECK(file.unitOutline.size() == 5);
    CHECK(readTrimProfileFile(dir.path() / "none.profile.json").status.code == ErrorCode::CannotRead);
    writeFile(dir.path() / "y.profile.json", profileText("Bad-Id"));
    CHECK(readTrimProfileFile(dir.path() / "y.profile.json").status.code == ErrorCode::BadFile);
}
