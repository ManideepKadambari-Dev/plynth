#include "OutlineChecks.h"
#include "SchemaSupport.h"
#include "TestSupport.h"

#include <plynth/core/OpeningShapes.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace plynth;
using Catch::Approx;
namespace fs = std::filesystem;

namespace {

// The shape file data folder.
fs::path dataDir()
{
    return fs::path(PLYNTH_TEST_DATA_DIR) / "shapes";
}

// Writes a text file.
void writeText(const fs::path& path, const std::string& text)
{
    std::ofstream out(path, std::ios::binary);
    out << text;
}

// A good shape file text with one part replaced by a test: the outline text and the id.
std::string shapeText(const std::string& outline, const std::string& id = "t_ok", const std::string& extra = "")
{
    return R"({"format":"plynth-shape","version":1,"id":")" + id + R"(","name":"Test",)" + extra + R"("outline":)" + outline + "}";
}

// Reads text written to a temporary file as a shape file.
ShapeFile readText(const std::string& text)
{
    // Tests may run as several programs at once, so the folder name must not repeat between them.
    static int counter = 0;
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    plynth::test::TempDir tmp("shapefile-" + std::to_string(ticks) + "-" + std::to_string(++counter));
    writeText(tmp.path() / "x.shape.json", text);
    return readShapeFile(tmp.path() / "x.shape.json");
}

// Expects a shape file to be refused as BadFile at a JSON pointer.
void expectBad(const std::string& text, const std::string& where)
{
    const ShapeFile r = readText(text);
    INFO(r.status.message << " at " << r.status.where);
    REQUIRE(r.status.code == ErrorCode::BadFile);
    REQUIRE(r.status.where == where);
    REQUIRE_FALSE(r.status.message.empty());
}

const char* const kSquare = "[[0,0],[1,0],[1,1],[0,1]]";

}

TEST_CASE("a good shape file reads", "[core][shapes][shapefile]") {
    const ShapeFile r = readShapeFile(dataDir() / "house.shape.json");
    plynth::test::requireOk(r.status);
    REQUIRE(r.id == "t_house");
    REQUIRE(r.name == "House");
    REQUIRE(r.outline.size() == 5);
    // The file lists the points clockwise; the result is anticlockwise.
    REQUIRE(signedArea(r.outline) > 0.0);
}

TEST_CASE("a read shape registers and stretches to the opening", "[core][shapes][shapefile]") {
    const ShapeFile r = readShapeFile(dataDir() / "house.shape.json");
    plynth::test::requireOk(r.status);
    REQUIRE(registerOpeningShape(makeOutlineShape(r.id, r.name, r.outline)));
    const OpeningShape* shape = findOpeningShape("t_house");
    REQUIRE(shape != nullptr);
    REQUIRE_FALSE(shape->builtIn());
    REQUIRE(shape->sortKey() == kCustomShapeSortKey);
    REQUIRE(shape->sizeRule().empty());
    REQUIRE(shape->fits(0.1, 0.1));
    REQUIRE(checkOpeningShape("t_house", 2.0, 1.0).ok());

    const std::vector<Vec2> o = shape->outline(2.0, 1.0);
    double minU = 9, maxU = -9, minV = 9, maxV = -9;
    for (const Vec2& p : o) {
        minU = std::min(minU, p.x);
        maxU = std::max(maxU, p.x);
        minV = std::min(minV, p.y);
        maxV = std::max(maxV, p.y);
    }
    REQUIRE(minU == Approx(-1.0));
    REQUIRE(maxU == Approx(1.0));
    REQUIRE(minV == Approx(0.0));
    REQUIRE(maxV == Approx(1.0));
    plynth::test::requireCleanOutline(o, 2.0, 1.0);

    // Custom shapes are listed after the built-in ones.
    const auto all = allOpeningShapes();
    std::size_t house = 0;
    std::size_t circle = 0;
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (all[i]->id() == "t_house") house = i;
        if (all[i]->id() == "circle") circle = i;
    }
    REQUIRE(house > circle);

    Opening opening;
    opening.shape = "t_house";
    opening.offset = 5.0;
    opening.width = 2.0;
    opening.height = 1.0;
    opening.sill = 1.0;
    REQUIRE(openingOutline(opening).size() == 5);
}

TEST_CASE("shape files are refused when they break the format", "[core][shapes][shapefile]") {
    SECTION("a file that cannot be opened") {
        const ShapeFile r = readShapeFile(dataDir() / "missing.shape.json");
        REQUIRE(r.status.code == ErrorCode::CannotRead);
    }
    SECTION("not JSON, not an object") {
        expectBad("{ not json", "");
        expectBad("[1]", "");
    }
    SECTION("wrong format or version") {
        expectBad(R"({"format":"plynth-plan","version":1,"id":"a","name":"A","outline":[[0,0],[1,0],[1,1]]})", "/format");
        expectBad(R"({"format":"plynth-shape","version":2,"id":"a","name":"A","outline":[[0,0],[1,0],[1,1]]})", "/version");
        expectBad(R"({"format":"plynth-shape","id":"a","name":"A","outline":[[0,0],[1,0],[1,1]]})", "/version");
    }
    SECTION("missing or bad id and name") {
        expectBad(R"({"format":"plynth-shape","version":1,"name":"A","outline":[[0,0],[1,0],[1,1]]})", "/id");
        expectBad(shapeText(kSquare, "Upper"), "/id");
        expectBad(shapeText(kSquare, "has-dash"), "/id");
        expectBad(shapeText(kSquare, "1abc"), "/id");
        expectBad(R"({"format":"plynth-shape","version":1,"id":"a","name":"","outline":[[0,0],[1,0],[1,1]]})", "/name");
        expectBad(R"({"format":"plynth-shape","version":1,"id":"a","outline":[[0,0],[1,0],[1,1]]})", "/name");
    }
    SECTION("an unknown field") {
        expectBad(shapeText(kSquare, "t_ok", R"("colour":"red",)"), "/colour");
    }
    SECTION("too few points") {
        expectBad(shapeText("[[0,0],[1,1]]"), "/outline");
    }
    SECTION("a number outside the unit box") {
        expectBad(shapeText("[[0,0],[1.5,0],[1,1],[0,1]]"), "/outline/1/0");
        expectBad(shapeText("[[0,0],[1,0],[1,1],[0,-0.1]]"), "/outline/3/1");
    }
    SECTION("a point that is not two numbers") {
        expectBad(shapeText(R"([[0,0],[1,0],[1,"a"],[0,1]])"), "/outline/2/1");
        expectBad(shapeText("[[0,0],[1,0],[1,1,1],[0,1]]"), "/outline/2");
    }
    SECTION("a bounding box that is not the unit box") {
        expectBad(shapeText("[[0,0],[0.9,0],[0.9,1],[0,1]]"), "/outline");
        expectBad(shapeText("[[0.1,0],[1,0],[1,1],[0.1,1]]"), "/outline");
        expectBad(shapeText("[[0,0],[1,0],[1,0.9],[0,0.9]]"), "/outline");
    }
    SECTION("a bow tie") {
        expectBad(shapeText("[[0,0],[1,1],[1,0],[0,1]]"), "/outline");
    }
    SECTION("a repeated point and no area") {
        expectBad(shapeText("[[0,0],[1,0],[1,1],[0,1],[0,0]]"), "/outline");
        expectBad(shapeText("[[0,0],[1,1],[0.5,0.5]]"), "/outline");
    }
}

TEST_CASE("a clockwise outline is turned anticlockwise", "[core][shapes][shapefile]") {
    const ShapeFile r = readText(shapeText("[[0,0],[0,1],[1,1],[1,0]]"));
    plynth::test::requireOk(r.status);
    REQUIRE(signedArea(r.outline) > 0.0);
    REQUIRE(r.outline.size() == 4);
}

TEST_CASE("the shape schema accepts the data files", "[core][shapes][shapefile][schema]") {
    const std::string text = plynth::test::readTextFile(dataDir() / "house.shape.json");
    REQUIRE(plynth::test::schemaProblem("shape.schema.json", nlohmann::json::parse(text)).empty());
    REQUIRE_FALSE(plynth::test::schemaProblemForText("shape.schema.json", shapeText("[[0,0],[1.5,0],[1,1]]")).empty());
}

TEST_CASE("registerShapeFolder", "[core][shapes][shapefile]") {
    SECTION("a missing folder registers nothing") {
        const ShapeFolderLoad load = registerShapeFolder(dataDir() / "nowhere");
        REQUIRE(load.registered.empty());
        REQUIRE(load.refused.empty());
    }
    SECTION("files are read in name order and bad ones are refused") {
        const ShapeFolderLoad load = registerShapeFolder(dataDir() / "folder");
        REQUIRE(load.registered == std::vector<std::string>{"t_alpha", "t_bravo"});
        REQUIRE(load.refused.size() == 2);
        // 03-clash uses a built-in id, 04-broken has two points.
        REQUIRE(load.refused[0].first.filename() == "03-clash.shape.json");
        REQUIRE(load.refused[0].second.code == ErrorCode::AlreadyExists);
        REQUIRE(load.refused[1].first.filename() == "04-broken.shape.json");
        REQUIRE(load.refused[1].second.code == ErrorCode::BadFile);
        REQUIRE(findOpeningShape("t_alpha") != nullptr);
        REQUIRE(findOpeningShape("t_bravo") != nullptr);
        // Subfolders and other files are left alone, and the built-in rectangle is untouched.
        REQUIRE(findOpeningShape("t_hidden") == nullptr);
        REQUIRE(findOpeningShape("rectangle")->builtIn());
        // A second load finds every id taken.
        const ShapeFolderLoad again = registerShapeFolder(dataDir() / "folder");
        REQUIRE(again.registered.empty());
        REQUIRE(again.refused.size() == 4);
    }
}
