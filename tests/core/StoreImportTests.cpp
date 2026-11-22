#include "CatalogSupport.h"
#include "FakeBlender.h"
#include "MaterialCatalogSupport.h"
#include "SchemaSupport.h"
#include "ZipTestWriter.h"

#include <plynth/core/Blender.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/catalog/AssetImport.h>
#include <plynth/core/catalog/AssetJobs.h>
#include <plynth/core/catalog/ImportPreview.h>
#include <plynth/core/catalog/Integrations.h>
#include <plynth/core/catalog/MaterialImport.h>
#include <plynth/core/catalog/MaterialJobs.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stop_token>
#include <string>
#include <vector>

using namespace plynth;
namespace fs = std::filesystem;

namespace {

constexpr const char* kId = "0c1d4a2e-1111-2222-3333-444455556666";

// Sets FAKE_BLENDERKIT for the scope of a test.
struct KitMode {
    explicit KitMode(const char* value) { _putenv_s("FAKE_BLENDERKIT", value); }
    ~KitMode() { _putenv_s("FAKE_BLENDERKIT", ""); }
};

// A plain delete in place of the Recycle Bin.
Status deleteFile(const fs::path& path)
{
    std::error_code ec;
    fs::remove(path, ec);
    return ec ? fail(ErrorCode::CannotWrite, "not removed") : Status{};
}

// The jobs the fake has been given, one JSON value per line.
std::vector<nlohmann::json> jobsIn(const fs::path& blenderFolder)
{
    std::vector<nlohmann::json> out;
    std::ifstream in(blenderFolder / "fake_jobs.log", std::ios::binary);
    for (std::string line; std::getline(in, line);) {
        if (!line.empty()) out.push_back(nlohmann::json::parse(line));
    }
    return out;
}

// The tasks of a list of jobs.
std::vector<std::string> tasksOf(const std::vector<nlohmann::json>& jobs)
{
    std::vector<std::string> out;
    for (const auto& job : jobs) out.push_back(job["task"].get<std::string>());
    return out;
}

// True when a path text ends with a part, comparing with forward slashes.
bool endsWith(const std::string& text, const std::string& part)
{
    std::string slashed = text;
    for (char& c : slashed) {
        if (c == '\\') c = '/';
    }
    return slashed.size() >= part.size() && slashed.compare(slashed.size() - part.size(), part.size(), part) == 0;
}

// The number of entries in a folder, zero when it is missing.
int entriesIn(const fs::path& folder)
{
    std::error_code ec;
    int n = 0;
    for (const auto& e : fs::directory_iterator(folder, ec)) {
        (void)e;
        ++n;
    }
    return n;
}

const std::initializer_list<const char*> kVariants = {
    "Chair/FBX/Low/Chair_LOD0.fbx",
    "Chair/FBX/Medium/Chair_LOD0.fbx",
    "Chair/FBX/Medium/Textures/T_Chair_BaseColor_2K.png",
    "Chair/FBX/High/Chair_LOD0.fbx",
    "Chair/Chair.json",
};

const std::initializer_list<const char*> kOak = {"Oak_2K_BaseColor.png", "Oak_2K_Normal.png", "Oak_4K_BaseColor.png",
                                                  "Oak_4K_Normal.png", "Oak.json"};

// A library with the assets catalog, a fake Blender and a context that deletes zips for good.
struct AssetFixture {
    explicit AssetFixture(const std::string& tag, const std::string& mode = "") : lib(tag)
    {
        exe = plynth::test::makeFakeBlender(lib.tmp.path() / "blender", "5.2.0", mode);
        context.library = lib.paths;
        context.blender = inspectBlender(exe);
        context.run.scriptsFolder = plynth::test::makeFakeScripts(lib.tmp.path() / "scripts");
        context.removeOriginal = deleteFile;
    }

    fs::path zip(const std::string& name, std::initializer_list<const char*> entries) const
    {
        const fs::path file = lib.tmp.path() / "in" / pathFromUtf8(name);
        fs::create_directories(file.parent_path());
        plynth::test::writeZipFile(file, plynth::test::zipEntriesNamed(entries));
        return file;
    }

    ImportRequest address(const std::string& text = std::string("https://www.blenderkit.com/get-blenderkit/") + kId + "/") const
    {
        ImportRequest request;
        request.source.url = text;
        return request;
    }

    ImportRequest file(const fs::path& path) const
    {
        ImportRequest request;
        request.source.file = path;
        return request;
    }

    std::vector<nlohmann::json> jobs() const { return jobsIn(lib.tmp.path() / "blender"); }
    int items() const { return entriesIn(lib.paths.assetItems); }
    int staging() const { return entriesIn(lib.paths.assetStaging); }

    plynth::test::TestLibrary lib;
    fs::path exe;
    ImportContext context;
};

// The same for the materials catalog.
struct MaterialFixture {
    explicit MaterialFixture(const std::string& tag, const std::string& mode = "") : lib(tag)
    {
        exe = plynth::test::makeFakeBlender(lib.tmp.path() / "blender", "5.2.0", mode);
        context.library = lib.paths;
        context.blender = inspectBlender(exe);
        context.run.scriptsFolder = plynth::test::makeFakeScripts(lib.tmp.path() / "scripts");
        context.removeOriginal = deleteFile;
    }

    fs::path zip(const std::string& name, std::initializer_list<const char*> entries) const
    {
        const fs::path file = lib.tmp.path() / "in" / pathFromUtf8(name);
        fs::create_directories(file.parent_path());
        plynth::test::writeZipFile(file, plynth::test::zipEntriesNamed(entries));
        return file;
    }

    MaterialImportRequest address(const std::string& text = std::string("asset_base_id:") + kId) const
    {
        MaterialImportRequest request;
        request.source.url = text;
        return request;
    }

    std::vector<nlohmann::json> jobs() const { return jobsIn(lib.tmp.path() / "blender"); }
    int items() const { return entriesIn(lib.paths.materialItems); }
    int staging() const { return entriesIn(lib.paths.materialStaging); }

    plynth::test::MaterialTestLibrary lib;
    fs::path exe;
    ImportContext context;
};

// The info.json of an asset, read back.
AssetInfo assetInfoOf(const AssetFixture& f, const std::string& id)
{
    const fs::path file = f.lib.paths.assetItems / pathFromUtf8(id) / "info.json";
    REQUIRE(plynth::test::schemaProblem("asset-info.schema.json", nlohmann::json::parse(plynth::test::readTextFile(file))).empty());
    const AssetInfoRead read = readAssetInfo(file, id);
    plynth::test::requireOk(read.status);
    return read.info;
}

// The source.fab.json that a kept work folder holds.
nlohmann::json keptFabSource(const fs::path& keptFolder)
{
    return nlohmann::json::parse(plynth::test::readTextFile(keptFolder / "source.fab.json"));
}

// An importer for the tests that fails or names badly by the file ending. It is only chosen for those endings.
class OddImporter final : public Importer {
public:
    std::string_view id() const override { return "store_import_odd"; }
    std::string_view name() const override { return "Odd"; }
    std::string_view description() const override { return "Test importer."; }
    std::vector<std::string> fileSuffixes() const override { return {}; }
    int score(const ImportSource& source) const override
    {
        const std::string ending = pathToUtf8(source.file.extension());
        return ending == ".badentry" || ending == ".longname" ? 100 : 0;
    }
    Status plan(const ImportSource& source, const fs::path&, ImportPlan& out) const override
    {
        if (pathToUtf8(source.file.extension()) == ".badentry") return fail(ErrorCode::BadFile, "The entry is damaged.");
        out = {};
        out.file = source.file;
        out.format = "obj";
        out.sourceKind = "local_file";
        out.reference = "odd";
        out.defaultName = std::string(500, 'x');
        out.license = "not_a_license";
        return {};
    }
};

void registerOddImporter()
{
    if (!findImporter("store_import_odd")) registerImporter(std::make_unique<OddImporter>());
}

}

TEST_CASE("an address import fetches, then cleans the fetched blend", "[store_import]") {
    KitMode mode("");
    AssetFixture f("si-address");
    const std::string address = std::string("https://www.blenderkit.com/get-blenderkit/") + kId + "/";
    std::vector<ImportStage> stages;
    const ImportOutcome outcome = importAsset(*f.lib.catalog, f.context, f.address(address), [&](const ImportProgress& p) {
        if (stages.empty() || stages.back() != p.stage) stages.push_back(p.stage);
    });
    plynth::test::requireOk(outcome.status);
    REQUIRE(outcome.importer == "blenderkit");
    REQUIRE(outcome.assetId == "fake_chair");
    REQUIRE(stages == std::vector<ImportStage>{ImportStage::Preparing, ImportStage::Blender, ImportStage::Saving, ImportStage::Done});

    const auto jobs = f.jobs();
    REQUIRE(tasksOf(jobs) == std::vector<std::string>{"blenderkit_fetch", "cleanup_asset"});
    REQUIRE(jobs[0]["params"]["asset_base_id"] == kId);
    REQUIRE(jobs[0]["params"]["kind"] == "model");
    REQUIRE(jobs[1]["params"]["source"]["format"] == "blend");
    REQUIRE(endsWith(jobs[1]["params"]["source"]["file"].get<std::string>(), "/fetched/fake_item.blend"));
    REQUIRE(jobs[1]["params"]["asset_id"] == "fake_chair");

    const AssetInfo info = assetInfoOf(f, "fake_chair");
    REQUIRE(info.source.kind == "blenderkit");
    REQUIRE(info.source.reference == address);
    REQUIRE(info.name == "Fake chair");
    REQUIRE(info.license == "royalty_free");
    REQUIRE(f.staging() == 0);
    REQUIRE(f.lib.catalog->asset("fake_chair").has_value());
}

TEST_CASE("an address import keeps the cleanup parameters", "[store_import]") {
    KitMode mode("");
    AssetFixture f("si-params");
    ImportRequest request = f.address();
    request.scale = 0.01;
    request.rotateZ = 90;
    plynth::test::requireOk(importAsset(*f.lib.catalog, f.context, request).status);
    const auto jobs = f.jobs();
    REQUIRE(jobs.size() == 2);
    const auto& params = jobs[1]["params"];
    REQUIRE(params["bake_size"] == CleanupJob{}.bakeSize);
    REQUIRE(params["scale"] == 0.01);
    REQUIRE(params["rotate_z"] == 90);
    REQUIRE(params.contains("triangle_warning"));
    REQUIRE(params["thumbnail"]["render"] == true);
}

TEST_CASE("the request's id, name and a second import of an address", "[store_import]") {
    KitMode mode("");
    AssetFixture f("si-address-ids");
    ImportRequest named = f.address();
    named.id = "my_chair";
    named.name = "My chair";
    plynth::test::requireOk(importAsset(*f.lib.catalog, f.context, named).status);
    REQUIRE(assetInfoOf(f, "my_chair").name == "My chair");

    plynth::test::requireOk(importAsset(*f.lib.catalog, f.context, f.address()).status);
    const ImportOutcome again = importAsset(*f.lib.catalog, f.context, f.address());
    plynth::test::requireOk(again.status);
    REQUIRE(again.assetId == "fake_chair_2");
}

TEST_CASE("a requested id that is taken stops before the fetch", "[store_import]") {
    KitMode mode("");
    AssetFixture f("si-taken");
    f.lib.add("chair", "Chair");
    ImportRequest request = f.address();
    request.id = "chair";
    const ImportOutcome outcome = importAsset(*f.lib.catalog, f.context, request);
    REQUIRE(outcome.status.code == ErrorCode::AlreadyExists);
    REQUIRE(f.jobs().empty());
    REQUIRE(f.staging() == 0);
}

TEST_CASE("a material address refuses the asset import and keeps nothing", "[store_import]") {
    KitMode mode("material");
    AssetFixture f("si-wrong-kind");
    const ImportOutcome outcome = importAsset(*f.lib.catalog, f.context, f.address());
    REQUIRE(outcome.status.code == ErrorCode::InvalidValue);
    REQUIRE(outcome.status.message ==
            "This BlenderKit item is a material. Import it in the Materials tab or with plynth materials import.");
    REQUIRE(tasksOf(f.jobs()) == std::vector<std::string>{"blenderkit_fetch"});
    REQUIRE(f.items() == 0);
    REQUIRE(f.staging() == 0);
    REQUIRE(outcome.keptWorkFolder.empty());
    REQUIRE_FALSE(f.lib.catalog->asset("fake_chair").has_value());
}

TEST_CASE("a fetch that is not signed in fails, and the work folder is kept and named", "[store_import]") {
    KitMode mode("not_signed_in");
    AssetFixture f("si-signed-out");
    const ImportOutcome outcome = importAsset(*f.lib.catalog, f.context, f.address());
    REQUIRE(outcome.status.code == ErrorCode::BlenderFailed);
    REQUIRE(outcome.status.message.find("Sign in to BlenderKit inside Blender first") == 0);
    REQUIRE_FALSE(outcome.keptWorkFolder.empty());
    REQUIRE(fs::is_directory(outcome.keptWorkFolder));
    REQUIRE(outcome.status.message.find(pathToUtf8(outcome.keptWorkFolder)) != std::string::npos);
    REQUIRE(fs::is_regular_file(outcome.keptWorkFolder / "job.json"));
    REQUIRE(tasksOf(f.jobs()) == std::vector<std::string>{"blenderkit_fetch"});
    REQUIRE(f.items() == 0);
}

TEST_CASE("a stop after the fetch cancels and keeps nothing", "[store_import]") {
    KitMode mode("");
    AssetFixture f("si-stop");
    std::stop_source source;
    const ImportOutcome outcome = importAsset(
        *f.lib.catalog, f.context, f.address(),
        [&](const ImportProgress& p) {
            if (p.blender.step == "copy") source.request_stop();
        },
        source.get_token());
    REQUIRE(outcome.status.code == ErrorCode::Cancelled);
    REQUIRE(f.items() == 0);
    REQUIRE(f.staging() == 0);
    REQUIRE(outcome.keptWorkFolder.empty());
}

TEST_CASE("an address that is not for BlenderKit is refused before any job", "[store_import]") {
    AssetFixture f("si-not-kit");
    const ImportOutcome outcome = importAsset(*f.lib.catalog, f.context, f.address("https://example.com/x"));
    REQUIRE(outcome.status.code == ErrorCode::InvalidValue);
    REQUIRE(outcome.status.message.find("https://example.com/x") != std::string::npos);
    REQUIRE(f.jobs().empty());
}

TEST_CASE("a Fab variants zip runs the cleanup with format fab on the source file", "[store_import]") {
    AssetFixture f("si-fab");
    const fs::path zip = f.zip("chair_variants.zip", kVariants);
    const ImportOutcome outcome = importAsset(*f.lib.catalog, f.context, f.file(zip));
    plynth::test::requireOk(outcome.status);
    REQUIRE(outcome.importer == "fab_zip");
    REQUIRE(outcome.originalRemoved);
    REQUIRE_FALSE(fs::exists(zip));

    const auto jobs = f.jobs();
    REQUIRE(tasksOf(jobs) == std::vector<std::string>{"cleanup_asset"});
    REQUIRE(jobs[0]["params"]["source"]["format"] == "fab");
    const std::string source = jobs[0]["params"]["source"]["file"].get<std::string>();
    REQUIRE(endsWith(source, "/source.fab.json"));
    REQUIRE(source.find(".staging") != std::string::npos);

    const AssetInfo info = assetInfoOf(f, outcome.assetId);
    REQUIRE(info.source.kind == "fab_zip");
    REQUIRE(info.source.reference == "chair_variants.zip");
    REQUIRE(info.name == "Chair");
    REQUIRE(info.license == "royalty_free");
    REQUIRE(f.staging() == 0);
}

TEST_CASE("a Fab zip stays when the request keeps it", "[store_import]") {
    AssetFixture f("si-fab-keep");
    const fs::path zip = f.zip("chair_variants.zip", kVariants);
    ImportRequest request = f.file(zip);
    request.keepOriginal = true;
    const ImportOutcome outcome = importAsset(*f.lib.catalog, f.context, request);
    plynth::test::requireOk(outcome.status);
    REQUIRE_FALSE(outcome.originalRemoved);
    REQUIRE(fs::exists(zip));
}

TEST_CASE("importer and importerOptions reach the plan", "[store_import]") {
    AssetFixture f("si-options", "fail");
    const fs::path plain = f.zip("box.zip", {"box.obj"});

    ImportRequest named = f.file(plain);
    named.importer = "fab_zip";
    const ImportOutcome forced = importAsset(*f.lib.catalog, f.context, named);
    REQUIRE(forced.status.code == ErrorCode::BlenderFailed);
    REQUIRE(forced.importer == "fab_zip");
    REQUIRE(keptFabSource(forced.keptWorkFolder)["model"] == "box.obj");

    const fs::path variants = f.zip("chair_variants.zip", kVariants);
    const ImportOutcome medium = importAsset(*f.lib.catalog, f.context, f.file(variants));
    REQUIRE(keptFabSource(medium.keptWorkFolder)["model"] == "Chair/FBX/Medium/Chair_LOD0.fbx");

    ImportRequest high = f.file(variants);
    high.importerOptions = nlohmann::ordered_json{{"quality", "high"}};
    const ImportOutcome picked = importAsset(*f.lib.catalog, f.context, high);
    REQUIRE(keptFabSource(picked.keptWorkFolder)["model"] == "Chair/FBX/High/Chair_LOD0.fbx");
    REQUIRE(f.jobs().back()["params"]["source"]["format"] == "fab");
}

TEST_CASE("an importer that is named but cannot read the source is refused", "[store_import]") {
    AssetFixture f("si-cannot");
    ImportRequest request = f.address();
    request.importer = "local_file";
    REQUIRE(importAsset(*f.lib.catalog, f.context, request).status.code == ErrorCode::InvalidValue);
    request.importer = "nothing_here";
    REQUIRE(importAsset(*f.lib.catalog, f.context, request).status.code == ErrorCode::NotFound);

    ImportRequest bad = f.file(f.zip("box.zip", {"box.obj"}));
    bad.importer = "fab_zip";
    bad.importerOptions = nlohmann::ordered_json{{"quality", "ultra"}};
    REQUIRE(importAsset(*f.lib.catalog, f.context, bad).status.code == ErrorCode::InvalidValue);
    REQUIRE(f.jobs().empty());
    REQUIRE(f.staging() == 0);
}

TEST_CASE("a saved Fab option reaches the plan", "[store_import]") {
    AssetFixture f("si-saved", "fail");
    plynth::test::requireOk(setIntegrationOptions(f.lib.paths, "fab", nlohmann::ordered_json{{"quality", "high"}}));
    const ImportOutcome outcome = importAsset(*f.lib.catalog, f.context, f.file(f.zip("chair_variants.zip", kVariants)));
    REQUIRE(outcome.status.code == ErrorCode::BlenderFailed);
    REQUIRE(keptFabSource(outcome.keptWorkFolder)["model"] == "Chair/FBX/High/Chair_LOD0.fbx");
}

TEST_CASE("a license the catalog lacks becomes unknown, and a bad plan name falls back to the id", "[store_import]") {
    registerOddImporter();
    AssetFixture f("si-odd");
    plynth::test::writeBytes(f.lib.tmp.path() / "in" / "lamp.longname", "x");
    const ImportOutcome outcome = importAsset(*f.lib.catalog, f.context, f.file(f.lib.tmp.path() / "in" / "lamp.longname"));
    plynth::test::requireOk(outcome.status);
    REQUIRE(outcome.importer == "store_import_odd");
    const AssetInfo info = assetInfoOf(f, "lamp");
    REQUIRE(info.license == kUnknownLicenseId);
    REQUIRE(info.name == "Lamp");
}

TEST_CASE("a Fab license that is not in the catalog becomes unknown", "[store_import]") {
    AssetFixture f("si-fab-license");
    plynth::test::requireOk(setIntegrationOptions(f.lib.paths, "fab", nlohmann::ordered_json{{"license", "not_a_license"}}));
    const ImportOutcome outcome = importAsset(*f.lib.catalog, f.context, f.file(f.zip("chair_variants.zip", kVariants)));
    plynth::test::requireOk(outcome.status);
    REQUIRE(assetInfoOf(f, outcome.assetId).license == kUnknownLicenseId);
}

TEST_CASE("the inbox picks the Fab importer for a zip with versions", "[store_import]") {
    AssetFixture f("si-inbox");
    const fs::path zip = f.lib.paths.assetInbox / "chair_variants.zip";
    plynth::test::writeZipFile(zip, plynth::test::zipEntriesNamed(kVariants));
    const ImportOutcome outcome = importInboxEntry(*f.lib.catalog, f.context, zip);
    plynth::test::requireOk(outcome.status);
    REQUIRE(outcome.importer == "fab_zip");
    REQUIRE(f.jobs()[0]["params"]["source"]["format"] == "fab");
    REQUIRE_FALSE(fs::exists(zip));
}

TEST_CASE("a zip with several models goes to failed with the sentence", "[store_import]") {
    AssetFixture f("si-inbox-pack");
    const fs::path zip = f.lib.paths.assetInbox / "pack.zip";
    plynth::test::writeZipFile(zip, plynth::test::zipEntriesNamed({"Chair/Chair_High.fbx", "Box/Box_High.fbx", "Box/Box_Low.fbx"}));
    const ImportOutcome outcome = importInboxEntry(*f.lib.catalog, f.context, zip);
    REQUIRE(outcome.status.code == ErrorCode::InvalidValue);
    REQUIRE(fs::exists(f.lib.paths.assetInboxFailed / "pack.zip"));
    const std::string note = plynth::test::readTextFile(f.lib.paths.assetInboxFailed / "pack.zip.error.txt");
    REQUIRE(note.find("Chair") != std::string::npos);
    REQUIRE(note.find("Box") != std::string::npos);
}

TEST_CASE("a broken integrations.json leaves inbox entries in place", "[store_import]") {
    AssetFixture f("si-inbox-broken");
    plynth::test::writeBytes(integrationsFile(f.lib.paths), "{ not json");
    const fs::path zip = f.lib.paths.assetInbox / "chair_variants.zip";
    plynth::test::writeZipFile(zip, plynth::test::zipEntriesNamed(kVariants));
    const ImportOutcome outcome = importInboxEntry(*f.lib.catalog, f.context, zip);
    REQUIRE(outcome.status.code == ErrorCode::BadFile);
    REQUIRE(fs::exists(zip));
    REQUIRE(entriesIn(f.lib.paths.assetInboxFailed) == 0);
    REQUIRE(f.jobs().empty());

    MaterialFixture m("si-inbox-broken-m");
    plynth::test::writeBytes(integrationsFile(m.lib.paths), "{ not json");
    const fs::path oak = m.lib.paths.materialInbox / "oak.zip";
    plynth::test::writeZipFile(oak, plynth::test::zipEntriesNamed(kOak));
    const MaterialImportOutcome material = importMaterialInboxEntry(*m.lib.catalog, m.context, oak);
    REQUIRE(material.status.code == ErrorCode::BadFile);
    REQUIRE(fs::exists(oak));
    REQUIRE(entriesIn(m.lib.paths.materialInboxFailed) == 0);
}

TEST_CASE("a damaged inbox entry itself still goes to failed", "[store_import]") {
    registerOddImporter();
    AssetFixture f("si-inbox-bad-entry");
    const fs::path entry = f.lib.paths.assetInbox / "lamp.badentry";
    plynth::test::writeBytes(entry, "x");
    const ImportOutcome outcome = importInboxEntry(*f.lib.catalog, f.context, entry);
    REQUIRE(outcome.status.code == ErrorCode::BadFile);
    REQUIRE_FALSE(fs::exists(entry));
    REQUIRE(fs::exists(f.lib.paths.assetInboxFailed / "lamp.badentry"));
}

TEST_CASE("a refusing Fab importer still gives its details to the preview", "[store_import]") {
    AssetFixture f("si-details");
    const fs::path pack = f.zip("pack.zip", {"Chair/Chair_High.fbx", "Box/Box_High.fbx"});
    ImportRequest request = f.file(pack);
    request.importer = "fab_zip";
    const ImportPreview preview = previewAssetImport(f.lib.paths, request);
    REQUIRE(preview.status.code == ErrorCode::InvalidValue);
    REQUIRE(preview.importer == "fab_zip");
    REQUIRE(preview.plan["details"]["models"].size() == 2);
    REQUIRE(preview.plan["details"]["picked"].is_null());
    REQUIRE(f.staging() == 0);

    const ImportPreview fine = previewAssetImport(f.lib.paths, f.file(f.zip("chair_variants.zip", kVariants)));
    plynth::test::requireOk(fine.status);
    REQUIRE(fine.plan.contains("file"));
}

TEST_CASE("a material address fetches, then cleans the blend material", "[store_import]") {
    KitMode mode("material");
    MaterialFixture f("si-material");
    const MaterialImportOutcome outcome = importMaterial(*f.lib.catalog, f.context, f.address());
    plynth::test::requireOk(outcome.status);
    REQUIRE(outcome.importer == "blenderkit");

    const auto jobs = f.jobs();
    REQUIRE(tasksOf(jobs) == std::vector<std::string>{"blenderkit_fetch", "cleanup_material"});
    REQUIRE(jobs[0]["params"]["kind"] == "material");
    REQUIRE(jobs[1]["params"]["source"]["kind"] == "blend");
    REQUIRE(jobs[1]["params"]["source"]["material"] == "Fake material");
    REQUIRE(endsWith(jobs[1]["params"]["source"]["path"].get<std::string>(), "/fetched/fake_item.blend"));
    REQUIRE(jobs[1]["params"]["bake_size"] == kDefaultBakeSize);

    const fs::path info = f.lib.paths.materialItems / pathFromUtf8(outcome.materialId) / "info.json";
    REQUIRE(plynth::test::schemaProblem("material-info.schema.json", nlohmann::json::parse(plynth::test::readTextFile(info))).empty());
    const MaterialInfoRead read = readMaterialInfo(info, outcome.materialId);
    plynth::test::requireOk(read.status);
    REQUIRE(read.info.source.kind == "blenderkit");
    REQUIRE(read.info.source.reference == std::string("asset_base_id:") + kId);
    REQUIRE(read.info.name == "Fake chair");
    REQUIRE(read.info.license == "royalty_free");
    REQUIRE(f.staging() == 0);
}

TEST_CASE("the request's blend material and name win over the fetch's", "[store_import]") {
    KitMode mode("material");
    MaterialFixture f("si-material-request");
    MaterialImportRequest request = f.address();
    request.blendMaterial = "Other";
    request.name = "Painted wall";
    const MaterialImportOutcome outcome = importMaterial(*f.lib.catalog, f.context, request);
    plynth::test::requireOk(outcome.status);
    REQUIRE(f.jobs()[1]["params"]["source"]["material"] == "Other");
    REQUIRE(f.lib.catalog->material(outcome.materialId)->name == "Painted wall");
}

TEST_CASE("a model address refuses the material import and keeps nothing", "[store_import]") {
    KitMode mode("");
    MaterialFixture f("si-material-wrong");
    const MaterialImportOutcome outcome = importMaterial(*f.lib.catalog, f.context, f.address());
    REQUIRE(outcome.status.code == ErrorCode::InvalidValue);
    REQUIRE(outcome.status.message ==
            "This BlenderKit item is a model. Import it in the Assets tab or with plynth assets import.");
    REQUIRE(f.items() == 0);
    REQUIRE(f.staging() == 0);
}

TEST_CASE("a material fetch that is not signed in keeps the work folder", "[store_import]") {
    KitMode mode("not_signed_in");
    MaterialFixture f("si-material-signed-out");
    const MaterialImportOutcome outcome = importMaterial(*f.lib.catalog, f.context, f.address());
    REQUIRE(outcome.status.code == ErrorCode::BlenderFailed);
    REQUIRE(outcome.status.message.find("Sign in to BlenderKit inside Blender first") == 0);
    REQUIRE(fs::is_directory(outcome.keptWorkFolder));
    REQUIRE(f.items() == 0);
}

TEST_CASE("an address that is not for BlenderKit is refused as a material", "[store_import]") {
    MaterialFixture f("si-material-not-kit");
    const MaterialImportOutcome outcome = importMaterial(*f.lib.catalog, f.context, f.address("https://example.com/x"));
    REQUIRE(outcome.status.code == ErrorCode::InvalidValue);
    REQUIRE(outcome.status.message.find("https://example.com/x") != std::string::npos);
    REQUIRE(f.jobs().empty());
}

TEST_CASE("a Fab texture zip runs the material cleanup with source fab", "[store_import]") {
    MaterialFixture f("si-material-fab");
    const fs::path zip = f.zip("oak.zip", kOak);
    MaterialImportRequest request;
    request.source.file = zip;
    const MaterialImportOutcome outcome = importMaterial(*f.lib.catalog, f.context, request);
    plynth::test::requireOk(outcome.status);
    REQUIRE(outcome.importer == "fab_zip");
    REQUIRE(outcome.originalRemoved);
    REQUIRE_FALSE(fs::exists(zip));

    const auto jobs = f.jobs();
    REQUIRE(tasksOf(jobs) == std::vector<std::string>{"cleanup_material"});
    REQUIRE(jobs[0]["params"]["source"]["kind"] == "fab");
    REQUIRE(endsWith(jobs[0]["params"]["source"]["path"].get<std::string>(), "/source.fab.json"));
    REQUIRE(f.lib.catalog->material(outcome.materialId)->name == "Oak");
    const fs::path info = f.lib.paths.materialItems / pathFromUtf8(outcome.materialId) / "info.json";
    const MaterialInfoRead read = readMaterialInfo(info, outcome.materialId);
    plynth::test::requireOk(read.status);
    REQUIRE(read.info.source.kind == "fab_zip");
    REQUIRE(read.info.license == "royalty_free");
}

TEST_CASE("a material import keeps the Fab zip when asked", "[store_import]") {
    MaterialFixture f("si-material-fab-keep");
    const fs::path zip = f.zip("oak.zip", kOak);
    MaterialImportRequest request;
    request.source.file = zip;
    request.keepOriginal = true;
    plynth::test::requireOk(importMaterial(*f.lib.catalog, f.context, request).status);
    REQUIRE(fs::exists(zip));
}

TEST_CASE("material options reach the plan and the inbox picks the Fab importer", "[store_import]") {
    MaterialFixture f("si-material-inbox", "fail");
    MaterialImportRequest request;
    request.source.file = f.zip("oak.zip", kOak);
    request.importerOptions = nlohmann::ordered_json{{"max_texture_size", 2048}};
    const MaterialImportOutcome limited = importMaterial(*f.lib.catalog, f.context, request);
    REQUIRE(limited.status.code == ErrorCode::BlenderFailed);
    const nlohmann::json source = keptFabSource(limited.keptWorkFolder);
    for (const auto& texture : source["textures"]) REQUIRE(texture.get<std::string>().find("4K") == std::string::npos);
    REQUIRE_FALSE(source["textures"].empty());

    MaterialFixture g("si-material-inbox-ok");
    const fs::path inbox = g.lib.paths.materialInbox / "oak.zip";
    plynth::test::writeZipFile(inbox, plynth::test::zipEntriesNamed(kOak));
    const MaterialImportOutcome outcome = importMaterialInboxEntry(*g.lib.catalog, g.context, inbox);
    plynth::test::requireOk(outcome.status);
    REQUIRE(outcome.importer == "fab_zip");
    REQUIRE_FALSE(fs::exists(inbox));
}
