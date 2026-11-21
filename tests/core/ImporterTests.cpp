#include "CatalogSupport.h"

#include <plynth/core/ProjectFiles.h>
#include <plynth/core/catalog/AssetJobs.h>
#include <plynth/core/catalog/Importer.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

using namespace plynth;
namespace fs = std::filesystem;

namespace {

// A source that is a file.
ImportSource fileSource(const fs::path& file)
{
    ImportSource s;
    s.file = file;
    return s;
}

// An importer for the tests that wins for zips named fab_*.zip, like the Fab importer of a later milestone.
class FabLikeImporter : public Importer {
public:
    std::string_view id() const override { return "fab_like_test"; }
    std::string_view name() const override { return "Fab like"; }
    std::string_view description() const override { return "A test importer."; }
    std::vector<std::string> fileSuffixes() const override { return {}; }
    int score(const ImportSource& source) const override
    {
        const std::string name = pathToUtf8(source.file.filename());
        return name.rfind("fab_", 0) == 0 && source.file.extension() == ".zip" ? 20 : 0;
    }
    Status plan(const ImportSource&, const fs::path&, ImportPlan&) const override { return {}; }
};

// A second importer that claims the id local_file.
class DuplicateImporter : public FabLikeImporter {
public:
    std::string_view id() const override { return kLocalFileImporterId; }
};

}

TEST_CASE("pickImporter chooses the local file importer by ending", "[core][importer]") {
    plynth::test::TempDir tmp("imp-pick");
    plynth::test::writeBytes(tmp.path() / "chair.OBJ", "o");
    plynth::test::writeBytes(tmp.path() / "a.glb", "x");
    plynth::test::writeBytes(tmp.path() / "a.zip", "x");
    plynth::test::writeBytes(tmp.path() / "a.txt", "x");
    plynth::test::writeBytes(tmp.path() / "withfbx" / "model.fbx", "x");
    fs::create_directories(tmp.path() / "empty");

    const Importer* obj = pickImporter(fileSource(tmp.path() / "chair.OBJ"));
    REQUIRE(obj);
    REQUIRE(obj->id() == "local_file");
    REQUIRE(pickImporter(fileSource(tmp.path() / "a.glb")) == obj);
    REQUIRE(pickImporter(fileSource(tmp.path() / "a.zip")) == obj);
    REQUIRE(pickImporter(fileSource(tmp.path() / "withfbx")) == obj);
    REQUIRE(pickImporter(fileSource(tmp.path() / "a.txt")) == nullptr);
    REQUIRE(pickImporter(fileSource(tmp.path() / "empty")) == nullptr);
    ImportSource url;
    url.url = "https://example.com/model.glb";
    REQUIRE(pickImporter(url) == nullptr);
}

TEST_CASE("importableSuffixes is sorted", "[core][importer]") {
    const std::vector<std::string> suffixes = importableSuffixes();
    REQUIRE(std::is_sorted(suffixes.begin(), suffixes.end()));
    for (const char* wanted : {".blend", ".fbx", ".glb", ".gltf", ".obj", ".zip"}) {
        REQUIRE(std::find(suffixes.begin(), suffixes.end(), wanted) != suffixes.end());
    }
}

TEST_CASE("the local file importer is in the registry", "[core][importer]") {
    const Importer* local = findImporter("local_file");
    REQUIRE(local);
    REQUIRE(local->name() == "Own files");
    REQUIRE_FALSE(local->description().empty());
    REQUIRE(findImporter("nothing") == nullptr);
    const auto all = allImporters();
    REQUIRE_FALSE(all.empty());
    for (std::size_t i = 1; i < all.size(); ++i) REQUIRE(all[i - 1]->id() < all[i]->id());
}

TEST_CASE("registering refuses a taken or badly formed id", "[core][importer]") {
    REQUIRE_FALSE(registerImporter(std::make_unique<DuplicateImporter>()));
    REQUIRE_FALSE(registerImporter(nullptr));
    REQUIRE(findImporter("local_file")->name() == "Own files");

    class BadId : public FabLikeImporter {
    public:
        std::string_view id() const override { return "Bad Id"; }
    };
    REQUIRE_FALSE(registerImporter(std::make_unique<BadId>()));
}

TEST_CASE("a higher score wins and a new importer needs no other change", "[core][importer]") {
    REQUIRE((findImporter("fab_like_test") != nullptr || registerImporter(std::make_unique<FabLikeImporter>())));
    plynth::test::TempDir tmp("imp-score");
    plynth::test::writeBytes(tmp.path() / "fab_chair.zip", "x");
    plynth::test::writeBytes(tmp.path() / "other.zip", "x");
    const Importer* fab = pickImporter(fileSource(tmp.path() / "fab_chair.zip"));
    REQUIRE(fab);
    REQUIRE(fab->id() == "fab_like_test");
    REQUIRE(pickImporter(fileSource(tmp.path() / "other.zip"))->id() == "local_file");
}

TEST_CASE("the local file importer plans files", "[core][importer]") {
    plynth::test::TempDir tmp("imp-plan");
    plynth::test::writeBytes(tmp.path() / "red_chair-02.OBJ", "x");
    plynth::test::writeBytes(tmp.path() / "pack.zip", "x");
    plynth::test::writeBytes(tmp.path() / "lamp.glb", "x");
    plynth::test::writeBytes(tmp.path() / "lamp.gltf", "x");
    plynth::test::writeBytes(tmp.path() / "scene.blend", "x");
    plynth::test::writeBytes(tmp.path() / "a.txt", "x");
    const Importer* local = findImporter("local_file");
    ImportPlan plan;

    plynth::test::requireOk(local->plan(fileSource(tmp.path() / "red_chair-02.OBJ"), tmp.path(), plan));
    REQUIRE(plan.format == kFormatObj);
    REQUIRE(plan.file == tmp.path() / "red_chair-02.OBJ");
    REQUIRE(plan.sourceKind == kSourceLocalFile);
    REQUIRE(plan.reference == "red_chair-02.OBJ");
    REQUIRE(plan.defaultName == "Red chair 02");
    REQUIRE(plan.license.empty());
    REQUIRE_FALSE(plan.originalIsZip);

    plynth::test::requireOk(local->plan(fileSource(tmp.path() / "pack.zip"), tmp.path(), plan));
    REQUIRE(plan.format == kFormatZip);
    REQUIRE(plan.originalIsZip);

    plynth::test::requireOk(local->plan(fileSource(tmp.path() / "lamp.glb"), tmp.path(), plan));
    REQUIRE(plan.format == kFormatGltf);
    plynth::test::requireOk(local->plan(fileSource(tmp.path() / "lamp.gltf"), tmp.path(), plan));
    REQUIRE(plan.format == kFormatGltf);
    plynth::test::requireOk(local->plan(fileSource(tmp.path() / "scene.blend"), tmp.path(), plan));
    REQUIRE(plan.format == kFormatBlend);

    REQUIRE(local->plan(fileSource(tmp.path() / "a.txt"), tmp.path(), plan).code == ErrorCode::InvalidValue);
    REQUIRE(local->plan(fileSource(tmp.path() / "none.obj"), tmp.path(), plan).code == ErrorCode::NotFound);
    REQUIRE(local->plan(ImportSource{}, tmp.path(), plan).code == ErrorCode::InvalidValue);
}

TEST_CASE("the local file importer plans folders", "[core][importer]") {
    plynth::test::TempDir tmp("imp-folder");
    const Importer* local = findImporter("local_file");
    ImportPlan plan;

    plynth::test::writeBytes(tmp.path() / "one" / "chair.obj", "x");
    plynth::test::requireOk(local->plan(fileSource(tmp.path() / "one"), tmp.path(), plan));
    REQUIRE(plan.file == tmp.path() / "one" / "chair.obj");
    REQUIRE(plan.format == kFormatObj);
    REQUIRE(plan.reference == "one");
    REQUIRE(plan.defaultName == "One");
    REQUIRE_FALSE(plan.originalIsZip);

    plynth::test::writeBytes(tmp.path() / "down" / "models" / "chair.fbx", "x");
    plynth::test::requireOk(local->plan(fileSource(tmp.path() / "down"), tmp.path(), plan));
    REQUIRE(plan.file == tmp.path() / "down" / "models" / "chair.fbx");

    plynth::test::writeBytes(tmp.path() / "both" / "a.obj", "x");
    plynth::test::writeBytes(tmp.path() / "both" / "a.fbx", "x");
    plynth::test::requireOk(local->plan(fileSource(tmp.path() / "both"), tmp.path(), plan));
    REQUIRE(plan.format == kFormatFbx);

    plynth::test::writeBytes(tmp.path() / "two" / "a.obj", "x");
    plynth::test::writeBytes(tmp.path() / "two" / "b.obj", "x");
    const Status s = local->plan(fileSource(tmp.path() / "two"), tmp.path(), plan);
    REQUIRE(s.code == ErrorCode::InvalidValue);
    REQUIRE(s.message.find("a.obj") != std::string::npos);
    REQUIRE(s.message.find("b.obj") != std::string::npos);
}
