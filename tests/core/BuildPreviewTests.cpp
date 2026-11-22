#include "SchemaSupport.h"
#include "TestSupport.h"

#include <plynth/core/BuildPreview.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>

using namespace plynth;
namespace fs = std::filesystem;

namespace {

// Writes a file with any bytes.
void write(const fs::path& file, const std::string& text)
{
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

}

TEST_CASE("readPreviewParts reads a valid parts file and keeps the order", "[core][build][preview]") {
    plynth::test::TempDir tmp("preview-parts");
    const fs::path preview = tmp.path() / "preview";
    write(preview / "parts.json",
          R"({"format":"plynth-build-preview","version":1,"parts":[{"floor":"f1","kind":"storey","file":"f1.fbx"},)"
          R"({"floor":"f1","kind":"roof","file":"f1_roof.fbx"},{"floor":"f2","kind":"storey","file":"f2.fbx"}]})");
    write(preview / "f1.fbx", "x");
    write(preview / "f1_roof.fbx", "x");
    REQUIRE(plynth::test::schemaProblem("build-preview.schema.json", nlohmann::json::parse(plynth::test::readTextFile(preview / "parts.json"))).empty());

    const PreviewPartsRead r = readPreviewParts(tmp.path());
    plynth::test::requireOk(r.status);
    REQUIRE(r.parts.size() == 2);
    REQUIRE(r.parts[0].floor == "f1");
    REQUIRE(r.parts[0].kind == PreviewPartKind::Storey);
    REQUIRE(r.parts[0].file.is_absolute());
    REQUIRE(r.parts[1].kind == PreviewPartKind::Roof);
}

TEST_CASE("readPreviewParts refuses a missing file and bad content", "[core][build][preview]") {
    plynth::test::TempDir tmp("preview-bad");
    REQUIRE(readPreviewParts(tmp.path()).status.code == ErrorCode::NotFound);

    const auto bad = [&](const std::string& text) {
        write(tmp.path() / "preview" / "parts.json", text);
        return readPreviewParts(tmp.path()).status.code;
    };
    REQUIRE(bad(R"({"format":"plynth-build-preview","version":1,"parts":[{"floor":"f1","kind":"storey","file":"../x.fbx"}]})") ==
            ErrorCode::BadFile);
    REQUIRE(bad(R"({"format":"plynth-build-preview","version":1,"parts":[{"floor":"f1","kind":"attic","file":"x.fbx"}]})") ==
            ErrorCode::BadFile);
    REQUIRE(bad(R"({"format":"other","version":1,"parts":[]})") == ErrorCode::BadFile);
    REQUIRE(bad("not json") == ErrorCode::BadFile);
}

TEST_CASE("preview part kinds have names that read back", "[core][build][preview]") {
    REQUIRE(previewPartKindName(PreviewPartKind::Storey) == "storey");
    REQUIRE(previewPartKindFromName("roof") == PreviewPartKind::Roof);
    REQUIRE_FALSE(previewPartKindFromName("x").has_value());
}
