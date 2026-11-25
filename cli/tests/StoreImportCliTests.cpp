#include "FakeBlender.h"
#include "MaterialLibrary.h"

#include "../../tests/core/ZipTestWriter.h"

#include <plynth/core/Process.h>
#include <plynth/core/ProjectFiles.h>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using plynth::cli::kExitOk;
using plynth::cli::testing::errorCode;
using plynth::cli::testing::installFakeBlender;
using plynth::cli::testing::PrivateUser;
using plynth::cli::testing::ScopedEnvironment;
using plynth::cli::testing::MaterialSettingsReset;
using plynth::cli::testing::runChecked;
using plynth::cli::testing::TempFolder;
using plynth::cli::testing::utf8;

namespace {

const char* const kId = "0c1d4a2e-1111-2222-3333-444455556666";

// A library in temp set in the settings. Returns its folder.
std::filesystem::path setUp(const TempFolder& temp) {
    const auto library = temp.path() / "Library";
    REQUIRE(runChecked({"library", "init", utf8(library), "--use"}).exitCode == kExitOk);
    return library;
}

// A Fab style model zip with three qualities and one texture.
std::filesystem::path variantsZip(const TempFolder& temp) {
    const auto file = temp.path() / "variants.zip";
    plynth::test::writeZipFile(
        file, plynth::test::zipEntriesNamed({"Chair/FBX/Low/Chair_LOD0.fbx", "Chair/FBX/Medium/Chair_LOD0.fbx",
                                             "Chair/FBX/Medium/Textures/T_Chair_BaseColor_2K.png",
                                             "Chair/FBX/High/Chair_LOD0.fbx", "Chair/Chair.json"}));
    return file;
}

// A Fab style material zip.
std::filesystem::path oakZip(const TempFolder& temp) {
    const auto file = temp.path() / "oak.zip";
    plynth::test::writeZipFile(
        file, plynth::test::zipEntriesNamed({"Oak_2K_BaseColor.png", "Oak_2K_Normal.png", "Oak_4K_BaseColor.png",
                                             "Oak_4K_Normal.png", "Oak.json"}));
    return file;
}

// How many entries the library's asset items folder holds.
int itemCount(const std::filesystem::path& library) {
    std::error_code ignored;
    int count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(library / "assets" / "items", ignored)) {
        (void)entry;
        ++count;
    }
    return count;
}

std::string blenderKitUrl() { return std::string("https://www.blenderkit.com/asset-gallery-detail/") + kId + "/"; }

}

TEST_CASE("assets import --dry-run of a Fab zip shows the pick and imports nothing", "[cli][store_import]") {
    MaterialSettingsReset reset;
    TempFolder temp("store-dry");
    const auto library = setUp(temp);
    const auto zip = variantsZip(temp);

    const auto outcome = runChecked({"assets", "import", utf8(zip), "--dry-run"});
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("command") == "assets import");
    const auto& result = outcome.answer.at("result");
    CHECK(result.at("importer") == "fab_zip");
    CHECK(result.at("integration") == "fab");
    CHECK(result.at("plan").at("details").at("picked") == "Chair/FBX/Medium/Chair_LOD0.fbx");
    CHECK(itemCount(library) == 0);
    CHECK(outcome.progress.empty());
}

TEST_CASE("assets import --option quality, model and maps reach the plan", "[cli][store_import]") {
    MaterialSettingsReset reset;
    TempFolder temp("store-options");
    setUp(temp);
    const auto zip = variantsZip(temp);

    const auto high = runChecked({"assets", "import", utf8(zip), "--dry-run", "--option", "quality=high"});
    REQUIRE(high.exitCode == kExitOk);
    CHECK(high.answer.at("result").at("options").at("quality") == "high");
    CHECK(high.answer.at("result").at("plan").at("details").at("picked") == "Chair/FBX/High/Chair_LOD0.fbx");

    const auto model = runChecked({"assets", "import", utf8(zip), "--dry-run", "--option",
                                   "model=Chair/FBX/Low/Chair_LOD0.fbx"});
    REQUIRE(model.exitCode == kExitOk);
    CHECK(model.answer.at("result").at("plan").at("details").at("picked") == "Chair/FBX/Low/Chair_LOD0.fbx");

    const auto maps = runChecked({"assets", "import", utf8(zip), "--dry-run", "--option",
                                  "maps.*.base_color=Chair/FBX/Medium/Textures/T_Chair_BaseColor_2K.png"});
    REQUIRE(maps.exitCode == kExitOk);
    CHECK(maps.answer.at("result").at("options").at("maps").at("*").at("base_color") ==
          "Chair/FBX/Medium/Textures/T_Chair_BaseColor_2K.png");

    const auto bad = runChecked({"assets", "import", utf8(zip), "--dry-run", "--option", "quality"});
    CHECK(errorCode(bad) == "invalid_value");
    const auto ultra = runChecked({"assets", "import", utf8(zip), "--dry-run", "--option", "quality=ultra"});
    CHECK(errorCode(ultra) == "invalid_value");
}

TEST_CASE("assets import --importer names the importer", "[cli][store_import]") {
    MaterialSettingsReset reset;
    TempFolder temp("store-importer");
    setUp(temp);
    const auto zip = variantsZip(temp);

    const auto local = runChecked({"assets", "import", utf8(zip), "--dry-run", "--importer", "local_file"});
    REQUIRE(local.exitCode == kExitOk);
    CHECK(local.answer.at("result").at("importer") == "local_file");

    CHECK(errorCode(runChecked({"assets", "import", utf8(zip), "--dry-run", "--importer", "nope"})) == "not_found");
    CHECK(errorCode(runChecked({"assets", "import", utf8(zip), "--importer", "nope"})) == "not_found");

    const auto kit = runChecked({"assets", "import", utf8(zip), "--dry-run", "--importer", "blenderkit"});
    CHECK(errorCode(kit) == "invalid_value");
    CHECK(kit.answer.at("error").at("message") == "BlenderKit cannot read this.");
}

TEST_CASE("assets import takes a BlenderKit address", "[cli][store_import]") {
    MaterialSettingsReset reset;
    TempFolder temp("store-address");
    setUp(temp);

    for (const std::string& text : {blenderKitUrl(), std::string("asset_base_id:") + kId,
                                    std::string("asset_base_id:") + kId + " asset_type:model", std::string(kId)}) {
        const auto outcome = runChecked({"assets", "import", text, "--dry-run"});
        REQUIRE(outcome.exitCode == kExitOk);
        const auto& result = outcome.answer.at("result");
        CHECK(result.at("importer") == "blenderkit");
        CHECK(result.at("plan").at("file").is_null());
        CHECK(result.at("plan").at("fetch").at("task") == "blenderkit_fetch");
        CHECK(result.at("plan").at("details").at("asset_base_id") == kId);
    }

    const auto other = runChecked({"assets", "import", "https://example.com/x", "--dry-run"});
    CHECK(errorCode(other) == "invalid_value");
    const auto real = runChecked({"assets", "import", "https://example.com/x"});
    CHECK(errorCode(real) == "invalid_value");

    const auto perImport = runChecked({"assets", "import", blenderKitUrl(), "--dry-run", "--option", "resolution=4k"});
    CHECK(errorCode(perImport) == "invalid_value");
}

TEST_CASE("a BlenderKit Get link is refused with the sentence that names the forms that work", "[cli][store_import]") {
    MaterialSettingsReset reset;
    TempFolder temp("store-get-link");
    setUp(temp);
    const std::string sentence = "This is a Get link, which holds a version id Plynth cannot look up. Paste the address "
                                 "of the item's page (blenderkit.com/asset-gallery-detail/...) or copy the asset ID in "
                                 "BlenderKit inside Blender.";
    for (const std::string& link : {std::string("https://www.blendkit.com/get-blendkit/") + kId + "/",
                                    std::string("https://www.blenderkit.com/get-blenderkit/") + kId + "/"}) {
        INFO(link);
        const auto asset = runChecked({"assets", "import", link, "--dry-run"});
        CHECK(errorCode(asset) == "invalid_value");
        CHECK(asset.answer.at("error").at("message") == sentence);
        const auto material = runChecked({"materials", "import", link, "--dry-run"});
        CHECK(errorCode(material) == "invalid_value");
        CHECK(material.answer.at("error").at("message") == sentence);
    }
}

TEST_CASE("a bare id that names a file is a file", "[cli][store_import]") {
    MaterialSettingsReset reset;
    TempFolder temp("store-bare");
    setUp(temp);
    const auto outcome = runChecked({"assets", "import", kId, "--dry-run"}, temp.path());
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("result").at("importer") == "blenderkit");

    std::ofstream(temp.path() / kId) << "v 0 0 0\n";
    const auto file = runChecked({"assets", "import", kId, "--dry-run"}, temp.path());
    CHECK(file.answer.at("ok") == false);
}

TEST_CASE("library integrations lists local, blenderkit and fab in order", "[cli][store_import]") {
    MaterialSettingsReset reset;
    TempFolder temp("store-list");
    const PrivateUser user(temp);
    setUp(temp);
    const auto fake = installFakeBlender(temp.path() / "fb", "5.2.0");

    const auto outcome = runChecked({"library", "integrations", "--no-addons", "--blender", utf8(fake)});
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& list = outcome.answer.at("result").at("integrations");
    REQUIRE(list.size() == 3);
    CHECK(list.at(0).at("id") == "local");
    CHECK(list.at(1).at("id") == "blenderkit");
    CHECK(list.at(2).at("id") == "fab");
    CHECK(list.at(1).at("state") == "not_checked");
    CHECK(list.at(1).at("takes_addresses") == true);
    CHECK(list.at(2).at("options").at("quality") == "medium");
}

TEST_CASE("library integrations carries the note and no window option", "[cli][store_import]") {
    MaterialSettingsReset reset;
    TempFolder temp("store-note");
    const PrivateUser user(temp);
    setUp(temp);
    const auto fake = installFakeBlender(temp.path() / "fb", "5.2.0");

    const auto outcome = runChecked({"library", "integrations", "--no-addons", "--blender", utf8(fake)});
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& list = outcome.answer.at("result").at("integrations");
    REQUIRE(list.size() == 3);
    for (const auto& entry : list) {
        REQUIRE(entry.contains("note"));
        CHECK(entry.at("note").is_string());
    }
    CHECK(list.at(0).at("note") == "");
    CHECK(list.at(1).at("note").get<std::string>().size() > 0);
    CHECK_FALSE(list.at(1).at("options").contains("blender_window"));
    CHECK_FALSE(list.at(1).at("defaults").contains("blender_window"));
}

TEST_CASE("library integration set saves, show reads, reset clears", "[cli][store_import]") {
    MaterialSettingsReset reset;
    TempFolder temp("store-set");
    const auto library = setUp(temp);

    const auto set = runChecked({"library", "integration", "set", "fab", "--option", "quality=high"});
    REQUIRE(set.exitCode == kExitOk);
    CHECK(set.answer.at("result").at("id") == "fab");
    CHECK(set.answer.at("result").at("options").at("quality") == "high");
    CHECK(set.answer.at("result").at("defaults").at("quality") == "medium");

    const auto file = library / "integrations.json";
    REQUIRE(std::filesystem::exists(file));
    {
        std::ifstream schemaFile(std::filesystem::path(PLYNTH_SCHEMA_DIR) / "integrations.schema.json");
        nlohmann::json_schema::json_validator validator;
        validator.set_root_schema(nlohmann::json::parse(schemaFile));
        std::ifstream saved(file);
        CHECK_NOTHROW(validator.validate(nlohmann::json::parse(saved)));
    }

    const auto show = runChecked({"library", "integration", "show", "fab"});
    REQUIRE(show.exitCode == kExitOk);
    CHECK(show.answer.at("result").at("options").at("quality") == "high");

    const auto dry = runChecked({"assets", "import", utf8(variantsZip(temp)), "--dry-run"});
    REQUIRE(dry.exitCode == kExitOk);
    CHECK(dry.answer.at("result").at("plan").at("details").at("picked") == "Chair/FBX/High/Chair_LOD0.fbx");

    const auto cleared = runChecked({"library", "integration", "reset", "fab"});
    REQUIRE(cleared.exitCode == kExitOk);
    CHECK(cleared.answer.at("result").at("options").at("quality") == "medium");
    const auto again = runChecked({"library", "integration", "show", "fab"});
    CHECK(again.answer.at("result").at("options").at("quality") == "medium");
}

TEST_CASE("library integration commands refuse bad input", "[cli][store_import]") {
    MaterialSettingsReset reset;
    TempFolder temp("store-bad");
    const auto library = setUp(temp);

    CHECK(errorCode(runChecked({"library", "integration", "set", "fab", "--option", "model=x"})) == "invalid_value");
    CHECK(errorCode(runChecked({"library", "integration", "set", "fab", "--option", "quality=ultra"})) ==
          "invalid_value");
    CHECK_FALSE(std::filesystem::exists(library / "integrations.json"));
    CHECK(errorCode(runChecked({"library", "integration", "set", "nope", "--option", "a=1"})) == "not_found");
    CHECK(errorCode(runChecked({"library", "integration", "show", "nope"})) == "not_found");
    CHECK(errorCode(runChecked({"library", "integration", "reset", "nope"})) == "not_found");
    CHECK(runChecked({"library", "integration", "set", "fab"}).exitCode == plynth::cli::kExitBadArguments);
}

TEST_CASE("materials import --dry-run previews a Fab zip", "[cli][store_import]") {
    MaterialSettingsReset reset;
    TempFolder temp("store-material");
    const auto library = setUp(temp);

    const auto outcome = runChecked({"materials", "import", utf8(oakZip(temp)), "--dry-run"});
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("command") == "materials import");
    CHECK(outcome.answer.at("result").at("importer") == "fab_zip");
    CHECK(outcome.answer.at("result").at("plan").at("source_kind") == "fab_zip");
    CHECK_FALSE(std::filesystem::exists(library / "materials" / "items" / "oak"));

    const auto kit = runChecked({"materials", "import", blenderKitUrl(), "--dry-run"});
    REQUIRE(kit.exitCode == kExitOk);
    CHECK(kit.answer.at("result").at("importer") == "blenderkit");
    CHECK(kit.answer.at("result").at("plan").at("fetch").at("kind") == "material");

    const auto two = runChecked({"materials", "import", utf8(oakZip(temp)), blenderKitUrl(), "--dry-run"});
    REQUIRE(two.exitCode == kExitOk);
    CHECK(two.answer.at("result").at("previews").size() == 2);

    CHECK(errorCode(runChecked({"materials", "import", utf8(oakZip(temp)), "--dry-run", "--importer", "nope"})) ==
          "not_found");
    CHECK(errorCode(runChecked({"materials", "import", "https://example.com/x", "--dry-run"})) == "invalid_value");
}

TEST_CASE("help lists the store commands and options", "[cli][store_import]") {
    const auto outcome = runChecked({"help"});
    REQUIRE(outcome.exitCode == kExitOk);
    std::vector<std::string> names;
    bool dryRun = false;
    for (const auto& command : outcome.answer.at("result").at("commands")) {
        const std::string name = command.at("command");
        names.push_back(name);
        if (name == "assets import") {
            for (const auto& option : command.at("options")) {
                dryRun = dryRun || option.at("name") == "--dry-run";
            }
        }
    }
    for (const char* name : {"library integrations", "library integration show", "library integration set",
                             "library integration reset"}) {
        CHECK(std::find(names.begin(), names.end(), name) != names.end());
    }
    CHECK(dryRun);
}

namespace {

// The folder a test's fake Blender writes its records to. It is labelled low, because a store fetch runs the
// fake at low integrity. The fake's own folder is never labelled, or every start of it would be low.
std::filesystem::path recordsOf(const TempFolder& temp) {
    const auto folder = temp.path() / "fl";
    if (!std::filesystem::exists(folder)) {
        const plynth::Status labelled = plynth::makeLowIntegrityFolder(folder);
        INFO(labelled.message);
        REQUIRE(labelled.ok());
    }
    return folder;
}

// A fake Blender next to the library of a test that sends its records to recordsOf while this object lives.
// It turns into the program's path as text, so it can stand in an argument list.
class FetchFake {
public:
    explicit FetchFake(const TempFolder& temp)
        : records_(recordsOf(temp)), variable_("FAKE_BLENDER_RECORDS", utf8(records_)),
          path_(utf8(installFakeBlender(temp.path() / "fb", "5.2.0"))) {}
    operator std::string() const { return path_; }

private:
    std::filesystem::path records_;
    ScopedEnvironment variable_;
    std::string path_;
};

// A fake Blender for one test, as FetchFake.
FetchFake fakeBlender(const TempFolder& temp) {
    return FetchFake(temp);
}

// Every job a fake Blender wrote to a folder, in order.
std::vector<nlohmann::json> jobsIn(const std::filesystem::path& folder) {
    std::vector<nlohmann::json> jobs;
    std::ifstream in(folder / "fake_jobs.log", std::ios::binary);
    for (std::string line; std::getline(in, line);) {
        if (!line.empty()) {
            jobs.push_back(nlohmann::json::parse(line));
        }
    }
    return jobs;
}

// Every job the test's FetchFake was given, in order.
std::vector<nlohmann::json> jobsLogged(const TempFolder& temp) {
    return jobsIn(recordsOf(temp));
}

std::vector<std::string> tasksOf(const std::vector<nlohmann::json>& jobs) {
    std::vector<std::string> tasks;
    for (const auto& job : jobs) {
        tasks.push_back(job.at("task"));
    }
    return tasks;
}

}

TEST_CASE("assets import of a BlenderKit address fetches, then cleans up", "[cli][store_import]") {
    MaterialSettingsReset reset;
    TempFolder temp("store-kit-import");
    const auto library = setUp(temp);
    const auto fake = fakeBlender(temp);

    const auto outcome = runChecked({"assets", "import", blenderKitUrl(), "--blender", fake});
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& result = outcome.answer.at("result");
    CHECK(result.at("importer") == "blenderkit");
    CHECK(result.at("asset").at("id") == "fake_chair");
    CHECK(result.at("asset").at("name") == "Fake chair");
    CHECK(itemCount(library) == 1);
    CHECK(tasksOf(jobsLogged(temp)) == std::vector<std::string>{"blenderkit_fetch", "cleanup_asset"});
}

TEST_CASE("assets import of a BlenderKit material id is refused with a sentence", "[cli][store_import]") {
    MaterialSettingsReset reset;
    TempFolder temp("store-kit-wrong");
    const auto library = setUp(temp);
    ScopedEnvironment mode("FAKE_BLENDERKIT", "mislabel");

    const auto outcome = runChecked({"assets", "import", blenderKitUrl(), "--blender", fakeBlender(temp)});
    CHECK(outcome.exitCode != kExitOk);
    CHECK(errorCode(outcome) == "invalid_value");
    CHECK(itemCount(library) == 0);
}

TEST_CASE("a signed out BlenderKit is blender_failed with the sign in sentence", "[cli][store_import]") {
    MaterialSettingsReset reset;
    TempFolder temp("store-kit-signed-out");
    setUp(temp);
    ScopedEnvironment mode("FAKE_BLENDERKIT", "not_signed_in");

    const auto outcome = runChecked({"assets", "import", blenderKitUrl(), "--blender", fakeBlender(temp)});
    CHECK(errorCode(outcome) == "blender_failed");
    const std::string message = outcome.answer.at("error").at("message");
    CHECK(message.find("Sign in to BlenderKit inside Blender first") != std::string::npos);
}

TEST_CASE("materials import of a BlenderKit address imports a material", "[cli][store_import]") {
    MaterialSettingsReset reset;
    TempFolder temp("store-kit-material");
    setUp(temp);
    ScopedEnvironment mode("FAKE_BLENDERKIT", "material");

    const auto outcome = runChecked({"materials", "import", std::string("asset_base_id:") + kId, "--blender",
                                     fakeBlender(temp), "--quiet"});
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& item = outcome.answer.at("result").at("imported").at(0);
    CHECK(item.at("importer") == "blenderkit");
    CHECK_FALSE(item.at("id").get<std::string>().empty());
    const auto tasks = tasksOf(jobsLogged(temp));
    REQUIRE(tasks.size() == 2);
    CHECK(tasks.at(0) == "blenderkit_fetch");
    CHECK(tasks.at(1) == "cleanup_material");
}

TEST_CASE("assets import of a Fab zip cleans up the picked model", "[cli][store_import]") {
    MaterialSettingsReset reset;
    TempFolder temp("store-fab-import");
    const auto library = setUp(temp);
    const auto zip = variantsZip(temp);

    const auto outcome = runChecked({"assets", "import", utf8(zip), "--blender", fakeBlender(temp), "--keep-original"});
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& result = outcome.answer.at("result");
    CHECK(result.at("importer") == "fab_zip");
    CHECK(result.at("original_removed") == false);
    CHECK(std::filesystem::exists(zip));
    CHECK(itemCount(library) == 1);
    const auto jobs = jobsLogged(temp);
    REQUIRE(jobs.size() == 1);
    CHECK(jobs.at(0).at("task") == "cleanup_asset");
    CHECK(jobs.at(0).at("params").at("source").at("format") == "fab");
}

TEST_CASE("assets import --importer fab_zip reads a plain zip as Fab", "[cli][store_import]") {
    MaterialSettingsReset reset;
    TempFolder temp("store-fab-forced");
    setUp(temp);
    const auto zip = temp.path() / "plain.zip";
    plynth::test::writeZipFile(zip, plynth::test::zipEntriesNamed({"lamp.obj", "lamp.mtl"}));
    const auto fake = fakeBlender(temp);

    const auto automatic = runChecked({"assets", "import", utf8(zip), "--dry-run"});
    REQUIRE(automatic.exitCode == kExitOk);
    CHECK(automatic.answer.at("result").at("importer") == "local_file");

    const auto outcome = runChecked({"assets", "import", utf8(zip), "--importer", "fab_zip", "--blender", fake,
                                     "--keep-original"});
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("result").at("importer") == "fab_zip");
    CHECK(jobsLogged(temp).at(0).at("params").at("source").at("format") == "fab");
}

TEST_CASE("a Fab pack with several models asks for one", "[cli][store_import]") {
    MaterialSettingsReset reset;
    TempFolder temp("store-fab-pack");
    const auto library = setUp(temp);
    const auto zip = temp.path() / "pack.zip";
    plynth::test::writeZipFile(zip, plynth::test::zipEntriesNamed({"Chair/Chair_Medium.fbx", "Box/Box_Medium.fbx"}));
    const auto fake = fakeBlender(temp);

    const auto refused = runChecked({"assets", "import", utf8(zip), "--importer", "fab_zip", "--blender", fake});
    CHECK(errorCode(refused) == "invalid_value");
    CHECK(itemCount(library) == 0);

    const auto picked = runChecked({"assets", "import", utf8(zip), "--importer", "fab_zip", "--option",
                                    "model=Chair/Chair_Medium.fbx", "--blender", fake, "--keep-original"});
    REQUIRE(picked.exitCode == kExitOk);
    CHECK(itemCount(library) == 1);
}

TEST_CASE("materials import of a Fab zip cleans up the picked set", "[cli][store_import]") {
    MaterialSettingsReset reset;
    TempFolder temp("store-fab-material");
    setUp(temp);
    const auto zip = oakZip(temp);

    const auto outcome = runChecked({"materials", "import", utf8(zip), "--blender", fakeBlender(temp),
                                     "--keep", "--quiet"});
    REQUIRE(outcome.exitCode == kExitOk);
    const auto& item = outcome.answer.at("result").at("imported").at(0);
    CHECK(item.at("importer") == "fab_zip");
    CHECK(item.at("original_removed") == false);
    CHECK(std::filesystem::exists(zip));
    const auto jobs = jobsLogged(temp);
    REQUIRE(jobs.size() == 1);
    CHECK(jobs.at(0).at("params").at("source").at("kind") == "fab");
}

TEST_CASE("library integrations --blender beats PLYNTH_BLENDER and asks only the named Blender", "[cli][store_import][safety]") {
    MaterialSettingsReset reset;
    TempFolder temp("store-integrations-option");
    const PrivateUser user(temp);
    setUp(temp);
    const auto named = installFakeBlender(temp.path() / "fb", "5.2.0");
    const auto other = installFakeBlender(temp.path() / "other", "5.2.0");
    const ScopedEnvironment variable("PLYNTH_BLENDER", utf8(other));
    const ScopedEnvironment kit("FAKE_BLENDER_BLENDERKIT", "signed_in");

    const auto outcome = runChecked({"library", "integrations", "--blender", utf8(named)});
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("result").at("integrations").at(1).at("state") == "ready");
    CHECK_FALSE(std::filesystem::exists(temp.path() / "other" / "fake_args.log"));

    std::ifstream starts(temp.path() / "fb" / "fake_args.log", std::ios::binary);
    int started = 0;
    for (std::string line; std::getline(starts, line);) {
        CHECK(line.find("--factory-startup") != std::string::npos);
        ++started;
    }
    CHECK(started == 2);

    const auto jobs = jobsIn(temp.path() / "fb");
    REQUIRE(jobs.size() == 1);
    CHECK(jobs.at(0).at("task") == "probe");
    const std::string config = jobs.at(0).at("params").at("user_folders").at("config");
    CHECK(std::filesystem::equivalent(plynth::pathFromUtf8(config), temp.path() / "user" / "config"));
}

TEST_CASE("library integrations --blender with a missing file starts no other Blender", "[cli][store_import][safety]") {
    MaterialSettingsReset reset;
    TempFolder temp("store-integrations-missing");
    const PrivateUser user(temp);
    setUp(temp);
    const auto other = installFakeBlender(temp.path() / "other", "5.2.0");
    const ScopedEnvironment variable("PLYNTH_BLENDER", utf8(other));

    const auto outcome =
        runChecked({"library", "integrations", "--blender", utf8(temp.path() / "nowhere" / "blender.exe")});
    REQUIRE(outcome.exitCode == kExitOk);
    CHECK(outcome.answer.at("result").at("integrations").at(1).at("state") == "not_checked");
    CHECK_FALSE(std::filesystem::exists(temp.path() / "other" / "fake_args.log"));
}
