#include "CatalogSupport.h"
#include "FakeBlender.h"
#include "MaterialCatalogSupport.h"
#include "ZipTestWriter.h"

#include <plynth/core/Blender.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/ZipListing.h>
#include <plynth/core/catalog/AssetImport.h>
#include <plynth/core/catalog/FabZip.h>
#include <plynth/core/catalog/ImportPreview.h>
#include <plynth/core/catalog/Importer.h>
#include <plynth/core/catalog/MaterialImport.h>
#include <plynth/core/catalog/MaterialImporter.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <vector>

using namespace plynth;
using plynth::test::ZipTestEntry;
namespace fs = std::filesystem;

namespace {

const std::string kChineseName = "\xE6\xA4\x85\xE5\xAD\x90_caf\xC3\xA9_high.zip";
const std::string kAccentName = "caf\xC3\xA9_high.zip";

// Says whether text can be written as JSON, which takes valid UTF-8 only.
bool validUtf8(const std::string& text)
{
    try {
        (void)nlohmann::json(text).dump();
        return true;
    } catch (const nlohmann::json::exception&) {
        return false;
    }
}

// An import context on the fake Blender that deletes zips for good.
ImportContext contextFor(const LibraryPaths& paths, const fs::path& root)
{
    ImportContext context;
    context.library = paths;
    context.blender = inspectBlender(plynth::test::makeFakeBlender(root / "b", "5.2.0"));
    context.run.scriptsFolder = plynth::test::makeFakeScripts(root / "s");
    context.removeOriginal = [](const fs::path& path) {
        std::error_code ec;
        fs::remove(path, ec);
        return Status{};
    };
    return context;
}

// Writes a zip, making its folder.
fs::path putZip(const fs::path& file, const std::vector<ZipTestEntry>& entries)
{
    fs::create_directories(file.parent_path());
    plynth::test::writeZipFile(file, entries);
    return file;
}

// Model zips with a name that claims UTF-8 and is not.
std::vector<std::vector<ZipTestEntry>> badModelZips()
{
    const std::string bad = "\xFF";
    return {{{"Chair" + bad + "_High.fbx", "x"}},
            {{"Chair_High.fbx", "x"}, {"Chair" + bad + "_BaseColor.png", "x"}},
            {{"Chair_High.fbx", "x"}, {"item" + bad + ".json", "{}"}}};
}

// Material zips with such a name.
std::vector<std::vector<ZipTestEntry>> badMaterialZips()
{
    const std::string bad = "\xFF";
    return {{{"Oak" + bad + "_2K_BaseColor.png", "x"}, {"Oak_4K_BaseColor.png", "x"}},
            {{"Oak_2K_BaseColor.png", "x"}, {"Oak_4K_BaseColor.png", "x"}, {"item" + bad + ".json", "{}"}}};
}

// The number of jobs the fake Blender was given.
int jobCount(const fs::path& root)
{
    std::ifstream in(root / "b" / "fake_jobs.log", std::ios::binary);
    int count = 0;
    for (std::string line; std::getline(in, line);) ++count;
    return count;
}

}

TEST_CASE("a zip name that claims UTF-8 and is not is a bad file", "[zip][store_safety]")
{
    plynth::test::TempDir tmp("zu");
    const std::vector<std::string> bad{"\xFF", "\xC0\xAF", "\xED\xA0\x80", "\xE6\xA4", "\x80", "\xF4\x90\x80\x80"};
    for (std::size_t i = 0; i < bad.size(); ++i) {
        INFO("bad name " << i);
        const fs::path file = putZip(tmp.path() / (std::to_string(i) + ".zip"), {{"Chair" + bad[i] + "_High.fbx", "x"}});
        const ZipListing listing = listZipEntries(file);
        CHECK(listing.status.code == ErrorCode::BadFile);
        CHECK(listing.status.message == "The zip file holds a file name that is not valid UTF-8.");
        CHECK(listing.badName);
        CHECK(listing.entries.empty());
    }
    const ZipListing good = listZipEntries(putZip(tmp.path() / "good.zip", {{"\xE6\xA4\x85_High.fbx", "x"}}));
    plynth::test::requireOk(good.status);
    CHECK(good.entries.at(0).name == "\xE6\xA4\x85_High.fbx");
}

TEST_CASE("the Fab plans refuse a zip with a bad name and do not throw", "[fab_importer][store_safety]")
{
    plynth::test::TempDir tmp("zp");
    int n = 0;
    for (const auto& entries : badModelZips()) {
        INFO("model zip " << ++n);
        ImportSource source;
        source.file = putZip(tmp.path() / (std::to_string(n) + ".zip"), entries);
        const Importer* fab = findImporter(kFabImporterId);
        REQUIRE(fab);
        ImportPlan plan;
        Status status;
        CHECK_NOTHROW(fab->score(source));
        CHECK_NOTHROW(status = fab->plan(source, tmp.path(), plan));
        CHECK(status.code == ErrorCode::BadFile);
    }
    for (const auto& entries : badMaterialZips()) {
        INFO("material zip " << ++n);
        ImportSource source;
        source.file = putZip(tmp.path() / (std::to_string(n) + ".zip"), entries);
        const MaterialImporter* fab = findMaterialImporter(kFabImporterId);
        REQUIRE(fab);
        MaterialImportPlan plan;
        Status status;
        CHECK_NOTHROW(fab->score(source));
        CHECK_NOTHROW(status = fab->plan(source, tmp.path(), plan));
        CHECK(status.code == ErrorCode::BadFile);
    }
}

TEST_CASE("a model zip with a bad name stops before Blender and the next zip still imports", "[store_import][store_safety]")
{
    int n = 0;
    for (const auto& entries : badModelZips()) {
        INFO("model zip " << ++n);
        plynth::test::IsolatedUser user("za");
        plynth::test::TestLibrary lib("za");
        const ImportContext context = contextFor(lib.paths, lib.tmp.path());
        ImportRequest request;
        request.source.file = putZip(lib.tmp.path() / "in" / "bad.zip", entries);
        ImportPreview preview;
        CHECK_NOTHROW(preview = previewAssetImport(lib.paths, request));
        CHECK(preview.status.code == ErrorCode::BadFile);
        ImportOutcome outcome;
        CHECK_NOTHROW(outcome = importAsset(*lib.catalog, context, request));
        CHECK(outcome.status.code == ErrorCode::BadFile);
        CHECK(validUtf8(outcome.status.message));

        const fs::path entry = putZip(lib.paths.assetInbox / "bad.zip", entries);
        ImportOutcome inbox;
        CHECK_NOTHROW(inbox = importInboxEntry(*lib.catalog, context, entry));
        CHECK(inbox.status.code == ErrorCode::BadFile);
        CHECK_FALSE(fs::exists(entry));
        CHECK(fs::exists(lib.paths.assetInboxFailed / "bad.zip"));
        CHECK(inbox.movedTo == lib.paths.assetInboxFailed / "bad.zip");
        CHECK(jobCount(lib.tmp.path()) == 0);

        request.source.file = putZip(lib.tmp.path() / "in" / "good.zip", {{"Chair_High.fbx", "x"}});
        plynth::test::requireOk(importAsset(*lib.catalog, context, request).status);
    }
}

TEST_CASE("a material zip with a bad name stops before Blender", "[store_import][store_safety]")
{
    int n = 0;
    for (const auto& entries : badMaterialZips()) {
        INFO("material zip " << ++n);
        plynth::test::IsolatedUser user("zm");
        plynth::test::MaterialTestLibrary lib("zm");
        const ImportContext context = contextFor(lib.paths, lib.tmp.path());
        MaterialImportRequest request;
        request.source.file = putZip(lib.tmp.path() / "in" / "bad.zip", entries);
        ImportPreview preview;
        CHECK_NOTHROW(preview = previewMaterialImport(lib.paths, request));
        CHECK(preview.status.code == ErrorCode::BadFile);
        MaterialImportOutcome outcome;
        CHECK_NOTHROW(outcome = importMaterial(*lib.catalog, context, request));
        CHECK(outcome.status.code == ErrorCode::BadFile);

        const fs::path entry = putZip(lib.paths.materialInbox / "bad.zip", entries);
        MaterialImportOutcome inbox;
        CHECK_NOTHROW(inbox = importMaterialInboxEntry(*lib.catalog, context, entry));
        CHECK(inbox.status.code == ErrorCode::BadFile);
        CHECK_FALSE(fs::exists(entry));
        CHECK(inbox.movedTo == lib.paths.materialInboxFailed / "bad.zip");
        CHECK(jobCount(lib.tmp.path()) == 0);
    }
}

TEST_CASE("a zip file name outside the code page gives a message and no exception", "[zip][store_safety]")
{
    plynth::test::TempDir tmp("zn");
    for (const std::string& name : {kChineseName, kAccentName}) {
        INFO(name);
        const fs::path file = tmp.path() / pathFromUtf8(name);
        ZipListing absent;
        CHECK_NOTHROW(absent = listZipEntries(file));
        CHECK(absent.status.code == ErrorCode::NotFound);
        CHECK(validUtf8(absent.status.message));
        CHECK(absent.status.message.find(name) != std::string::npos);

        putZip(file, {{"Chair_High.fbx", "x"}});
        const HANDLE held = CreateFileW(file.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        REQUIRE(held != INVALID_HANDLE_VALUE);
        ZipListing locked;
        CHECK_NOTHROW(locked = listZipEntries(file));
        ImportSource source;
        source.file = file;
        CHECK_NOTHROW(pickImporter(source));
        CHECK_NOTHROW(pickMaterialImporter(source));
        CloseHandle(held);
        CHECK(locked.status.code == ErrorCode::CannotRead);
        CHECK(validUtf8(locked.status.message));
        CHECK(locked.status.message.find(name) != std::string::npos);
    }
}

TEST_CASE("names that Windows maps elsewhere are unsafe", "[zip][store_safety]")
{
    plynth::test::TempDir tmp("zw");
    const std::vector<std::string> unsafe{"../a.png", "/a.png", "C:a.png", "Textures/a:b.png", "a.png:stream", "CON.png",
                                          "con.PNG", "Textures/nul", "PRN.txt", "COM1.jpg", "LPT9.png", "AUX", "NUL.tar.png",
                                          "Aux/a.png", "Wood./a.png", "Wood /a.png", ".. /a.png", "a.png.", "a.png ",
                                          "nul .png", "CONIN$", "conout$.png", "COM\xC2\xB9.png", "lpt\xC2\xB3.jpg"};
    for (std::size_t i = 0; i < unsafe.size(); ++i) {
        INFO(unsafe[i]);
        const ZipListing listing = listZipEntries(putZip(tmp.path() / (std::to_string(i) + ".zip"), {{unsafe[i], "x"}}));
        plynth::test::requireOk(listing.status);
        CHECK(listing.entries.at(0).unsafe);
    }
    const std::vector<std::string> safe{"./a.png", "Textures/Console.png", "a..b.png", "COM10.png", "Auxiliary/a.png", "My Textures/a b.png", "Textures/"};
    for (std::size_t i = 0; i < safe.size(); ++i) {
        INFO(safe[i]);
        const ZipListing listing = listZipEntries(putZip(tmp.path() / ("safe" + std::to_string(i) + ".zip"), {{safe[i], "x"}}));
        plynth::test::requireOk(listing.status);
        CHECK_FALSE(listing.entries.at(0).unsafe);
    }
}

TEST_CASE("an import that would unpack such a name is refused before Blender", "[store_import][store_safety]")
{
    for (const char* bad : {"Textures/a:b.png", "CON.png", "COM1.jpg", "LPT9.png", "Wood./a.png", "Wood /a.png", ".. /a.png",
                            "a.png:stream", "a.png.", "a.png "}) {
        INFO(bad);
        plynth::test::IsolatedUser user("zr");
        plynth::test::TestLibrary lib("zr");
        const ImportContext context = contextFor(lib.paths, lib.tmp.path());
        ImportRequest request;
        request.importer = std::string(kFabImporterId);
        request.source.file = putZip(lib.tmp.path() / "in" / "pack.zip", {{"Chair_High.fbx", "x"}, {bad, "x"}});
        CHECK(previewAssetImport(lib.paths, request).status.code == ErrorCode::InvalidValue);
        CHECK(importAsset(*lib.catalog, context, request).status.code == ErrorCode::InvalidValue);
        CHECK(jobCount(lib.tmp.path()) == 0);
        CHECK(fs::exists(request.source.file));
    }
}
