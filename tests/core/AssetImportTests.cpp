#include "CatalogSupport.h"
#include "FakeBlender.h"
#include "SchemaSupport.h"

#include <plynth/core/Blender.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/catalog/AssetImport.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

using namespace plynth;
namespace fs = std::filesystem;

namespace {

// A library, a fake Blender and an import context that deletes zips for good, so tests never fill the Recycle Bin.
struct ImportFixture {
    explicit ImportFixture(const std::string& tag, const std::string& mode = "") : lib(tag)
    {
        exe = plynth::test::makeFakeBlender(lib.tmp.path() / "blender", "5.2.0", mode);
        context.library = lib.paths;
        context.blender = inspectBlender(exe);
        context.run.scriptsFolder = plynth::test::makeFakeScripts(lib.tmp.path() / "scripts");
        context.removeOriginal = [](const fs::path& path) {
            std::error_code ec;
            fs::remove(path, ec);
            return ec ? fail(ErrorCode::CannotWrite, "not removed") : Status{};
        };
    }

    // A request for a file in the temporary folder.
    ImportRequest request(const std::string& name, const std::string& content = "o") const
    {
        const fs::path file = lib.tmp.path() / "in" / pathFromUtf8(name);
        plynth::test::writeBytes(file, content);
        ImportRequest r;
        r.source.file = file;
        return r;
    }

    // The folders and files left in .staging.
    int stagingEntries() const
    {
        int n = 0;
        for (const auto& e : fs::directory_iterator(lib.paths.assetStaging)) {
            (void)e;
            ++n;
        }
        return n;
    }

    plynth::test::TestLibrary lib;
    fs::path exe;
    ImportContext context;
};

// Checks an info.json against its schema.
std::string infoProblem(const fs::path& file)
{
    return plynth::test::schemaProblem("asset-info.schema.json", nlohmann::json::parse(plynth::test::readTextFile(file)));
}


// Gives an asset on disk the one material the fake Blender reports, with or without its maps, the way an
// asset cleaned after milestone 10 or before it looks.
AssetInfo setFakeMaterial(ImportFixture& f, const std::string& id, bool withMaps)
{
    const fs::path file = f.lib.paths.assetItems / pathFromUtf8(id) / "info.json";
    AssetInfoRead read = readAssetInfo(file, id);
    plynth::test::requireOk(read.status);
    AssetInfo info = read.info;
    info.materials = {"Fake"};
    info.materialMaps.clear();
    if (withMaps) {
        AssetMaterial material;
        material.name = "Fake";
        for (const MapChannel channel : {MapChannel::BaseColor, MapChannel::Normal, MapChannel::Roughness, MapChannel::Metallic}) {
            materialMap(material.maps, channel) = {"textures/maps/fake/" + std::string(mapChannelName(channel)) + ".png", MapOrigin::File, "fake.png"};
        }
        material.previewMaps = material.maps;
        info.materialMaps = {material};
    }
    plynth::test::requireOk(writeAssetInfo(file, info));
    plynth::test::requireOk(f.lib.catalog->indexAsset(info));
    return info;
}

}

TEST_CASE("an import lands in items with asset.blend, thumb.png and info.json", "[core][import]") {
    ImportFixture f("import-ok");
    ImportRequest request = f.request("chair.obj");
    request.categories = {"chair", "table"};
    request.tags = {"oak", "wood", "oak"};
    request.license = "cc0";

    std::vector<ImportStage> stages;
    int blenderLines = 0;
    const ImportOutcome outcome = importAsset(*f.lib.catalog, f.context, request, [&](const ImportProgress& p) {
        if (stages.empty() || stages.back() != p.stage) stages.push_back(p.stage);
        if (p.stage == ImportStage::Blender && !p.blender.step.empty()) ++blenderLines;
    });
    plynth::test::requireOk(outcome.status);
    REQUIRE(outcome.assetId == "chair");
    REQUIRE(outcome.importer == "local_file");
    REQUIRE_FALSE(outcome.thumbnailFailed);
    REQUIRE_FALSE(outcome.originalRemoved);
    REQUIRE(outcome.warnings.empty());
    REQUIRE(stages == std::vector<ImportStage>{ImportStage::Preparing, ImportStage::Blender, ImportStage::Saving, ImportStage::Done});
    REQUIRE(blenderLines == 2);

    const fs::path folder = f.lib.paths.assetItems / "chair";
    REQUIRE(plynth::test::readTextFile(folder / "asset.blend") == "FAKEBLEND");
    REQUIRE(fs::file_size(folder / "thumb.png") > 50);
    REQUIRE(infoProblem(folder / "info.json").empty());
    const AssetInfoRead info = readAssetInfo(folder / "info.json", "chair");
    plynth::test::requireOk(info.status);
    REQUIRE(info.info.name == "Chair");
    REQUIRE(info.info.categories == std::vector<std::string>{"chair", "table"});
    REQUIRE(info.info.tags == std::vector<std::string>{"oak", "wood"});
    REQUIRE(info.info.license == "cc0");
    REQUIRE(info.info.source.kind == "local_file");
    REQUIRE(info.info.source.reference == "chair.obj");
    REQUIRE(info.info.size.height == 0.9);
    REQUIRE(info.info.triangles == 12);
    REQUIRE(info.info.thumbnail.rendered);
    REQUIRE(info.info.thumbnail.engine == "eevee");
    REQUIRE(fs::exists(folder / "asset.fbx"));
    REQUIRE(info.info.blendBytes == 9);
    REQUIRE(info.info.blenderVersion == "5.2.0");

    const auto record = f.lib.catalog->asset("chair");
    REQUIRE(record);
    REQUIRE(record->state == AssetState::Ready);
    REQUIRE(record->thumbnail == ThumbnailState::Ok);
    REQUIRE(record->fbxFile == folder / "asset.fbx");
    AssetQuery query;
    query.text = "chair";
    REQUIRE(f.lib.catalog->search(query).size() == 1);
    REQUIRE(f.stagingEntries() == 0);
    REQUIRE(fs::exists(f.request("chair.obj").source.file));
}

TEST_CASE("an import without an fbx still succeeds with the fbx_failed warning", "[core][import]") {
    ImportFixture f("import-nofbx", "no_fbx");
    const ImportOutcome outcome = importAsset(*f.lib.catalog, f.context, f.request("chair.obj"));
    plynth::test::requireOk(outcome.status);
    REQUIRE(outcome.assetId == "chair");
    REQUIRE_FALSE(outcome.thumbnailFailed);
    REQUIRE(outcome.warnings.size() == 1);
    REQUIRE(outcome.warnings[0].code == "fbx_failed");
    const fs::path folder = f.lib.paths.assetItems / "chair";
    REQUIRE(fs::exists(folder / "asset.blend"));
    REQUIRE_FALSE(fs::exists(folder / "asset.fbx"));
    const auto record = f.lib.catalog->asset("chair");
    REQUIRE(record->state == AssetState::Ready);
    REQUIRE(record->fbxFile.empty());
    REQUIRE(infoProblem(folder / "info.json").empty());
}

TEST_CASE("a second import of the same file gets a new id", "[core][import]") {
    ImportFixture f("import-twice");
    ImportRequest request = f.request("chair.obj");
    REQUIRE(importAsset(*f.lib.catalog, f.context, request).assetId == "chair");
    REQUIRE(importAsset(*f.lib.catalog, f.context, request).assetId == "chair_2");
    REQUIRE(importAsset(*f.lib.catalog, f.context, request).assetId == "chair_3");
    REQUIRE(f.lib.catalog->assetCount() == 3);
}

TEST_CASE("the id and name come from the request when given", "[core][import]") {
    ImportFixture f("import-names");
    ImportRequest request = f.request("Red Chair (2).obj");
    REQUIRE(importAsset(*f.lib.catalog, f.context, request).assetId == "red_chair_2");

    request.id = "my_chair";
    request.name = "My favourite chair";
    plynth::test::requireOk(importAsset(*f.lib.catalog, f.context, request).status);
    REQUIRE(f.lib.catalog->asset("my_chair")->name == "My favourite chair");

    const ImportOutcome taken = importAsset(*f.lib.catalog, f.context, request);
    REQUIRE(taken.status.code == ErrorCode::AlreadyExists);
    REQUIRE(f.lib.catalog->assetCount() == 2);
}

TEST_CASE("bad requests are refused before Blender runs", "[core][import]") {
    ImportFixture f("import-refuse", "hang");
    ImportRequest request = f.request("chair.obj");

    SECTION("an unknown category") {
        request.categories = {"spaceship"};
        REQUIRE(importAsset(*f.lib.catalog, f.context, request).status.code == ErrorCode::NotFound);
    }
    SECTION("an unknown license") {
        request.license = "free_beer";
        REQUIRE(importAsset(*f.lib.catalog, f.context, request).status.code == ErrorCode::NotFound);
    }
    SECTION("a bad id, name, tag and category") {
        ImportRequest bad = request;
        bad.id = "Bad Id";
        REQUIRE(importAsset(*f.lib.catalog, f.context, bad).status.code == ErrorCode::InvalidValue);
        bad = request;
        bad.name = "   ";
        REQUIRE(importAsset(*f.lib.catalog, f.context, bad).status.code == ErrorCode::InvalidValue);
        bad = request;
        bad.tags = {"Bad Tag"};
        REQUIRE(importAsset(*f.lib.catalog, f.context, bad).status.code == ErrorCode::InvalidValue);
        bad = request;
        bad.categories = {"Bad Category"};
        REQUIRE(importAsset(*f.lib.catalog, f.context, bad).status.code == ErrorCode::InvalidValue);
    }
    SECTION("scale and rotation") {
        ImportRequest bad = request;
        bad.scale = 0.0;
        REQUIRE(importAsset(*f.lib.catalog, f.context, bad).status.code == ErrorCode::InvalidValue);
        bad.scale = -2.0;
        REQUIRE(importAsset(*f.lib.catalog, f.context, bad).status.code == ErrorCode::InvalidValue);
        bad = request;
        bad.rotateZ = 45;
        REQUIRE(importAsset(*f.lib.catalog, f.context, bad).status.code == ErrorCode::InvalidValue);
    }
    SECTION("a file no importer reads names the endings") {
        const ImportRequest text = f.request("notes.txt");
        const ImportOutcome outcome = importAsset(*f.lib.catalog, f.context, text);
        REQUIRE(outcome.status.code == ErrorCode::InvalidValue);
        REQUIRE(outcome.status.message.find(".obj") != std::string::npos);
        REQUIRE(outcome.status.message.find(".zip") != std::string::npos);
    }
    SECTION("a missing file") {
        ImportRequest missing;
        missing.source.file = f.lib.tmp.path() / "nowhere" / "chair.obj";
        REQUIRE(importAsset(*f.lib.catalog, f.context, missing).status.code == ErrorCode::NotFound);
    }
    SECTION("nothing to import") {
        REQUIRE(importAsset(*f.lib.catalog, f.context, ImportRequest{}).status.code == ErrorCode::InvalidValue);
    }
    REQUIRE(f.stagingEntries() == 0);
    REQUIRE(f.lib.catalog->assetCount() == 0);
}

TEST_CASE("a Blender failure leaves nothing and keeps the work folder", "[core][import]") {
    ImportFixture f("import-fail", "fail");
    const ImportOutcome outcome = importAsset(*f.lib.catalog, f.context, f.request("chair.obj"));
    REQUIRE(outcome.status.code == ErrorCode::BlenderFailed);
    REQUIRE(outcome.status.message.find("Fake import failure.") != std::string::npos);
    REQUIRE_FALSE(outcome.keptWorkFolder.empty());
    REQUIRE(fs::exists(outcome.keptWorkFolder / "blender.log"));
    REQUIRE(outcome.status.message.find(pathToUtf8(outcome.keptWorkFolder)) != std::string::npos);
    REQUIRE(outcome.assetId.empty());
    REQUIRE(f.lib.catalog->assetCount() == 0);
    REQUIRE(fs::is_empty(f.lib.paths.assetItems));

    plynth::test::requireOk(initLibrary(f.lib.tmp.path() / "Library"));
    REQUIRE(f.stagingEntries() == 0);
}

TEST_CASE("a failed import does not hold the id, and the next good import cleans up", "[core][import]") {
    ImportFixture f("import-fail-then-ok", "fail");
    const ImportRequest request = f.request("chair.obj");
    const ImportOutcome failed = importAsset(*f.lib.catalog, f.context, request);
    REQUIRE(failed.status.code == ErrorCode::BlenderFailed);
    REQUIRE(fs::exists(failed.keptWorkFolder));
    REQUIRE_FALSE(fs::exists(failed.keptWorkFolder / ".live"));
    REQUIRE_FALSE(f.lib.catalog->idTaken("chair"));

    plynth::test::makeFakeBlender(f.lib.tmp.path() / "blender", "5.2.0", "ok");
    const ImportOutcome good = importAsset(*f.lib.catalog, f.context, request);
    plynth::test::requireOk(good.status);
    REQUIRE(good.assetId == "chair");
    REQUIRE_FALSE(fs::exists(failed.keptWorkFolder));
    REQUIRE(f.stagingEntries() == 0);
}

TEST_CASE("a kept folder of another source stays after a good import", "[core][import]") {
    ImportFixture f("import-fail-other", "fail");
    const ImportOutcome failed = importAsset(*f.lib.catalog, f.context, f.request("table.obj"));
    REQUIRE(failed.status.code == ErrorCode::BlenderFailed);
    plynth::test::makeFakeBlender(f.lib.tmp.path() / "blender", "5.2.0", "ok");
    plynth::test::requireOk(importAsset(*f.lib.catalog, f.context, f.request("chair.obj")).status);
    REQUIRE(fs::exists(failed.keptWorkFolder));
}

TEST_CASE("a running import marks its work folder as live", "[core][import]") {
    ImportFixture f("import-live", "hang");
    std::stop_source source;
    bool sawLive = false;
    std::thread watcher([&] {
        Status status;
        auto other = AssetCatalog::open(f.lib.paths, status);
        for (int i = 0; i < 100 && !sawLive && other; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            sawLive = other->idTaken("chair");
        }
        source.request_stop();
    });
    const ImportOutcome outcome = importAsset(*f.lib.catalog, f.context, f.request("chair.obj"), {}, source.get_token());
    watcher.join();
    REQUIRE(sawLive);
    REQUIRE(outcome.status.code == ErrorCode::Cancelled);
    REQUIRE(f.stagingEntries() == 0);
}

TEST_CASE("a failed render still imports the asset", "[core][import]") {
    ImportFixture f("import-nothumb", "no_thumbnail");
    const ImportOutcome outcome = importAsset(*f.lib.catalog, f.context, f.request("chair.obj"));
    plynth::test::requireOk(outcome.status);
    REQUIRE(outcome.thumbnailFailed);
    const auto record = f.lib.catalog->asset("chair");
    REQUIRE(record->thumbnail == ThumbnailState::Failed);
    REQUIRE(record->thumbnailError == "Fake render failure");
    REQUIRE_FALSE(fs::exists(f.lib.paths.assetItems / "chair" / "thumb.png"));
    REQUIRE(infoProblem(f.lib.paths.assetItems / "chair" / "info.json").empty());
}

TEST_CASE("an imported zip is removed unless something keeps it", "[core][import]") {
    ImportFixture f("import-zip");
    SECTION("removed by default") {
        const ImportRequest request = f.request("pack.zip");
        const ImportOutcome outcome = importAsset(*f.lib.catalog, f.context, request);
        plynth::test::requireOk(outcome.status);
        REQUIRE(outcome.originalRemoved);
        REQUIRE_FALSE(fs::exists(request.source.file));
        REQUIRE(f.lib.catalog->asset("pack")->sourceReference == "pack.zip");
    }
    SECTION("kept with keepOriginal") {
        ImportRequest request = f.request("pack.zip");
        request.keepOriginal = true;
        const ImportOutcome outcome = importAsset(*f.lib.catalog, f.context, request);
        plynth::test::requireOk(outcome.status);
        REQUIRE_FALSE(outcome.originalRemoved);
        REQUIRE(fs::exists(request.source.file));
    }
    SECTION("kept with keepImportedZips") {
        f.context.settings.keepImportedZips = true;
        const ImportRequest request = f.request("pack.zip");
        REQUIRE_FALSE(importAsset(*f.lib.catalog, f.context, request).originalRemoved);
        REQUIRE(fs::exists(request.source.file));
    }
    SECTION("keepOriginal false overrides the setting") {
        f.context.settings.keepImportedZips = true;
        ImportRequest request = f.request("pack.zip");
        request.keepOriginal = false;
        REQUIRE(importAsset(*f.lib.catalog, f.context, request).originalRemoved);
    }
    SECTION("a failed import keeps the zip") {
        plynth::test::makeFakeBlender(f.lib.tmp.path() / "blender", "5.2.0", "fail");
        const ImportRequest request = f.request("pack.zip");
        REQUIRE_FALSE(importAsset(*f.lib.catalog, f.context, request).status.ok());
        REQUIRE(fs::exists(request.source.file));
    }
}

TEST_CASE("a file that is not a zip is never removed", "[core][import]") {
    ImportFixture f("import-keep-obj");
    const ImportRequest request = f.request("chair.obj");
    const ImportOutcome outcome = importAsset(*f.lib.catalog, f.context, request);
    plynth::test::requireOk(outcome.status);
    REQUIRE_FALSE(outcome.originalRemoved);
    REQUIRE(fs::exists(request.source.file));
}

TEST_CASE("a folder is imported through the model file in it", "[core][import]") {
    ImportFixture f("import-folder");
    plynth::test::writeBytes(f.lib.tmp.path() / "in" / "table_set" / "table.fbx", "x");
    ImportRequest request;
    request.source.file = f.lib.tmp.path() / "in" / "table_set";
    const ImportOutcome outcome = importAsset(*f.lib.catalog, f.context, request);
    plynth::test::requireOk(outcome.status);
    REQUIRE(outcome.assetId == "table_set");
    REQUIRE(f.lib.catalog->asset("table_set")->name == "Table set");
    REQUIRE(fs::exists(request.source.file / "table.fbx"));
}

TEST_CASE("a stop request during Blender cancels and keeps nothing", "[core][import]") {
    ImportFixture f("import-cancel", "hang");
    std::stop_source source;
    std::thread stopper([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(900));
        source.request_stop();
    });
    const ImportOutcome outcome = importAsset(*f.lib.catalog, f.context, f.request("chair.obj"), {}, source.get_token());
    stopper.join();
    REQUIRE(outcome.status.code == ErrorCode::Cancelled);
    REQUIRE(f.stagingEntries() == 0);
    REQUIRE(f.lib.catalog->assetCount() == 0);
    REQUIRE(fs::is_empty(f.lib.paths.assetItems));
}

TEST_CASE("inboxEntries lists what waits, sorted", "[core][import][inbox]") {
    ImportFixture f("inbox-list");
    const LibraryPaths& p = f.lib.paths;
    plynth::test::writeBytes(p.assetInbox / "b.obj", "x");
    plynth::test::writeBytes(p.assetInbox / "a.zip", "x");
    plynth::test::writeBytes(p.assetInbox / "~partial.obj", "x");
    plynth::test::writeBytes(p.assetInbox / ".hidden.obj", "x");
    plynth::test::writeBytes(p.assetInbox / "notes.txt", "x");
    plynth::test::writeBytes(p.assetInbox / "pack" / "m.fbx", "x");
    plynth::test::writeBytes(p.assetInboxDone / "old.obj", "x");
    plynth::test::writeBytes(p.assetInboxFailed / "bad.obj", "x");
    const auto entries = inboxEntries(p);
    REQUIRE(entries.size() == 3);
    REQUIRE(entries[0].filename() == "a.zip");
    REQUIRE(entries[1].filename() == "b.obj");
    REQUIRE(entries[2].filename() == "pack");
}

TEST_CASE("inbox entries leave the inbox after import", "[core][import][inbox]") {
    ImportFixture f("inbox-import");
    const LibraryPaths& p = f.lib.paths;
    plynth::test::writeBytes(p.assetInbox / "pack.zip", "x");
    plynth::test::writeBytes(p.assetInbox / "chair.obj", "x");
    plynth::test::writeBytes(p.assetInbox / "folder" / "lamp.fbx", "x");

    plynth::test::requireOk(importInboxEntry(*f.lib.catalog, f.context, p.assetInbox / "pack.zip").status);
    REQUIRE_FALSE(fs::exists(p.assetInbox / "pack.zip"));
    REQUIRE_FALSE(fs::exists(p.assetInboxDone / "pack.zip"));

    plynth::test::requireOk(importInboxEntry(*f.lib.catalog, f.context, p.assetInbox / "chair.obj").status);
    REQUIRE_FALSE(fs::exists(p.assetInbox / "chair.obj"));
    REQUIRE(fs::exists(p.assetInboxDone / "chair.obj"));

    plynth::test::requireOk(importInboxEntry(*f.lib.catalog, f.context, p.assetInbox / "folder").status);
    REQUIRE(fs::exists(p.assetInboxDone / "folder" / "lamp.fbx"));
    REQUIRE(inboxEntries(p).empty());
    REQUIRE(f.lib.catalog->assetCount() == 3);
}

TEST_CASE("a kept zip from the inbox goes to done", "[core][import][inbox]") {
    ImportFixture f("inbox-keep-zip");
    f.context.settings.keepImportedZips = true;
    plynth::test::writeBytes(f.lib.paths.assetInbox / "pack.zip", "x");
    plynth::test::requireOk(importInboxEntry(*f.lib.catalog, f.context, f.lib.paths.assetInbox / "pack.zip").status);
    REQUIRE(fs::exists(f.lib.paths.assetInboxDone / "pack.zip"));
    REQUIRE_FALSE(fs::exists(f.lib.paths.assetInbox / "pack.zip"));
}

TEST_CASE("a name clash in done gets a number", "[core][import][inbox]") {
    ImportFixture f("inbox-clash");
    const LibraryPaths& p = f.lib.paths;
    plynth::test::writeBytes(p.assetInboxDone / "chair.obj", "old");
    plynth::test::writeBytes(p.assetInbox / "chair.obj", "new");
    plynth::test::requireOk(importInboxEntry(*f.lib.catalog, f.context, p.assetInbox / "chair.obj").status);
    REQUIRE(plynth::test::readTextFile(p.assetInboxDone / "chair.obj") == "old");
    REQUIRE(plynth::test::readTextFile(p.assetInboxDone / "chair (2).obj") == "new");
}

TEST_CASE("a failed inbox entry goes to failed with its error", "[core][import][inbox]") {
    ImportFixture f("inbox-fail", "fail");
    const LibraryPaths& p = f.lib.paths;
    plynth::test::writeBytes(p.assetInbox / "broken.obj", "x");
    const ImportOutcome outcome = importInboxEntry(*f.lib.catalog, f.context, p.assetInbox / "broken.obj");
    REQUIRE(outcome.status.code == ErrorCode::BlenderFailed);
    REQUIRE_FALSE(fs::exists(p.assetInbox / "broken.obj"));
    REQUIRE(fs::exists(p.assetInboxFailed / "broken.obj"));
    const std::string note = plynth::test::readTextFile(p.assetInboxFailed / "broken.obj.error.txt");
    REQUIRE(note.find("Fake import failure.") != std::string::npos);
    REQUIRE(f.lib.catalog->assetCount() == 0);
}

TEST_CASE("a cancelled inbox import leaves the entry in the inbox", "[core][import][inbox]") {
    ImportFixture f("inbox-cancel", "hang");
    plynth::test::writeBytes(f.lib.paths.assetInbox / "chair.obj", "x");
    std::stop_source source;
    std::thread stopper([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(900));
        source.request_stop();
    });
    const ImportOutcome outcome = importInboxEntry(*f.lib.catalog, f.context, f.lib.paths.assetInbox / "chair.obj", {}, source.get_token());
    stopper.join();
    REQUIRE(outcome.status.code == ErrorCode::Cancelled);
    REQUIRE(fs::exists(f.lib.paths.assetInbox / "chair.obj"));
}

TEST_CASE("refreshAsset measures again and renders the thumbnail", "[core][import][refresh]") {
    ImportFixture f("refresh-ok");
    AssetInfo info = plynth::test::makeAssetOnDisk(f.lib.paths, "lamp", "Lamp", {"lamp"});
    info.size = {1.0, 1.0, 1.0};
    info.triangles = 99;
    info.objects = 4;
    plynth::test::requireOk(writeAssetInfo(f.lib.paths.assetItems / "lamp" / "info.json", info));
    plynth::test::requireOk(f.lib.catalog->indexAsset(info));
    REQUIRE(f.lib.catalog->asset("lamp")->thumbnail == ThumbnailState::Missing);

    const ImportOutcome outcome = refreshAsset(*f.lib.catalog, f.context, "lamp");
    plynth::test::requireOk(outcome.status);
    REQUIRE(outcome.assetId == "lamp");
    REQUIRE_FALSE(outcome.thumbnailFailed);
    const auto record = f.lib.catalog->asset("lamp");
    REQUIRE(record->size.width == 0.5);
    REQUIRE(record->size.height == 0.9);
    REQUIRE(record->triangles == 12);
    REQUIRE(record->thumbnail == ThumbnailState::Ok);
    REQUIRE(record->state == AssetState::Ready);
    REQUIRE(record->categories == std::vector<std::string>{"lamp"});
    REQUIRE(fs::file_size(f.lib.paths.assetItems / "lamp" / "thumb.png") > 50);
    REQUIRE(plynth::test::readTextFile(f.lib.paths.assetItems / "lamp" / "asset.blend") == "FAKEBLEND");
    REQUIRE(infoProblem(f.lib.paths.assetItems / "lamp" / "info.json").empty());
    REQUIRE(f.stagingEntries() == 0);
}

TEST_CASE("refreshAsset writes the missing asset.fbx", "[core][import][refresh]") {
    ImportFixture f("refresh-fbx");
    f.lib.add("lamp", "Lamp", {}, {}, true, false);
    setFakeMaterial(f, "lamp", true);
    const fs::path fbx = f.lib.paths.assetItems / "lamp" / "asset.fbx";
    REQUIRE_FALSE(fs::exists(fbx));
    REQUIRE(f.lib.catalog->asset("lamp")->fbxFile.empty());
    REQUIRE(f.lib.catalog->rescan().needsRefresh == std::vector<std::string>{"lamp"});

    plynth::test::requireOk(refreshAsset(*f.lib.catalog, f.context, "lamp").status);
    REQUIRE(fs::exists(fbx));
    REQUIRE(f.lib.catalog->asset("lamp")->fbxFile == fbx);
    REQUIRE(plynth::test::readTextFile(f.lib.paths.assetItems / "lamp" / "asset.blend") == "FAKEBLEND");
    REQUIRE(f.lib.catalog->rescan().needsRefresh.empty());
}

TEST_CASE("refreshAsset with no_fbx keeps the asset and reports the warning", "[core][import][refresh]") {
    ImportFixture f("refresh-nofbx", "no_fbx");
    f.lib.add("lamp", "Lamp", {}, {}, true, false);
    const ImportOutcome outcome = refreshAsset(*f.lib.catalog, f.context, "lamp");
    plynth::test::requireOk(outcome.status);
    REQUIRE(outcome.warnings.size() == 1);
    REQUIRE(outcome.warnings[0].code == "fbx_failed");
    REQUIRE_FALSE(fs::exists(f.lib.paths.assetItems / "lamp" / "asset.fbx"));
    REQUIRE(f.lib.catalog->asset("lamp")->state == AssetState::Ready);
}

TEST_CASE("refreshAsset picks up a changed asset.blend", "[core][import][refresh]") {
    ImportFixture f("refresh-changed");
    f.lib.add("lamp", "Lamp");
    setFakeMaterial(f, "lamp", true);
    plynth::test::writeBytes(f.lib.paths.assetItems / "lamp" / "asset.blend", "FAKEBLEND");
    plynth::test::writeBytes(f.lib.paths.assetItems / "lamp" / "asset.blend", "A LONGER FAKE");
    REQUIRE(f.lib.catalog->rescan().needsRefresh == std::vector<std::string>{"lamp"});
    plynth::test::requireOk(refreshAsset(*f.lib.catalog, f.context, "lamp").status);
    REQUIRE(f.lib.catalog->asset("lamp")->state == AssetState::Ready);
    REQUIRE(f.lib.catalog->rescan().needsRefresh.empty());
}

TEST_CASE("refreshAsset with an unknown id", "[core][import][refresh]") {
    ImportFixture f("refresh-unknown");
    REQUIRE(refreshAsset(*f.lib.catalog, f.context, "nothing").status.code == ErrorCode::NotFound);
}

TEST_CASE("a failed render leaves the asset Failed with the reason", "[core][import][refresh]") {
    ImportFixture f("refresh-nothumb", "no_thumbnail");
    f.lib.add("lamp", "Lamp", {}, {}, true);
    REQUIRE(f.lib.catalog->asset("lamp")->thumbnail == ThumbnailState::Ok);
    const ImportOutcome outcome = refreshAsset(*f.lib.catalog, f.context, "lamp");
    REQUIRE(outcome.status.code == ErrorCode::BlenderFailed);
    REQUIRE(outcome.thumbnailFailed);
    const auto record = f.lib.catalog->asset("lamp");
    REQUIRE(record->thumbnail == ThumbnailState::Failed);
    REQUIRE(record->thumbnailError == "Fake render failure");
    REQUIRE(plynth::test::readTextFile(f.lib.paths.assetItems / "lamp" / "asset.blend") == "FAKEBLEND");
}

TEST_CASE("refreshAsset with a crashing Blender changes nothing", "[core][import][refresh]") {
    ImportFixture f("refresh-crash", "crash");
    f.lib.add("lamp", "Lamp", {}, {}, true);
    const std::string before = plynth::test::readTextFile(f.lib.paths.assetItems / "lamp" / "info.json");
    const ImportOutcome outcome = refreshAsset(*f.lib.catalog, f.context, "lamp");
    REQUIRE(outcome.status.code == ErrorCode::BlenderFailed);
    REQUIRE(plynth::test::readTextFile(f.lib.paths.assetItems / "lamp" / "info.json") == before);
    REQUIRE(f.lib.catalog->asset("lamp")->thumbnail == ThumbnailState::Ok);
}

namespace {

// The width and height in a PNG header. Zero when the file is not a PNG.
std::pair<int, int> pngSize(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    unsigned char header[24] = {};
    in.read(reinterpret_cast<char*>(header), 24);
    if (!in || header[1] != 'P' || header[2] != 'N' || header[3] != 'G') return {0, 0};
    const auto be32 = [&](int at) { return (header[at] << 24) | (header[at + 1] << 16) | (header[at + 2] << 8) | header[at + 3]; };
    return {be32(16), be32(20)};
}

}

TEST_CASE("importing the sample chair with the real Blender", "[.blender]") {
    plynth::test::TempDir work("import-real");
    const fs::path sample = fs::path(PLYNTH_SAMPLES_DIR) / "models" / "chair";
    REQUIRE(fs::exists(sample / "chair.obj"));
    fs::create_directories(work.path() / "in");
    fs::copy(sample, work.path() / "in" / "chair", fs::copy_options::recursive);

    plynth::test::TestLibrary lib("import-real-lib");
    const BlenderChoice choice = chooseBlender(std::nullopt, MachineSettings{});
    plynth::test::requireOk(choice.status);
    ImportContext context;
    context.library = lib.paths;
    context.blender = choice.install;
    context.removeOriginal = [](const fs::path& path) {
        std::error_code ec;
        fs::remove(path, ec);
        return Status{};
    };

    ImportRequest request;
    request.source.file = work.path() / "in" / "chair" / "chair.obj";
    const ImportOutcome outcome = importAsset(*lib.catalog, context, request);
    INFO(outcome.status.message);
    plynth::test::requireOk(outcome.status);
    REQUIRE(outcome.assetId == "chair");
    const auto record = lib.catalog->asset("chair");
    REQUIRE(record);
    REQUIRE(record->size.width == Catch::Approx(0.45).margin(0.001));
    REQUIRE(record->size.depth == Catch::Approx(0.50).margin(0.001));
    REQUIRE(record->size.height == Catch::Approx(0.90).margin(0.001));
    REQUIRE(record->triangles == 72);
    REQUIRE(record->thumbnail == ThumbnailState::Ok);
    const fs::path folder = lib.paths.assetItems / "chair";
    REQUIRE(pngSize(folder / "thumb.png") == std::pair<int, int>{512, 512});
    REQUIRE(infoProblem(folder / "info.json").empty());
    const AssetInfoRead info = readAssetInfo(folder / "info.json", "chair");
    plynth::test::requireOk(info.status);
    REQUIRE(hasMaterialMaps(info.info));
    REQUIRE_FALSE(info.info.materialMaps.empty());
    for (const AssetMaterial& material : info.info.materialMaps) {
        for (const MapChannel channel : {MapChannel::BaseColor, MapChannel::Normal, MapChannel::Roughness, MapChannel::Metallic}) {
            REQUIRE(fs::exists(folder / pathFromUtf8(materialMap(material.maps, channel).file)));
        }
    }
    AssetQuery query;
    query.text = "chair";
    REQUIRE(lib.catalog->search(query).size() == 1);

    const ImportOutcome refreshed = refreshAsset(*lib.catalog, context, "chair");
    INFO(refreshed.status.message);
    plynth::test::requireOk(refreshed.status);
    const auto again = lib.catalog->asset("chair");
    REQUIRE(again->size.width == Catch::Approx(0.45).margin(0.001));
    REQUIRE(again->size.height == Catch::Approx(0.90).margin(0.001));
    REQUIRE(again->triangles == 72);
}

TEST_CASE("an import keeps the maps of its materials in info.json", "[core][import][assetmaps]") {
    ImportFixture f("import-maps");
    const ImportOutcome outcome = importAsset(*f.lib.catalog, f.context, f.request("chair.obj"));
    plynth::test::requireOk(outcome.status);
    const fs::path folder = f.lib.paths.assetItems / "chair";
    const AssetInfoRead read = readAssetInfo(folder / "info.json", "chair");
    plynth::test::requireOk(read.status);
    REQUIRE(read.info.materials == std::vector<std::string>{"Fake"});
    REQUIRE(read.info.materialMaps.size() == 1);
    REQUIRE(hasMaterialMaps(read.info));
    REQUIRE(read.info.textures.size() == 4);
    REQUIRE(read.info.materialMaps[0].maps.baseColor.file == "textures/maps/fake/base_color.png");
    REQUIRE(fs::exists(folder / "textures" / "maps" / "fake" / "base_color.png"));
    REQUIRE(infoProblem(folder / "info.json").empty());
    REQUIRE(f.lib.catalog->rescan().needsRefresh.empty());
}

TEST_CASE("refreshAsset cleans an asset without maps again from its own asset.blend", "[core][import][refresh][assetmaps]") {
    ImportFixture f("refresh-maps");
    f.lib.add("lamp", "Floor lamp", {"lamp"}, {"brass", "tall"}, true);
    const fs::path folder = f.lib.paths.assetItems / "lamp";
    AssetInfo before = setFakeMaterial(f, "lamp", false);
    before.license = "cc0";
    before.source = {"blenderkit", "abc123", *parseSessionTime("2026-11-20T08:00:00Z")};
    plynth::test::requireOk(writeAssetInfo(folder / "info.json", before));
    plynth::test::requireOk(f.lib.catalog->indexAsset(before));
    plynth::test::writeBytes(folder / "textures" / "old_brass.png", "OLD");
    REQUIRE_FALSE(hasMaterialMaps(before));

    const RescanReport report = f.lib.catalog->rescan();
    REQUIRE(report.needsRefresh == std::vector<std::string>{"lamp"});
    REQUIRE(f.lib.catalog->asset("lamp")->state == AssetState::Ready);

    const ImportOutcome outcome = refreshAsset(*f.lib.catalog, f.context, "lamp");
    plynth::test::requireOk(outcome.status);
    REQUIRE_FALSE(outcome.thumbnailFailed);

    const AssetInfoRead read = readAssetInfo(folder / "info.json", "lamp");
    plynth::test::requireOk(read.status);
    REQUIRE(hasMaterialMaps(read.info));
    REQUIRE(read.info.name == "Floor lamp");
    REQUIRE(read.info.categories == std::vector<std::string>{"lamp"});
    REQUIRE(read.info.tags == std::vector<std::string>{"brass", "tall"});
    REQUIRE(read.info.license == "cc0");
    REQUIRE(read.info.source == before.source);
    REQUIRE(read.info.textures.size() == 4);
    REQUIRE(read.info.thumbnail.rendered);
    REQUIRE(infoProblem(folder / "info.json").empty());

    REQUIRE(plynth::test::readTextFile(folder / "asset.blend") == "CLEANED:" + pathToUtf8(folder / "asset.blend"));
    REQUIRE(fs::exists(folder / "asset.fbx"));
    REQUIRE(fs::exists(folder / "thumb.png"));
    REQUIRE(fs::exists(folder / "textures" / "maps" / "fake" / "base_color.png"));
    REQUIRE_FALSE(fs::exists(folder / "textures" / "old_brass.png"));
    const auto record = f.lib.catalog->asset("lamp");
    REQUIRE(record->state == AssetState::Ready);
    REQUIRE(record->thumbnail == ThumbnailState::Ok);
    REQUIRE(f.lib.catalog->rescan().needsRefresh.empty());
    REQUIRE(f.stagingEntries() == 0);
}

TEST_CASE("a crash while cleaning an asset again changes nothing", "[core][import][refresh][assetmaps]") {
    ImportFixture f("refresh-maps-crash", "crash");
    f.lib.add("lamp", "Lamp", {}, {}, true);
    setFakeMaterial(f, "lamp", false);
    const fs::path folder = f.lib.paths.assetItems / "lamp";
    plynth::test::writeBytes(folder / "textures" / "old.png", "OLD");
    const std::string info = plynth::test::readTextFile(folder / "info.json");
    const ImportOutcome outcome = refreshAsset(*f.lib.catalog, f.context, "lamp");
    REQUIRE(outcome.status.code == ErrorCode::BlenderFailed);
    REQUIRE(plynth::test::readTextFile(folder / "info.json") == info);
    REQUIRE(plynth::test::readTextFile(folder / "asset.blend") == "FAKEBLEND");
    REQUIRE(plynth::test::readTextFile(folder / "textures" / "old.png") == "OLD");
    REQUIRE(fs::exists(folder / "asset.fbx"));
    REQUIRE(f.lib.catalog->rescan().needsRefresh == std::vector<std::string>{"lamp"});
}

TEST_CASE("cleaning an asset again with no_fbx keeps it and warns", "[core][import][refresh][assetmaps]") {
    ImportFixture f("refresh-maps-nofbx", "no_fbx");
    f.lib.add("lamp", "Lamp", {}, {}, true);
    setFakeMaterial(f, "lamp", false);
    const ImportOutcome outcome = refreshAsset(*f.lib.catalog, f.context, "lamp");
    plynth::test::requireOk(outcome.status);
    REQUIRE(outcome.warnings.size() == 1);
    REQUIRE(outcome.warnings[0].code == "fbx_failed");
    const fs::path folder = f.lib.paths.assetItems / "lamp";
    REQUIRE_FALSE(fs::exists(folder / "asset.fbx"));
    REQUIRE(f.lib.catalog->asset("lamp")->state == AssetState::Ready);
    const AssetInfoRead read = readAssetInfo(folder / "info.json", "lamp");
    plynth::test::requireOk(read.status);
    REQUIRE(hasMaterialMaps(read.info));
    REQUIRE(f.lib.catalog->rescan().needsRefresh == std::vector<std::string>{"lamp"});
}
