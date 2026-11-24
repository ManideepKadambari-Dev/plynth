#include "CatalogSupport.h"
#include "FakeBlender.h"
#include "ZipTestWriter.h"

#include <plynth/core/Blender.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/catalog/AssetImport.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <string>

using namespace plynth;
namespace fs = std::filesystem;

TEST_CASE("a refused inbox zip names where it moved, not a zip with a similar name", "[store_import][inbox_path]")
{
    for (const std::string& stem : {std::string("Pack"), std::string("Pack [1]")}) {
        INFO(stem);
        plynth::test::IsolatedUser user("ib");
        plynth::test::TestLibrary lib("ib");
        ImportContext context;
        context.library = lib.paths;
        context.blender = inspectBlender(plynth::test::makeFakeBlender(lib.tmp.path() / "b", "5.2.0"));
        context.run.scriptsFolder = plynth::test::makeFakeScripts(lib.tmp.path() / "s");
        context.removeOriginal = [](const fs::path& path) {
            std::error_code ec;
            fs::remove(path, ec);
            return Status{};
        };

        const fs::path other = lib.paths.assetInboxFailed / pathFromUtf8(stem + ".zip");
        plynth::test::writeBytes(other, "another pack, refused today");
        const fs::path entry = lib.paths.assetInbox / pathFromUtf8(stem + ".zip");
        plynth::test::writeZipFile(entry, plynth::test::zipEntriesNamed({"Chair_LOD0.fbx", "Table_LOD0.fbx"}));
        fs::last_write_time(entry, fs::file_time_type::clock::now() - std::chrono::hours(24 * 365));
        const std::string bytes = plynth::test::readTextFile(entry);

        const ImportOutcome outcome = importInboxEntry(*lib.catalog, context, entry);
        REQUIRE(outcome.status.code == ErrorCode::InvalidValue);
        const fs::path moved = lib.paths.assetInboxFailed / pathFromUtf8(stem + " (2).zip");
        CHECK(plynth::test::readTextFile(moved) == bytes);
        CHECK(plynth::test::readTextFile(other) == "another pack, refused today");
        CHECK(outcome.movedTo == moved);
    }
}

TEST_CASE("an imported inbox file names where it moved", "[store_import][inbox_path]")
{
    plynth::test::IsolatedUser user("id");
    plynth::test::TestLibrary lib("id");
    ImportContext context;
    context.library = lib.paths;
    context.blender = inspectBlender(plynth::test::makeFakeBlender(lib.tmp.path() / "b", "5.2.0"));
    context.run.scriptsFolder = plynth::test::makeFakeScripts(lib.tmp.path() / "s");
    plynth::test::writeBytes(lib.paths.assetInboxDone / "chair.obj", "an older one");
    const fs::path entry = lib.paths.assetInbox / "chair.obj";
    plynth::test::writeBytes(entry, "v 0 0 0\n");
    const ImportOutcome outcome = importInboxEntry(*lib.catalog, context, entry);
    plynth::test::requireOk(outcome.status);
    CHECK(outcome.movedTo == lib.paths.assetInboxDone / "chair (2).obj");
}
