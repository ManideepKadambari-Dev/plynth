#include "PackSupportTests.h"

#include "../../core/src/PackSupport.h"

#include <plynth/core/EditSession.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <stop_token>
#include <string>
#include <vector>

using namespace plynth;
using namespace plynth::test;
namespace fs = std::filesystem;

namespace {

// Says whether a list holds a text.
bool has(const std::vector<std::string>& list, const std::string& text)
{
    return std::find(list.begin(), list.end(), text) != list.end();
}

// Says whether any file below the pack folder starts with this prefix.
bool anyWithPrefix(const std::vector<std::string>& files, const std::string& prefix)
{
    return std::any_of(files.begin(), files.end(), [&](const std::string& f) { return f.rfind(prefix, 0) == 0; });
}

}

TEST_CASE("the copy rule follows the table", "[pack]") {
    const AssetSource own{"local_file", "sofa.obj", {}};
    CHECK(packCopyRule(PackKind::Asset, own, "cc0") == PackReason::None);
    CHECK(packCopyRule(PackKind::Asset, own, "unknown") == PackReason::None);
    CHECK(packCopyRule(PackKind::Asset, own, "own_work") == PackReason::None);
    CHECK(packCopyRule(PackKind::Asset, own, "royalty_free") == PackReason::License);
    CHECK(packCopyRule(PackKind::Material, own, "personal_use") == PackReason::License);
    CHECK(packCopyRule(PackKind::Asset, {"blenderkit", "https://example.org/a", {}}, "cc0") == PackReason::Store);
    CHECK(packCopyRule(PackKind::Material, {"fab_zip", "pack.zip", {}}, "cc0") == PackReason::Store);
    CHECK(packCopyRule(PackKind::Asset, {"made_up_store", "x", {}}, "cc0") == PackReason::UnknownOrigin);
    CHECK(packCopyRule(PackKind::Asset, {"local_file", "asset.blend", {}}, "cc0") == PackReason::UnknownOrigin);
    CHECK(packCopyRule(PackKind::Material, {"blend_material", "Asset.blend", {}}, "cc0") == PackReason::UnknownOrigin);
    CHECK(packCopyRule(PackKind::Material, {"blend_material", "rooms.blend", {}}, "cc0") == PackReason::None);
    CHECK(packCopyRule(PackKind::Material, {"texture_set", "oak", {}}, "cc_by_4") == PackReason::None);
    CHECK(packCopyRule(PackKind::Shape, {}, "") == PackReason::None);
    CHECK(packCopyRule(PackKind::TrimProfile, {}, "") == PackReason::None);
    CHECK(packCopyRule(PackKind::Generator, own, "cc0") == PackReason::Script);
}

TEST_CASE("an own asset is copied with exactly its listed files and their write times", "[pack]") {
    PackFixture f("pack-own");
    f.asset("sofa");
    const fs::path item = f.a.assetItems / "sofa";
    writeBytes(item / "notes.txt", "private notes");
    writeBytes(item / "textures" / "unlisted.png", "NOT LISTED");
    const PackReport report = f.pack({{PackKind::Asset, "sofa"}});
    requireOk(report.status);
    CHECK(report.complete);
    const PackEntry* entry = findEntry(report.manifest, "sofa");
    REQUIRE(entry != nullptr);
    CHECK(entry->state == PackState::Copied);
    const std::vector<std::string> expected = {"assets/sofa/asset.blend", "assets/sofa/asset.fbx", "assets/sofa/info.json",
                                               "assets/sofa/textures/wood.png", "assets/sofa/thumb.png"};
    CHECK(entry->files == expected);
    std::vector<std::string> onDisk = expected;
    onDisk.push_back("pack.json");
    std::sort(onDisk.begin(), onDisk.end());
    CHECK(f.packFiles() == onDisk);
    for (const char* file : {"asset.blend", "textures/wood.png", "info.json"}) {
        INFO(file);
        CHECK(fs::last_write_time(item / file) == fs::last_write_time(f.packFolder() / "assets" / "sofa" / file));
    }
    CHECK(report.bytes > 0);
    CHECK(report.folder == f.packFolder());
}

TEST_CASE("a store item is only named and none of its files reach the pack", "[pack]") {
    PackFixture f("pack-store");
    f.asset("chair", "blenderkit", "royalty_free", "https://www.blenderkit.com/get/abc");
    writeBytes(f.a.assetItems / "chair" / "planted.png", "PLANTED");
    const PackReport report = f.pack({{PackKind::Asset, "chair"}});
    requireOk(report.status);
    const PackEntry* entry = findEntry(report.manifest, "chair");
    REQUIRE(entry != nullptr);
    CHECK(entry->state == PackState::Reference);
    CHECK(entry->reason == PackReason::Store);
    CHECK(entry->files.empty());
    CHECK(entry->source.kind == "blenderkit");
    CHECK(entry->source.reference == "https://www.blenderkit.com/get/abc");
    CHECK(entry->license == "royalty_free");
    CHECK(f.packFiles() == std::vector<std::string>{"pack.json"});
    CHECK_FALSE(anyWithPrefix(f.packFiles(), "assets/"));
}

TEST_CASE("licenses, unknown origins and unknown licenses follow the rule", "[pack]") {
    PackFixture f("pack-rules");
    f.asset("lamp", "local_file", "royalty_free");
    f.asset("bare", "local_file", "cc0", "asset.blend");
    f.asset("odd", "someone_elses_store", "cc0");
    f.asset("rug", "local_file", "unknown");
    f.material("oak", "personal_use");
    f.material("brick");
    const PackReport report = f.pack({{PackKind::Asset, "lamp"}, {PackKind::Asset, "bare"}, {PackKind::Asset, "odd"},
                                      {PackKind::Asset, "rug"}, {PackKind::Material, "oak"}, {PackKind::Material, "brick"}});
    requireOk(report.status);
    CHECK(findEntry(report.manifest, "lamp")->reason == PackReason::License);
    CHECK(findEntry(report.manifest, "bare")->reason == PackReason::UnknownOrigin);
    CHECK(findEntry(report.manifest, "odd")->reason == PackReason::UnknownOrigin);
    CHECK(findEntry(report.manifest, "rug")->state == PackState::Copied);
    CHECK(findEntry(report.manifest, "oak")->state == PackState::Reference);
    const PackEntry* brick = findEntry(report.manifest, "brick");
    CHECK(brick->state == PackState::Copied);
    CHECK(has(brick->files, "materials/brick/info.json"));
    CHECK(has(brick->files, "materials/brick/textures/base_color.png"));
    CHECK(has(brick->files, "materials/brick/thumb.png"));
    REQUIRE(report.problems.size() == 1);
    CHECK(report.problems[0].code == "license_unknown");
    CHECK(report.problems[0].object == "rug");
    CHECK(report.complete);
}

TEST_CASE("missing and broken items are listed and the pack is still written", "[pack]") {
    PackFixture f("pack-missing");
    f.asset("sofa");
    writeBytes(f.a.assetItems / "broken" / "info.json", "{ not json");
    const PackReport report = f.pack({{PackKind::Asset, "sofa"}, {PackKind::Asset, "ghost"}, {PackKind::Asset, "broken"},
                                      {PackKind::Generator, "nothere"}, {PackKind::Shape, "noshape"}});
    requireOk(report.status);
    CHECK_FALSE(report.complete);
    CHECK(findEntry(report.manifest, "ghost")->reason == PackReason::NotInLibrary);
    CHECK(findEntry(report.manifest, "broken")->reason == PackReason::Broken);
    CHECK(findEntry(report.manifest, "nothere")->state == PackState::Missing);
    CHECK(findEntry(report.manifest, "noshape")->state == PackState::Missing);
    CHECK(fs::exists(f.manifestFile()));
    CHECK(schemaProblem("pack.schema.json", f.manifest()).empty());
    const PackRead read = readPack(f.project);
    requireOk(read.status);
    CHECK(findEntry(read.manifest, "ghost")->state == PackState::Missing);
    const UnpackPlan plan = planUnpack(f.project, f.b);
    requireOk(plan.status);
    int missing = 0;
    for (const BuildProblem& p : report.problems) missing += p.code == "item_missing";
    CHECK(missing == 4);
}

TEST_CASE("generators are named, shapes are copied and room types are carried", "[pack]") {
    PackFixture f("pack-kinds");
    writeBytes(f.a.generators / "spiral.py", "print('never copied')");
    f.shape("arch");
    const PackReport report = f.pack({{PackKind::Generator, "spiral"}, {PackKind::Shape, "arch"}}, false,
                                     {{"gaming", "Gaming room"}, {"gaming", "Gaming room"}});
    requireOk(report.status);
    const PackEntry* generator = findEntry(report.manifest, "spiral");
    CHECK(generator->state == PackState::Reference);
    CHECK(generator->reason == PackReason::Script);
    CHECK(generator->files.empty());
    const PackEntry* shape = findEntry(report.manifest, "arch");
    CHECK(shape->state == PackState::Copied);
    CHECK(shape->name == "Shape arch");
    CHECK(shape->files == std::vector<std::string>{"shapes/arch.shape.json"});
    CHECK(report.manifest.roomTypes.size() == 1);
    CHECK_FALSE(anyWithPrefix(f.packFiles(), "generators"));
}

TEST_CASE("the manifest matches its schema and holds no local path or user name", "[pack]") {
    PackFixture f("pack-schema");
    f.asset("sofa");
    f.asset("chair", "fab_zip", "cc0", "Chair_pack.zip");
    f.material("brick");
    f.shape("arch");
    requireOk(f.pack({{PackKind::Asset, "sofa"}, {PackKind::Asset, "chair"}, {PackKind::Material, "brick"},
                      {PackKind::Shape, "arch"}}, false, {{"gaming", "Gaming room"}}).status);
    const nlohmann::json json = f.manifest();
    CHECK(schemaProblem("pack.schema.json", json).empty());
    const std::string text = readTextFile(f.manifestFile());
    CHECK(text.find(pathToUtf8(f.tmp.path())) == std::string::npos);
    CHECK(text.find(":/") == std::string::npos);
    CHECK(text.find(":\\") == std::string::npos);
    char* user = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&user, &size, "USERNAME") == 0 && user != nullptr) {
        const std::string name(user);
        std::free(user);
        if (name.size() > 2) CHECK(text.find(name) == std::string::npos);
    }
    const PackRead read = readPack(f.project);
    requireOk(read.status);
    CHECK(read.manifest.entries.size() == 4);
    CHECK(read.manifest.project == "Flat");
}

TEST_CASE("a check writes nothing", "[pack]") {
    PackFixture f("pack-check");
    f.asset("sofa");
    const PackReport report = f.pack({{PackKind::Asset, "sofa"}}, true);
    requireOk(report.status);
    CHECK(report.checkedOnly);
    CHECK(report.bytes > 0);
    CHECK(findEntry(report.manifest, "sofa")->state == PackState::Copied);
    CHECK_FALSE(fs::exists(f.packFolder()));
    for (const auto& entry : fs::directory_iterator(f.project)) {
        CHECK(entry.path().filename().string().rfind(".pack-", 0) != 0);
    }
}

TEST_CASE("a new pack replaces the old one, and a stopped one keeps it", "[pack]") {
    PackFixture f("pack-replace");
    f.asset("sofa");
    f.asset("lamp");
    requireOk(f.pack({{PackKind::Asset, "sofa"}, {PackKind::Asset, "lamp"}}).status);
    requireOk(f.pack({{PackKind::Asset, "sofa"}}).status);
    CHECK_FALSE(anyWithPrefix(f.packFiles(), "assets/lamp/"));
    const std::string before = readTextFile(f.manifestFile());

    std::stop_source stop;
    stop.request_stop();
    const PackReport stopped = f.pack({{PackKind::Asset, "lamp"}}, false, {}, stop.get_token());
    CHECK(stopped.status.code == ErrorCode::Cancelled);
    CHECK(readTextFile(f.manifestFile()) == before);
    for (const auto& entry : fs::directory_iterator(f.project)) {
        CHECK(entry.path().filename().string().rfind(".pack-", 0) != 0);
    }
}

TEST_CASE("pack refuses while the app has unsaved changes, without a library or a project", "[pack]") {
    PackFixture f("pack-refuse");
    f.asset("sofa");
    requireOk(writeAppState(f.project, AppState{currentProcess(), true, sessionNow()}));
    CHECK(f.pack({{PackKind::Asset, "sofa"}}).status.code == ErrorCode::UnsavedChanges);
    CHECK_FALSE(fs::exists(f.packFolder()));
    requireOk(writeAppState(f.project, AppState{currentProcess(), false, sessionNow()}));
    requireOk(f.pack({{PackKind::Asset, "sofa"}}).status);

    PackRequest request;
    request.projectFolder = f.project;
    request.contents.used = {{PackKind::Asset, "sofa"}};
    request.library = libraryPaths(f.tmp.path() / "nolibrary");
    CHECK(packProject(request).status.code == ErrorCode::NoLibrary);
    request.projectFolder = f.tmp.path();
    request.library = f.a;
    CHECK(packProject(request).status.code == ErrorCode::NotAProject);
}

TEST_CASE("a linked file in an item makes it broken instead of followed", "[pack]") {
    PackFixture f("pack-link");
    f.asset("sofa");
    const fs::path texture = f.a.assetItems / "sofa" / "textures" / "wood.png";
    fs::remove(texture);
    std::error_code ec;
    fs::create_symlink(f.tmp.path() / "outside.png", texture, ec);
    if (ec) {
        SKIP("This machine does not allow links to be made.");
    }
    writeBytes(f.tmp.path() / "outside.png", "OUTSIDE");
    const PackReport report = f.pack({{PackKind::Asset, "sofa"}});
    requireOk(report.status);
    CHECK(findEntry(report.manifest, "sofa")->reason == PackReason::Broken);
    CHECK_FALSE(anyWithPrefix(f.packFiles(), "assets/"));
}

TEST_CASE("names and ids with other alphabets survive a round trip", "[pack]") {
    PackFixture f("pack-unicode");
    AssetInfo info = f.asset("sofa");
    info.name = "Sofá Ünïcode 沙发";
    requireOk(writeAssetInfo(f.a.assetItems / "sofa" / "info.json", info));
    requireOk(f.pack({{PackKind::Asset, "sofa"}}).status);
    const PackRead read = readPack(f.project);
    requireOk(read.status);
    CHECK(read.manifest.entries.at(0).name == "Sofá Ünïcode 沙发");
}

TEST_CASE("unsafe relative paths are refused", "[pack]") {
    for (const char* bad : {"", "/abs/x.png", "a//b.png", "a/../b.png", "./a.png", "C:/x.png", "a\\b.png", "a/CON.png",
                            "a/con", "a/lpt1.txt", "a/b.", "a/b ", "a/b|c.png", "\\\\server\\x"}) {
        INFO(bad);
        CHECK_FALSE(plynth::pack::isSafeRelative(bad));
    }
    for (const char* good : {"assets/sofa/info.json", "assets/sofa/textures/maps/wood/base_color.png", "shapes/a.shape.json",
                             "assets/sofa/console.png"}) {
        INFO(good);
        CHECK(plynth::pack::isSafeRelative(good));
    }
}
