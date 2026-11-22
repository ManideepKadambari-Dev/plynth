#include "FakeBlender.h"
#include "MaterialCatalogSupport.h"

#include <plynth/core/Blender.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/catalog/MaterialImport.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stop_token>
#include <string>
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

    // A folder with one image and, when given, a material.json.
    fs::path folder(const std::string& name, const std::string& materialJson = "") const
    {
        const fs::path dir = lib.tmp.path() / "in" / pathFromUtf8(name);
        plynth::test::writeBytes(dir / "base_col.png", "image");
        if (!materialJson.empty()) plynth::test::writeBytes(dir / "material.json", materialJson);
        return dir;
    }

    // A file in the input folder.
    fs::path file(const std::string& name, const std::string& content = "x") const
    {
        const fs::path f = lib.tmp.path() / "in" / pathFromUtf8(name);
        plynth::test::writeBytes(f, content);
        return f;
    }

    MaterialImportRequest request(const fs::path& source) const
    {
        MaterialImportRequest r;
        r.source.file = source;
        return r;
    }

    // The jobs the fake has been given, one JSON value per line.
    std::vector<nlohmann::json> jobs() const
    {
        std::vector<nlohmann::json> out;
        std::ifstream in(lib.tmp.path() / "blender" / "fake_jobs.log", std::ios::binary);
        for (std::string line; std::getline(in, line);) {
            if (!line.empty()) out.push_back(nlohmann::json::parse(line));
        }
        return out;
    }

    int stagingEntries() const
    {
        int n = 0;
        for (const auto& e : fs::directory_iterator(lib.paths.materialStaging)) {
            (void)e;
            ++n;
        }
        return n;
    }

    int itemFolders() const
    {
        int n = 0;
        for (const auto& e : fs::directory_iterator(lib.paths.materialItems)) {
            (void)e;
            ++n;
        }
        return n;
    }

    plynth::test::MaterialTestLibrary lib;
    fs::path exe;
    ImportContext context;
};

const char* const kOakJson = R"({"format": "plynth-material-source", "version": 1, "id": "oak_planks", "name": "Oak planks",
 "groups": ["floor"], "tags": ["wood", "warm"], "license": "own_work", "tile_size": {"width": 2.0, "height": 2.0}})";

// Checks an info.json against its schema.
std::string infoProblem(const fs::path& file)
{
    return plynth::test::schemaProblem("material-info.schema.json", nlohmann::json::parse(plynth::test::readTextFile(file)));
}

// Says whether a file starts like a PNG.
bool isPng(const fs::path& file)
{
    return plynth::test::readTextFile(file).rfind("\x89PNG", 0) == 0;
}

}

TEST_CASE("the material importers are registered and sorted", "[core][materialimport]") {
    const auto all = allMaterialImporters();
    REQUIRE(all.size() >= 2);
    std::vector<std::string> ids;
    for (const MaterialImporter* i : all) ids.emplace_back(i->id());
    REQUIRE(std::is_sorted(ids.begin(), ids.end()));
    REQUIRE(findMaterialImporter("texture_set") != nullptr);
    REQUIRE(findMaterialImporter("blend_material") != nullptr);
    REQUIRE(findMaterialImporter("nothing") == nullptr);
    REQUIRE(findMaterialImporter("texture_set")->name() == "Texture set");

    REQUIRE_FALSE(registerMaterialImporter(nullptr));
    class Dup : public MaterialImporter {
    public:
        explicit Dup(std::string id) : id_(std::move(id)) {}
        std::string_view id() const override { return id_; }
        std::string_view name() const override { return "x"; }
        std::string_view description() const override { return "x"; }
        std::vector<std::string> fileSuffixes() const override { return {}; }
        int score(const ImportSource&) const override { return 0; }
        Status plan(const ImportSource&, const fs::path&, MaterialImportPlan&) const override { return {}; }
        std::string id_;
    };
    REQUIRE_FALSE(registerMaterialImporter(std::make_unique<Dup>("texture_set")));
    REQUIRE_FALSE(registerMaterialImporter(std::make_unique<Dup>("Bad Id")));

    const std::vector<std::string> suffixes = importableMaterialSuffixes();
    for (const char* s : {".png", ".jpg", ".zip", ".blend", ".exr"}) {
        REQUIRE(std::find(suffixes.begin(), suffixes.end(), s) != suffixes.end());
    }
    REQUIRE(std::is_sorted(suffixes.begin(), suffixes.end()));
    REQUIRE(textureImageSuffixes().size() == 8);
}

TEST_CASE("importer scores pick folders with images, zips, images and .blend files", "[core][materialimport]") {
    ImportFixture f("mi-score");
    const auto pick = [&](const fs::path& p) {
        ImportSource source;
        source.file = p;
        const MaterialImporter* i = pickMaterialImporter(source);
        return i ? std::string(i->id()) : std::string();
    };
    REQUIRE(pick(f.folder("with_images")) == "texture_set");

    const fs::path nested = f.lib.tmp.path() / "in" / "nested";
    plynth::test::writeBytes(nested / "set" / "a.JPG", "i");
    REQUIRE(pick(nested) == "texture_set");

    fs::create_directories(f.lib.tmp.path() / "in" / "empty");
    REQUIRE(pick(f.lib.tmp.path() / "in" / "empty").empty());

    const fs::path deep = f.lib.tmp.path() / "in" / "deep";
    plynth::test::writeBytes(deep / "a" / "b" / "c.png", "i");
    REQUIRE(pick(deep).empty());

    REQUIRE(pick(f.file("brick.zip")) == "texture_set");
    REQUIRE(pick(f.file("brick.png")) == "texture_set");
    REQUIRE(pick(f.file("Wood.BLEND")) == "blend_material");
    REQUIRE(pick(f.file("chair.obj")).empty());
    REQUIRE(pick(f.file("notes.txt")).empty());

    ImportSource url;
    url.url = "https://example.com/a.zip";
    REQUIRE(pickMaterialImporter(url) == nullptr);

    MaterialImportPlan plan;
    plynth::test::requireOk(findMaterialImporter("texture_set")->plan({f.file("red_brick.zip")}, {}, plan));
    REQUIRE(plan.originalIsZip);
    REQUIRE(plan.workerSource == "texture_set");
    REQUIRE(plan.sourceKind == "texture_set");
    REQUIRE(plan.reference == "red_brick.zip");
    REQUIRE(plan.defaultName == "Red brick");
    plynth::test::requireOk(findMaterialImporter("texture_set")->plan({f.folder("oak-floor")}, {}, plan));
    REQUIRE_FALSE(plan.originalIsZip);
    REQUIRE(plan.defaultName == "Oak floor");
    plynth::test::requireOk(findMaterialImporter("blend_material")->plan({f.file("wood.blend")}, {}, plan));
    REQUIRE(plan.workerSource == "blend");
    REQUIRE(plan.sourceKind == "blend_material");
    REQUIRE(plan.reference == "wood.blend");
    REQUIRE(plan.defaultName.empty());
}

TEST_CASE("importing a folder makes items/<id> with a valid info.json and indexes it", "[core][materialimport]") {
    ImportFixture f("mi-folder");
    const fs::path source = f.folder("oak_set", kOakJson);
    std::vector<ImportStage> stages;
    const MaterialImportOutcome outcome =
        importMaterial(*f.lib.catalog, f.context, f.request(source), [&](const ImportProgress& p) {
            if (stages.empty() || stages.back() != p.stage) stages.push_back(p.stage);
        });
    plynth::test::requireOk(outcome.status);
    REQUIRE(outcome.materialId == "oak_planks");
    REQUIRE(outcome.importer == "texture_set");
    REQUIRE_FALSE(outcome.skipped);
    REQUIRE_FALSE(outcome.thumbnailFailed);
    REQUIRE(stages == std::vector<ImportStage>{ImportStage::Preparing, ImportStage::Blender, ImportStage::Saving, ImportStage::Done});

    const fs::path folder = f.lib.paths.materialItems / "oak_planks";
    REQUIRE(isPng(folder / "textures" / "base_color.png"));
    REQUIRE(isPng(folder / "thumb.png"));
    REQUIRE(infoProblem(folder / "info.json").empty());
    const MaterialInfoRead read = readMaterialInfo(folder / "info.json", "oak_planks");
    plynth::test::requireOk(read.status);
    REQUIRE(read.info.name == "Oak planks");
    REQUIRE(read.info.groups == std::vector<std::string>{"floor"});
    REQUIRE(read.info.tags == std::vector<std::string>{"warm", "wood"});
    REQUIRE(read.info.license == "own_work");
    REQUIRE(read.info.source.kind == "texture_set");
    REQUIRE(read.info.source.reference == "oak_set");
    REQUIRE(read.info.tileSize == TileSize{2.0, 2.0});
    REQUIRE(read.info.textureSize == TextureSize{8, 8});
    REQUIRE(read.info.thumbnail.rendered);
    REQUIRE(read.info.blenderVersion == "5.2.0");
    REQUIRE(read.info.files.bytes > 0);
    REQUIRE(isOpaque(read.info));

    const auto record = f.lib.catalog->material("oak_planks");
    REQUIRE(record);
    REQUIRE(record->state == MaterialState::Ready);
    REQUIRE(record->thumbnail == ThumbnailState::Ok);
    MaterialQuery query;
    query.group = "floor";
    REQUIRE(plynth::test::ids(f.lib.catalog->search(query)) == std::vector<std::string>{"oak_planks"});
    REQUIRE(f.stagingEntries() == 0);
    REQUIRE(fs::exists(source / "base_col.png"));
}

TEST_CASE("the id comes from the request, material.json, then the name", "[core][materialimport]") {
    ImportFixture f("mi-ids");
    MaterialImportRequest byRequest = f.request(f.folder("a_set", kOakJson));
    byRequest.id = "mine";
    plynth::test::requireOk(importMaterial(*f.lib.catalog, f.context, byRequest).status);
    REQUIRE(f.lib.catalog->material("mine"));
    REQUIRE(f.lib.catalog->material("mine")->name == "Oak planks");

    const MaterialImportOutcome byName = importMaterial(*f.lib.catalog, f.context, f.request(f.folder("Red Brick (2)")));
    plynth::test::requireOk(byName.status);
    REQUIRE(byName.materialId == "red_brick_2");
    REQUIRE(f.lib.catalog->material("red_brick_2")->name == "Red Brick (2)");
}

TEST_CASE("the request's name, tile size, groups, tags and license win over material.json", "[core][materialimport]") {
    ImportFixture f("mi-request-wins");
    MaterialImportRequest r = f.request(f.folder("oak_set", kOakJson));
    r.name = "My oak";
    r.tileSize = TileSize{1.5, 0.5};
    r.groups = {"wall", "floor", "wall"};
    r.tags = {"b", "a"};
    r.license = "cc0";
    const MaterialImportOutcome outcome = importMaterial(*f.lib.catalog, f.context, r);
    plynth::test::requireOk(outcome.status);
    REQUIRE(outcome.materialId == "oak_planks");
    const MaterialInfoRead read = readMaterialInfo(f.lib.folder("oak_planks") / "info.json", "oak_planks");
    plynth::test::requireOk(read.status);
    REQUIRE(read.info.name == "My oak");
    REQUIRE(read.info.tileSize == TileSize{1.5, 0.5});
    REQUIRE(read.info.groups == std::vector<std::string>{"wall", "floor"});
    REQUIRE(read.info.tags == std::vector<std::string>{"a", "b"});
    REQUIRE(read.info.license == "cc0");
    const auto jobs = f.jobs();
    REQUIRE(jobs.size() == 1);
    REQUIRE(jobs[0]["params"]["tile_size"]["width"] == 1.5);
}

TEST_CASE("maps, values, normal and bake size reach the job", "[core][materialimport]") {
    ImportFixture f("mi-job-fields");
    MaterialImportRequest r = f.request(f.folder("set"));
    r.maps = {{MapChannel::Roughness, f.file("rough.png")}};
    r.values = {{MapChannel::Metallic, 1.0}, {MapChannel::Opacity, 0.25}};
    r.normal = NormalConvention::DirectX;
    r.bakeSize = 512;
    const MaterialImportOutcome outcome = importMaterial(*f.lib.catalog, f.context, r);
    plynth::test::requireOk(outcome.status);
    const auto jobs = f.jobs();
    REQUIRE(jobs.size() == 1);
    const auto& params = jobs[0]["params"];
    REQUIRE(params["source"]["maps"]["roughness"].get<std::string>().find("rough.png") != std::string::npos);
    REQUIRE(params["source"]["values"]["metallic"] == 1.0);
    REQUIRE(params["source"]["normal"] == "directx");
    REQUIRE(params["bake_size"] == 512);
    const MaterialInfoRead read = readMaterialInfo(f.lib.folder(outcome.materialId) / "info.json", outcome.materialId);
    REQUIRE_FALSE(isOpaque(read.info));
    REQUIRE(read.info.maps.opacity.origin == MapOrigin::Value);
    REQUIRE(isPng(f.lib.folder(outcome.materialId) / "textures" / "opacity.png"));
}

TEST_CASE("a taken id from material.json gets a number, or is skipped", "[core][materialimport]") {
    ImportFixture f("mi-taken");
    const fs::path source = f.folder("oak_set", kOakJson);
    plynth::test::requireOk(importMaterial(*f.lib.catalog, f.context, f.request(source)).status);

    const MaterialImportOutcome second = importMaterial(*f.lib.catalog, f.context, f.request(source));
    plynth::test::requireOk(second.status);
    REQUIRE(second.materialId == "oak_planks_2");
    REQUIRE(f.lib.catalog->material("oak_planks_2"));
    REQUIRE(f.itemFolders() == 2);
    const std::size_t jobsBefore = f.jobs().size();

    MaterialImportRequest skip = f.request(source);
    skip.skipExisting = true;
    const MaterialImportOutcome skipped = importMaterial(*f.lib.catalog, f.context, skip);
    plynth::test::requireOk(skipped.status);
    REQUIRE(skipped.skipped);
    REQUIRE(skipped.materialId == "oak_planks");
    REQUIRE(f.itemFolders() == 2);
    REQUIRE(f.jobs().size() == jobsBefore);
    REQUIRE(f.stagingEntries() == 0);

    MaterialImportRequest byName = f.request(f.folder("red_brick"));
    byName.skipExisting = true;
    plynth::test::requireOk(importMaterial(*f.lib.catalog, f.context, byName).status);
    const MaterialImportOutcome again = importMaterial(*f.lib.catalog, f.context, byName);
    plynth::test::requireOk(again.status);
    REQUIRE(again.skipped);
    REQUIRE(again.materialId == "red_brick");
}

TEST_CASE("a request id that is taken is AlreadyExists, or skipped with skipExisting", "[core][materialimport]") {
    ImportFixture f("mi-request-taken");
    f.lib.add("brick", "Brick");
    MaterialImportRequest r = f.request(f.folder("set"));
    r.id = "brick";
    const MaterialImportOutcome outcome = importMaterial(*f.lib.catalog, f.context, r);
    REQUIRE(outcome.status.code == ErrorCode::AlreadyExists);
    REQUIRE(f.jobs().empty());
    r.skipExisting = true;
    const MaterialImportOutcome skipped = importMaterial(*f.lib.catalog, f.context, r);
    plynth::test::requireOk(skipped.status);
    REQUIRE(skipped.skipped);
    REQUIRE(f.lib.catalog->material("brick")->name == "Brick");
}

TEST_CASE("a Blender failure keeps the work folder and leaves nothing in items", "[core][materialimport]") {
    ImportFixture f("mi-fail", "fail");
    const MaterialImportOutcome outcome = importMaterial(*f.lib.catalog, f.context, f.request(f.folder("set")));
    REQUIRE(outcome.status.code == ErrorCode::BlenderFailed);
    REQUIRE(outcome.status.message.find("Fake import failure.") != std::string::npos);
    REQUIRE_FALSE(outcome.keptWorkFolder.empty());
    REQUIRE(fs::is_directory(outcome.keptWorkFolder));
    REQUIRE(outcome.status.message.find("kept for the log") != std::string::npos);
    REQUIRE(f.itemFolders() == 0);
    REQUIRE(f.lib.catalog->materialCount() == 0);
}

TEST_CASE("a cancelled import leaves nothing", "[core][materialimport]") {
    ImportFixture f("mi-cancel");
    std::stop_source source;
    source.request_stop();
    const MaterialImportOutcome outcome =
        importMaterial(*f.lib.catalog, f.context, f.request(f.folder("set")), {}, source.get_token());
    REQUIRE(outcome.status.code == ErrorCode::Cancelled);
    REQUIRE(f.itemFolders() == 0);
    REQUIRE(f.stagingEntries() == 0);
    REQUIRE(f.lib.catalog->materialCount() == 0);
}

TEST_CASE("a zip is removed after import unless it is kept", "[core][materialimport]") {
    ImportFixture f("mi-zip");
    const fs::path zip = f.file("red_brick.zip");
    const MaterialImportOutcome outcome = importMaterial(*f.lib.catalog, f.context, f.request(zip));
    plynth::test::requireOk(outcome.status);
    REQUIRE(outcome.materialId == "red_brick");
    REQUIRE(outcome.originalRemoved);
    REQUIRE_FALSE(fs::exists(zip));
    const MaterialInfoRead read = readMaterialInfo(f.lib.folder("red_brick") / "info.json", "red_brick");
    REQUIRE(read.info.source.reference == "red_brick.zip");

    const fs::path again = f.file("grey_tile.zip");
    MaterialImportRequest keep = f.request(again);
    keep.keepOriginal = true;
    const MaterialImportOutcome kept = importMaterial(*f.lib.catalog, f.context, keep);
    plynth::test::requireOk(kept.status);
    REQUIRE_FALSE(kept.originalRemoved);
    REQUIRE(fs::exists(again));

    ImportFixture g("mi-zip-setting");
    g.context.settings.keepImportedZips = true;
    const fs::path third = g.file("a.zip");
    plynth::test::requireOk(importMaterial(*g.lib.catalog, g.context, g.request(third)).status);
    REQUIRE(fs::exists(third));
}

TEST_CASE("a failed thumbnail still imports and is marked", "[core][materialimport]") {
    ImportFixture f("mi-nothumb", "no_thumbnail");
    const MaterialImportOutcome outcome = importMaterial(*f.lib.catalog, f.context, f.request(f.folder("set")));
    plynth::test::requireOk(outcome.status);
    REQUIRE(outcome.thumbnailFailed);
    const auto record = f.lib.catalog->material("set");
    REQUIRE(record);
    REQUIRE(record->thumbnail == ThumbnailState::Failed);
    REQUIRE(record->thumbnailError == "Fake render failure");
    REQUIRE_FALSE(fs::exists(f.lib.folder("set") / "thumb.png"));
    REQUIRE(infoProblem(f.lib.folder("set") / "info.json").empty());
}

TEST_CASE("a .blend with several materials needs one named", "[core][materialimport]") {
    ImportFixture f("mi-blend", "several_materials");
    const fs::path blend = f.file("pack.blend");
    const MaterialImportOutcome several = importMaterial(*f.lib.catalog, f.context, f.request(blend));
    REQUIRE(several.status.code == ErrorCode::BlenderFailed);
    REQUIRE(several.status.message.find("Brick, Wood") != std::string::npos);
    REQUIRE(several.importer == "blend_material");

    MaterialImportRequest named = f.request(blend);
    named.blendMaterial = "Wood";
    const MaterialImportOutcome one = importMaterial(*f.lib.catalog, f.context, named);
    plynth::test::requireOk(one.status);
    REQUIRE(one.materialId == "wood");
    const MaterialInfoRead read = readMaterialInfo(f.lib.folder("wood") / "info.json", "wood");
    plynth::test::requireOk(read.status);
    REQUIRE(read.info.name == "Wood");
    REQUIRE(read.info.source.kind == "blend_material");
    REQUIRE(read.info.source.reference == "pack.blend");
    const auto jobs = f.jobs();
    REQUIRE(jobs.back()["params"]["source"]["kind"] == "blend");
    REQUIRE(jobs.back()["params"]["source"]["material"] == "Wood");

    ImportFixture g("mi-blend-one");
    const MaterialImportOutcome only = importMaterial(*g.lib.catalog, g.context, g.request(g.file("sofa.blend")));
    plynth::test::requireOk(only.status);
    REQUIRE(only.materialId == "fakematerial");
    REQUIRE(g.lib.catalog->material("fakematerial")->name == "FakeMaterial");
}

TEST_CASE("bad requests are refused before Blender runs", "[core][materialimport]") {
    ImportFixture f("mi-refuse");
    const fs::path set = f.folder("set");
    const auto code = [&](const MaterialImportRequest& r) { return importMaterial(*f.lib.catalog, f.context, r).status.code; };

    MaterialImportRequest r = f.request(set);
    r.values = {{MapChannel::BaseColor, 0.5}};
    REQUIRE(code(r) == ErrorCode::InvalidValue);
    r.values = {{MapChannel::Normal, 0.5}};
    REQUIRE(code(r) == ErrorCode::InvalidValue);
    r.values = {{MapChannel::Roughness, 1.5}};
    REQUIRE(code(r) == ErrorCode::InvalidValue);
    r.values = {{MapChannel::Roughness, -0.1}};
    REQUIRE(code(r) == ErrorCode::InvalidValue);

    r = f.request(set);
    r.tileSize = TileSize{0.0, 1.0};
    REQUIRE(code(r) == ErrorCode::InvalidValue);
    r = f.request(set);
    r.bakeSize = 1000;
    REQUIRE(code(r) == ErrorCode::InvalidValue);
    r.bakeSize = 8192;
    REQUIRE(code(r) == ErrorCode::InvalidValue);
    r = f.request(set);
    r.maps = {{MapChannel::Normal, f.lib.tmp.path() / "in" / "missing.png"}};
    REQUIRE(code(r) == ErrorCode::NotFound);
    r = f.request(set);
    r.groups = {"nowhere"};
    REQUIRE(code(r) == ErrorCode::NotFound);
    r = f.request(set);
    r.license = "unlicensed_thing";
    REQUIRE(code(r) == ErrorCode::NotFound);
    r = f.request(set);
    r.id = "Bad Id";
    REQUIRE(code(r) == ErrorCode::InvalidValue);
    r = f.request(set);
    r.tags = {"Bad Tag"};
    REQUIRE(code(r) == ErrorCode::InvalidValue);

    REQUIRE(code(f.request(f.file("chair.obj"))) == ErrorCode::InvalidValue);
    REQUIRE(code(f.request(f.lib.tmp.path() / "in" / "absent.zip")) == ErrorCode::NotFound);
    REQUIRE(code(MaterialImportRequest{}) == ErrorCode::InvalidValue);
    REQUIRE(f.jobs().empty());
    REQUIRE(f.itemFolders() == 0);
}

TEST_CASE("inbox entries list folders, zips, images and .blend files", "[core][materialimport]") {
    ImportFixture f("mi-inbox-list");
    const LibraryPaths& p = f.lib.paths;
    fs::create_directories(p.materialInbox / "b_folder");
    plynth::test::writeBytes(p.materialInbox / "a.zip", "z");
    plynth::test::writeBytes(p.materialInbox / "c.PNG", "i");
    plynth::test::writeBytes(p.materialInbox / "d.blend", "b");
    plynth::test::writeBytes(p.materialInbox / "e.obj", "o");
    plynth::test::writeBytes(p.materialInbox / "notes.txt", "t");
    plynth::test::writeBytes(p.materialInbox / ".hidden.png", "i");
    plynth::test::writeBytes(p.materialInbox / "~tmp.zip", "z");
    fs::create_directories(p.materialInboxDone / "old");
    fs::create_directories(p.materialInboxFailed / "bad");
    std::vector<std::string> names;
    for (const fs::path& e : materialInboxEntries(p)) names.push_back(pathToUtf8(e.filename()));
    REQUIRE(names == std::vector<std::string>{"a.zip", "b_folder", "c.PNG", "d.blend"});
    REQUIRE(f.lib.catalog->rescan().inboxWaiting == 4);
}

TEST_CASE("an inbox entry is imported and moved to done, or to failed with the error", "[core][materialimport]") {
    ImportFixture f("mi-inbox-run");
    const LibraryPaths& p = f.lib.paths;
    plynth::test::writeBytes(p.materialInbox / "grey_tile" / "col.png", "i");
    const MaterialImportOutcome ok = importMaterialInboxEntry(*f.lib.catalog, f.context, p.materialInbox / "grey_tile");
    plynth::test::requireOk(ok.status);
    REQUIRE(ok.materialId == "grey_tile");
    REQUIRE_FALSE(fs::exists(p.materialInbox / "grey_tile"));
    REQUIRE(fs::exists(p.materialInboxDone / "grey_tile" / "col.png"));

    plynth::test::writeBytes(p.materialInbox / "red_brick.zip", "z");
    const MaterialImportOutcome zip = importMaterialInboxEntry(*f.lib.catalog, f.context, p.materialInbox / "red_brick.zip");
    plynth::test::requireOk(zip.status);
    REQUIRE(zip.originalRemoved);
    REQUIRE_FALSE(fs::exists(p.materialInbox / "red_brick.zip"));
    REQUIRE_FALSE(fs::exists(p.materialInboxDone / "red_brick.zip"));
    REQUIRE(f.lib.catalog->material("red_brick"));

    plynth::test::writeBytes(p.materialInbox / "grey_tile" / "col.png", "i");
    const MaterialImportOutcome second = importMaterialInboxEntry(*f.lib.catalog, f.context, p.materialInbox / "grey_tile");
    plynth::test::requireOk(second.status);
    REQUIRE(second.materialId == "grey_tile_2");
    REQUIRE(fs::exists(p.materialInboxDone / "grey_tile (2)" / "col.png"));

    plynth::test::writeBytes(p.materialInbox / "wall" / "readme.txt", "t");
    const MaterialImportOutcome bad = importMaterialInboxEntry(*f.lib.catalog, f.context, p.materialInbox / "wall");
    REQUIRE_FALSE(bad.status.ok());
    REQUIRE_FALSE(fs::exists(p.materialInbox / "wall"));
    REQUIRE(fs::exists(p.materialInboxFailed / "wall"));
    const std::string note = plynth::test::readTextFile(p.materialInboxFailed / "wall.error.txt");
    REQUIRE(note.find("cannot import") != std::string::npos);
    REQUIRE(note.find("wall") != std::string::npos);
}

TEST_CASE("an inbox entry that fails in Blender goes to failed with the message", "[core][materialimport]") {
    ImportFixture f("mi-inbox-fail", "fail");
    const LibraryPaths& p = f.lib.paths;
    plynth::test::writeBytes(p.materialInbox / "set" / "col.png", "i");
    const MaterialImportOutcome out = importMaterialInboxEntry(*f.lib.catalog, f.context, p.materialInbox / "set");
    REQUIRE(out.status.code == ErrorCode::BlenderFailed);
    REQUIRE(fs::exists(p.materialInboxFailed / "set"));
    REQUIRE(plynth::test::readTextFile(p.materialInboxFailed / "set.error.txt").find("Fake import failure.") != std::string::npos);
}

TEST_CASE("refresh rewrites the maps of a changed material and keeps the rest", "[core][materialimport]") {
    ImportFixture f("mi-refresh");
    f.lib.add("brick", "Brick wall", {"wall"}, {"red"});
    const fs::path folder = f.lib.folder("brick");
    plynth::test::writeBytes(folder / "notes.txt", "keep me");
    plynth::test::writeBytes(folder / "textures" / "base_color.png", "changed on disk");
    f.lib.catalog->rescan();
    REQUIRE(f.lib.catalog->material("brick")->state == MaterialState::NeedsRefresh);

    std::vector<ImportStage> stages;
    const MaterialImportOutcome outcome = refreshMaterial(*f.lib.catalog, f.context, "brick", [&](const ImportProgress& p) {
        if (stages.empty() || stages.back() != p.stage) stages.push_back(p.stage);
    });
    plynth::test::requireOk(outcome.status);
    REQUIRE(outcome.materialId == "brick");
    REQUIRE(stages == std::vector<ImportStage>{ImportStage::Preparing, ImportStage::Blender, ImportStage::Saving, ImportStage::Done});

    REQUIRE(isPng(folder / "textures" / "base_color.png"));
    REQUIRE(isPng(folder / "thumb.png"));
    REQUIRE(plynth::test::readTextFile(folder / "notes.txt") == "keep me");
    REQUIRE_FALSE(fs::exists(folder / "textures" / "roughness.jpg"));
    REQUIRE(isPng(folder / "textures" / "roughness.png"));

    REQUIRE(infoProblem(folder / "info.json").empty());
    const MaterialInfoRead read = readMaterialInfo(folder / "info.json", "brick");
    plynth::test::requireOk(read.status);
    REQUIRE(read.info.name == "Brick wall");
    REQUIRE(read.info.groups == std::vector<std::string>{"wall"});
    REQUIRE(read.info.tags == std::vector<std::string>{"red"});
    REQUIRE(read.info.source.kind == "texture_set");
    REQUIRE(read.info.maps.roughness.file == "textures/roughness.png");
    REQUIRE(read.info.maps.roughness.origin == MapOrigin::File);
    REQUIRE(read.info.maps.roughness.detail == "roughness.jpg");
    REQUIRE(read.info.tileSize == TileSize{1.0, 0.5});
    REQUIRE(f.lib.catalog->material("brick")->state == MaterialState::Ready);
    REQUIRE(f.lib.catalog->rescan().needsRefresh.empty());

    const auto jobs = f.jobs();
    REQUIRE(jobs.size() == 1);
    const auto& source = jobs[0]["params"]["source"];
    REQUIRE(source["kind"] == "texture_set");
    REQUIRE(source["normal"] == "opengl");
    REQUIRE(source["maps"].contains("base_color"));
    REQUIRE(source["maps"].contains("roughness"));
    REQUIRE_FALSE(source["maps"].contains("opacity"));
    REQUIRE(jobs[0]["params"]["tile_size"]["height"] == 0.5);
    REQUIRE(f.stagingEntries() == 0);
}

TEST_CASE("a material whose tile size was edited is Ready again after a refresh", "[core][materialimport]") {
    ImportFixture f("mi-refresh-tile");
    f.lib.add("brick", "Brick");
    MaterialEdit edit;
    edit.tileSize = TileSize{2.0, 1.0};
    plynth::test::requireOk(f.lib.catalog->editMaterial("brick", edit));
    REQUIRE(f.lib.catalog->material("brick")->state == MaterialState::NeedsRefresh);
    f.lib.catalog->rescan();
    REQUIRE(f.lib.catalog->material("brick")->state == MaterialState::NeedsRefresh);

    plynth::test::requireOk(refreshMaterial(*f.lib.catalog, f.context, "brick").status);
    REQUIRE(f.lib.catalog->material("brick")->state == MaterialState::Ready);
    REQUIRE(f.lib.catalog->material("brick")->tileSize == TileSize{2.0, 1.0});
    REQUIRE(f.lib.catalog->rescan().needsRefresh.empty());
    REQUIRE(f.jobs()[0]["params"]["tile_size"]["width"] == 2.0);
}

TEST_CASE("refresh of a folder without info.json writes one", "[core][materialimport]") {
    ImportFixture f("mi-refresh-raw");
    plynth::test::writeBytes(f.lib.paths.materialItems / "raw_wood" / "wood_col.png", "image");
    const RescanReport report = f.lib.catalog->rescan();
    REQUIRE(report.needsRefresh == std::vector<std::string>{"raw_wood"});

    plynth::test::requireOk(refreshMaterial(*f.lib.catalog, f.context, "raw_wood").status);
    const fs::path folder = f.lib.folder("raw_wood");
    REQUIRE(infoProblem(folder / "info.json").empty());
    REQUIRE(isPng(folder / "textures" / "base_color.png"));
    REQUIRE(plynth::test::readTextFile(folder / "wood_col.png") == "image");
    const auto record = f.lib.catalog->material("raw_wood");
    REQUIRE(record->state == MaterialState::Ready);
    REQUIRE(record->name == "Raw wood");
    REQUIRE(f.jobs()[0]["params"]["source"]["maps"].empty());
    REQUIRE(f.jobs()[0]["params"]["source"]["normal"] == "auto");
    REQUIRE(f.jobs()[0]["params"]["tile_size"].is_null());
}

TEST_CASE("a failed render leaves the material with a failed thumbnail and BlenderFailed", "[core][materialimport]") {
    ImportFixture f("mi-refresh-render", "no_thumbnail");
    f.lib.add("brick", "Brick");
    const MaterialImportOutcome outcome = refreshMaterial(*f.lib.catalog, f.context, "brick");
    REQUIRE(outcome.status.code == ErrorCode::BlenderFailed);
    REQUIRE(outcome.thumbnailFailed);
    const auto record = f.lib.catalog->material("brick");
    REQUIRE(record->thumbnail == ThumbnailState::Failed);
    REQUIRE(record->thumbnailError == "Fake render failure");
    REQUIRE_FALSE(fs::exists(f.lib.folder("brick") / "thumb.png"));
    REQUIRE(isPng(f.lib.folder("brick") / "textures" / "base_color.png"));
}

TEST_CASE("a refresh that fails in Blender changes nothing", "[core][materialimport]") {
    ImportFixture f("mi-refresh-fail", "fail");
    f.lib.add("brick", "Brick");
    const std::string before = plynth::test::readTextFile(f.lib.folder("brick") / "info.json");
    const MaterialImportOutcome outcome = refreshMaterial(*f.lib.catalog, f.context, "brick");
    REQUIRE(outcome.status.code == ErrorCode::BlenderFailed);
    REQUIRE_FALSE(outcome.keptWorkFolder.empty());
    REQUIRE(plynth::test::readTextFile(f.lib.folder("brick") / "info.json") == before);
    REQUIRE(plynth::test::readTextFile(f.lib.folder("brick") / "textures" / "base_color.png") == "IMG-textures/base_color.png");
}

TEST_CASE("refresh of an unknown material is NotFound", "[core][materialimport]") {
    ImportFixture f("mi-refresh-unknown");
    REQUIRE(refreshMaterial(*f.lib.catalog, f.context, "ghost").status.code == ErrorCode::NotFound);
}
