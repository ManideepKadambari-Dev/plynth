#include "FakeBlender.h"
#include "SchemaSupport.h"

#include <plynth/core/Library.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/SetupCheck.h>
#include <plynth/core/Version.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

using namespace plynth;
namespace fs = std::filesystem;

namespace {

// Makes a folder read only through its Windows attribute and puts it back when the object goes away.
struct ReadOnlyFolder {
    explicit ReadOnlyFolder(const fs::path& folder) : path(folder)
    {
        original = GetFileAttributesW(path.c_str());
        SetFileAttributesW(path.c_str(), original | FILE_ATTRIBUTE_READONLY);
    }
    ~ReadOnlyFolder() { SetFileAttributesW(path.c_str(), original); }
    fs::path path;
    DWORD original = 0;
};

// Checks a report against the setup report schema.
void requireValidReport(const SetupReport& report)
{
    const Json json = setupReportToJson(report);
    const std::string problem = plynth::test::schemaProblem("setup-report.schema.json", nlohmann::json::parse(json.dump()));
    INFO(problem);
    REQUIRE(problem.empty());
}

// A search that looks only in the given folders.
SetupCheckOptions optionsFor(const fs::path& folder, bool probe = false)
{
    SetupCheckOptions options;
    options.search.environment = false;
    options.search.programFolders = false;
    options.search.registry = false;
    options.search.store = false;
    options.search.steam = false;
    options.search.systemPath = false;
    if (!folder.empty()) options.search.extraFolders = {folder};
    options.probeAddons = probe;
    return options;
}

// Settings with a ready library and projects folder inside a temporary folder.
MachineSettings goodFolders(const fs::path& root)
{
    MachineSettings s;
    s.library = root / "Library";
    s.projects = root / "Projects";
    plynth::test::requireOk(initLibrary(*s.library));
    fs::create_directories(*s.projects);
    return s;
}

}

TEST_CASE("no Blender gives blender missing and not ready", "[core][setup]") {
    plynth::test::TempDir tmp("setup-none");
    const SetupReport report = runSetupCheck(goodFolders(tmp.path()), optionsFor({}));
    const SetupItem* blender = report.find(kCheckBlender);
    REQUIRE(blender);
    REQUIRE(blender->state == CheckState::Missing);
    REQUIRE(blender->required);
    REQUIRE(blender->link == kBlenderDownloadUrl);
    REQUIRE_FALSE(blender->action.empty());
    REQUIRE_FALSE(report.ready);
    REQUIRE(report.find(kCheckBlenderVersion)->state == CheckState::NotChecked);
    REQUIRE(report.blenderInstalls.empty());
    requireValidReport(report);
}

TEST_CASE("only an old Blender gives too_old", "[core][setup]") {
    plynth::test::TempDir tmp("setup-old");
    plynth::test::makeFakeBlender(tmp.path() / "b" / "Blender 4.1", "4.1.0");
    const SetupReport report = runSetupCheck(goodFolders(tmp.path()), optionsFor(tmp.path() / "b"));
    REQUIRE(report.find(kCheckBlender)->state == CheckState::Found);
    const SetupItem* version = report.find(kCheckBlenderVersion);
    REQUIRE(version->state == CheckState::TooOld);
    REQUIRE(version->version == "4.1.0");
    REQUIRE(version->blocks());
    REQUIRE_FALSE(report.ready);
    REQUIRE_FALSE(report.selectedBlender);
    requireValidReport(report);
}

TEST_CASE("a supported Blender with good folders is ready", "[core][setup]") {
    plynth::test::TempDir tmp("setup-ready");
    plynth::test::makeFakeBlender(tmp.path() / "b" / "Blender 4.1", "4.1.0");
    const auto good = plynth::test::makeFakeBlender(tmp.path() / "b" / "Blender 5.2", "5.2.0");
    const SetupReport report = runSetupCheck(goodFolders(tmp.path()), optionsFor(tmp.path() / "b"));
    REQUIRE(report.ready);
    REQUIRE(report.selectedBlender == 0);
    REQUIRE(report.blenderInstalls.size() == 2);
    const SetupItem* blender = report.find(kCheckBlender);
    REQUIRE(blender->state == CheckState::Found);
    REQUIRE(blender->version == "5.2.0");
    REQUIRE(blender->path == fs::weakly_canonical(good));
    REQUIRE(report.find(kCheckBlenderVersion)->state == CheckState::Found);
    REQUIRE(report.find(kCheckLibrary)->state == CheckState::Found);
    REQUIRE(report.find(kCheckProjects)->state == CheckState::Found);
    const SetupItem* graphics = report.find(kCheckGraphics);
    REQUIRE(graphics->state == CheckState::NotChecked);
    REQUIRE(graphics->detail == "Checked by the app when it starts the 3D preview.");
    REQUIRE_FALSE(graphics->blocks());
    REQUIRE_FALSE(report.probed);
    for (const char* id : {"addon_blenderkit", "addon_archimesh", "addon_bagapie"}) {
        REQUIRE(report.find(id)->state == CheckState::NotChecked);
    }
    requireValidReport(report);
}

TEST_CASE("the graphics driver item follows the app's probe", "[core][setup][graphics]") {
    plynth::test::TempDir tmp("setup-graphics");
    plynth::test::makeFakeBlender(tmp.path() / "b" / "Blender 5.2", "5.2.0");
    const MachineSettings settings = goodFolders(tmp.path());

    SetupCheckOptions options = optionsFor(tmp.path() / "b");
    options.graphics = GraphicsProbe{true, "Direct3D 11", "Test Graphics 9000", ""};
    SetupReport report = runSetupCheck(settings, options);
    const SetupItem* found = report.find(kCheckGraphics);
    REQUIRE(found->state == CheckState::Found);
    REQUIRE(found->version == "Direct3D 11");
    REQUIRE(found->detail == "Test Graphics 9000");
    REQUIRE(report.ready);
    requireValidReport(report);

    options.graphics = GraphicsProbe{false, "", "", "No graphics interface could be started."};
    report = runSetupCheck(settings, options);
    const SetupItem* missing = report.find(kCheckGraphics);
    REQUIRE(missing->state == CheckState::Missing);
    REQUIRE(missing->blocks());
    REQUIRE(missing->detail == "No graphics interface could be started.");
    REQUIRE(missing->action == "Update the graphics driver. See the help page on the 3D preview.");
    REQUIRE_FALSE(report.ready);
    requireValidReport(report);

    options.graphics.reset();
    REQUIRE(runSetupCheck(settings, options).find(kCheckGraphics)->state == CheckState::NotChecked);

    const SetupReport quickOk = quickSetupCheck(settings, true, GraphicsProbe{true, "Vulkan", "Card", ""});
    REQUIRE(quickOk.find(kCheckGraphics)->state == CheckState::Found);
    const SetupReport quickBad = quickSetupCheck(settings, true, GraphicsProbe{false, "", "", ""});
    REQUIRE(quickBad.find(kCheckGraphics)->state == CheckState::Missing);
    REQUIRE(quickBad.find(kCheckGraphics)->blocks());
    REQUIRE(setupNeeded(quickBad));
    REQUIRE(quickSetupCheck(settings, true).find(kCheckGraphics)->state == CheckState::NotChecked);
}

TEST_CASE("an untested Blender needs attention but does not block", "[core][setup]") {
    plynth::test::TempDir tmp("setup-untested");
    plynth::test::makeFakeBlender(tmp.path() / "b" / "Blender 6.0", "6.0.0");
    const SetupReport report = runSetupCheck(goodFolders(tmp.path()), optionsFor(tmp.path() / "b"));
    REQUIRE(report.find(kCheckBlenderVersion)->state == CheckState::NeedsAttention);
    REQUIRE(report.ready);
    requireValidReport(report);
}

TEST_CASE("the add-on probe finds add-ons and BlenderKit", "[core][setup]") {
    plynth::test::IsolatedUser user("setup-addons-user");
    plynth::test::TempDir tmp("setup-addons");
    plynth::test::makeFakeBlender(tmp.path() / "b" / "Blender 5.2", "5.2.0");
    plynth::test::ScopedEnv addons("FAKE_BLENDER_ADDONS", "bl_ext.blender_org.archimesh,bl_ext.blender_org.home_builder_5,object_boolean_tools");
    plynth::test::ScopedEnv kit("FAKE_BLENDER_BLENDERKIT", "enabled");
    const SetupReport report = runSetupCheck(goodFolders(tmp.path()), optionsFor(tmp.path() / "b", true));
    REQUIRE(report.probed);
    REQUIRE(report.ready);
    REQUIRE(report.find(kCheckArchimesh)->state == CheckState::Found);
    REQUIRE(report.find(kCheckArchimesh)->version == "1.0.0");
    REQUIRE(report.find(kCheckHomeBuilder)->state == CheckState::Found);
    REQUIRE(report.find(kCheckBoolTool)->state == CheckState::Found);
    REQUIRE(report.find(kCheckBagapie)->state == CheckState::Missing);
    REQUIRE(report.find(kCheckAssetOptimizer)->state == CheckState::Missing);
    REQUIRE_FALSE(report.find(kCheckBagapie)->affects.empty());
    const SetupItem* blenderKit = report.find(kCheckBlenderKit);
    REQUIRE(blenderKit->state == CheckState::NeedsAttention);
    REQUIRE_FALSE(blenderKit->blocks());
    REQUIRE(blenderKit->link == "https://www.blendkit.com/");
    for (const SetupItem& i : report.items) {
        if (i.id.rfind("addon_", 0) == 0) REQUIRE_FALSE(i.required);
    }
    requireValidReport(report);
}

TEST_CASE("BlenderKit states", "[core][setup]") {
    plynth::test::IsolatedUser user("setup-kit-user");
    plynth::test::TempDir tmp("setup-kit");
    plynth::test::makeFakeBlender(tmp.path() / "b" / "Blender 5.2", "5.2.0");
    const MachineSettings settings = goodFolders(tmp.path());
    const auto stateFor = [&](const char* mode) {
        plynth::test::ScopedEnv kit("FAKE_BLENDER_BLENDERKIT", mode);
        return runSetupCheck(settings, optionsFor(tmp.path() / "b", true)).find(kCheckBlenderKit)->state;
    };
    REQUIRE(stateFor("none") == CheckState::Missing);
    REQUIRE(stateFor("installed") == CheckState::Missing);
    REQUIRE(stateFor("enabled") == CheckState::NeedsAttention);
    REQUIRE(stateFor("signed_in") == CheckState::Found);
}

TEST_CASE("a failed probe leaves add-ons not checked", "[core][setup]") {
    plynth::test::IsolatedUser user("setup-probe-fail-user");
    plynth::test::TempDir tmp("setup-probe-fail");
    plynth::test::makeFakeBlender(tmp.path() / "b" / "Blender 5.2", "5.2.0", "fail");
    const SetupReport report = runSetupCheck(goodFolders(tmp.path()), optionsFor(tmp.path() / "b", true));
    REQUIRE_FALSE(report.probed);
    REQUIRE(report.ready);
    const SetupItem* archimesh = report.find(kCheckArchimesh);
    REQUIRE(archimesh->state == CheckState::NotChecked);
    REQUIRE(archimesh->detail == "Blender is needed to check add-ons.");
}

TEST_CASE("the setup check never gives the probe the user's folders", "[core][setup][safety]") {
    for (const char* mode : {"write_user", "write_user+fail", "write_user+crash"}) {
        DYNAMIC_SECTION(mode) {
            plynth::test::TempDir tmp("setup-safe");
            plynth::test::IsolatedUser user("setup-safe-user");
            const fs::path exe = plynth::test::makeFakeBlender(tmp.path() / "b" / "Blender 5.2", "5.2.0", mode);
            const auto before = user.fingerprint();
            const SetupReport report = runSetupCheck(goodFolders(tmp.path()), optionsFor(tmp.path() / "b", true));
            REQUIRE(user.fingerprint() == before);
            REQUIRE(report.probed == (std::string(mode) == "write_user"));
            REQUIRE(fs::exists(user.tmp.path() / "private" / "config" / "fake_marker.txt"));
            REQUIRE(fs::is_empty(user.tempFolder));

            std::ifstream args(exe.parent_path() / "fake_args.log", std::ios::binary);
            int starts = 0;
            for (std::string line; std::getline(args, line); ++starts) {
                REQUIRE(line.find("--factory-startup") != std::string::npos);
            }
            REQUIRE(starts >= 2);
        }
    }
}

namespace {

// True when the fake in this folder was ever started.
bool wasStarted(const fs::path& exe)
{
    return fs::exists(exe.parent_path() / "fake_env.log");
}

}

TEST_CASE("a named Blender is the only one the setup check looks at", "[core][setup][choice]") {
    plynth::test::TempDir tmp("setup-bound");
    plynth::test::IsolatedUser user("setup-bound-user");
    plynth::test::ScopedEnv shortTimeout("PLYNTH_BLENDER_VERSION_TIMEOUT_MS", "2000");
    const fs::path good = plynth::test::makeFakeBlender(tmp.path() / "b" / "good", "5.2.0");
    const fs::path other = plynth::test::makeFakeBlender(tmp.path() / "b" / "other", "5.2.0");
    const fs::path old = plynth::test::makeFakeBlender(tmp.path() / "old", "4.1.0");
    const fs::path hung = plynth::test::makeFakeBlender(tmp.path() / "hung", "5.2.0", "hang_version");
    const fs::path missing = tmp.path() / "none" / "blender.exe";
    const MachineSettings settings = goodFolders(tmp.path());
    SetupCheckOptions options = optionsFor(tmp.path() / "b", false);
    options.search.environment = true;

    SECTION("the variable names a missing file") {
        plynth::test::ScopedEnv env("PLYNTH_BLENDER", pathToUtf8(missing));
        const SetupReport report = runSetupCheck(settings, options);
        REQUIRE(report.blenderInstalls.empty());
        REQUIRE(report.find(kCheckBlender)->state == CheckState::Missing);
        REQUIRE(report.find(kCheckBlender)->detail.find("none") != std::string::npos);
        REQUIRE_FALSE(report.ready);
        REQUIRE_FALSE(wasStarted(good));
        REQUIRE_FALSE(wasStarted(other));
    }
    SECTION("the variable names a Blender that is too old") {
        plynth::test::ScopedEnv env("PLYNTH_BLENDER", pathToUtf8(old));
        const SetupReport report = runSetupCheck(settings, options);
        REQUIRE(report.blenderInstalls.size() == 1);
        REQUIRE(report.find(kCheckBlenderVersion)->state == CheckState::TooOld);
        REQUIRE_FALSE(wasStarted(good));
        REQUIRE_FALSE(wasStarted(other));
    }
    SECTION("the variable names a Blender that does not answer") {
        plynth::test::ScopedEnv env("PLYNTH_BLENDER", pathToUtf8(hung));
        const SetupReport report = runSetupCheck(settings, options);
        REQUIRE(report.blenderInstalls.size() == 1);
        REQUIRE(report.find(kCheckBlender)->state == CheckState::Missing);
        REQUIRE_FALSE(wasStarted(good));
        REQUIRE_FALSE(wasStarted(other));
    }
    SECTION("the option beats the variable") {
        plynth::test::ScopedEnv env("PLYNTH_BLENDER", pathToUtf8(old));
        options.blender = good;
        const SetupReport report = runSetupCheck(settings, options);
        REQUIRE(report.blenderInstalls.size() == 1);
        REQUIRE(report.find(kCheckBlender)->state == CheckState::Found);
        REQUIRE(report.ready);
        REQUIRE_FALSE(wasStarted(old));
        REQUIRE_FALSE(wasStarted(other));
    }
    SECTION("a bad option starts no other Blender") {
        plynth::test::ScopedEnv env("PLYNTH_BLENDER", pathToUtf8(good));
        options.blender = missing;
        REQUIRE(runSetupCheck(settings, options).find(kCheckBlender)->state == CheckState::Missing);
        options.blender = old;
        REQUIRE(runSetupCheck(settings, options).find(kCheckBlenderVersion)->state == CheckState::TooOld);
        REQUIRE_FALSE(wasStarted(good));
        REQUIRE_FALSE(wasStarted(other));
    }
    SECTION("without a name every install is listed") {
        plynth::test::ScopedEnv env("PLYNTH_BLENDER", "");
        REQUIRE(runSetupCheck(settings, options).blenderInstalls.size() == 2);
    }
    SECTION("the variable is ignored when the search leaves it out") {
        plynth::test::ScopedEnv env("PLYNTH_BLENDER", pathToUtf8(missing));
        options.search.environment = false;
        REQUIRE(runSetupCheck(settings, options).blenderInstalls.size() == 2);
    }
    SECTION("the quick check looks only at the variable") {
        {
            plynth::test::ScopedEnv env("PLYNTH_BLENDER", pathToUtf8(missing));
            MachineSettings saved = settings;
            saved.blender = good;
            REQUIRE(quickSetupCheck(saved, true).find(kCheckBlender)->state == CheckState::Missing);
        }
        plynth::test::ScopedEnv env("PLYNTH_BLENDER", pathToUtf8(old));
        const SetupReport report = quickSetupCheck(settings, true);
        REQUIRE(report.find(kCheckBlender)->state == CheckState::Found);
        REQUIRE(report.find(kCheckBlender)->path == old);
        REQUIRE_FALSE(wasStarted(old));
    }
}

TEST_CASE("library folder states", "[core][setup]") {
    plynth::test::TempDir tmp("setup-library");
    plynth::test::makeFakeBlender(tmp.path() / "b" / "Blender 5.2", "5.2.0");
    const SetupCheckOptions options = optionsFor(tmp.path() / "b");
    MachineSettings settings;
    settings.projects = tmp.path();

    SECTION("not set") {
        const SetupReport report = runSetupCheck(settings, options);
        REQUIRE(report.find(kCheckLibrary)->state == CheckState::Missing);
        REQUIRE_FALSE(report.ready);
    }
    SECTION("set but missing") {
        settings.library = tmp.path() / "nowhere";
        REQUIRE(runSetupCheck(settings, options).find(kCheckLibrary)->state == CheckState::Missing);
    }
    SECTION("set but not a library") {
        fs::create_directories(tmp.path() / "plain");
        settings.library = tmp.path() / "plain";
        const SetupReport report = runSetupCheck(settings, options);
        const SetupItem* item = report.find(kCheckLibrary);
        REQUIRE(item->state == CheckState::Missing);
        REQUIRE(item->action.find("library init") != std::string::npos);
    }
    SECTION("a read only library") {
        plynth::test::requireOk(initLibrary(tmp.path() / "Library"));
        settings.library = tmp.path() / "Library";
        REQUIRE(runSetupCheck(settings, options).find(kCheckLibrary)->state == CheckState::Found);
        const ReadOnlyFolder readOnly(*settings.library);
        const SetupReport report = runSetupCheck(settings, options);
        const SetupItem* item = report.find(kCheckLibrary);
        REQUIRE(item->state == CheckState::Missing);
        REQUIRE(item->detail.find("cannot be written") != std::string::npos);
    }
    SECTION("low free space needs attention and does not block") {
        plynth::test::requireOk(initLibrary(tmp.path() / "Library"));
        settings.library = tmp.path() / "Library";
        SetupCheckOptions tight = options;
        tight.minLibraryFree = std::numeric_limits<std::uintmax_t>::max();
        const SetupReport report = runSetupCheck(settings, tight);
        REQUIRE(report.find(kCheckLibrary)->state == CheckState::NeedsAttention);
        REQUIRE(report.ready);
    }
}

TEST_CASE("projects folder states", "[core][setup]") {
    plynth::test::TempDir tmp("setup-projects");
    MachineSettings settings = goodFolders(tmp.path());
    const SetupCheckOptions options = optionsFor({});

    settings.projects.reset();
    REQUIRE(runSetupCheck(settings, options).find(kCheckProjects)->state == CheckState::Missing);
    settings.projects = tmp.path() / "nowhere";
    REQUIRE(runSetupCheck(settings, options).find(kCheckProjects)->state == CheckState::Missing);
    settings.projects = tmp.path() / "Projects";
    REQUIRE(runSetupCheck(settings, options).find(kCheckProjects)->state == CheckState::Found);
    const ReadOnlyFolder readOnly(*settings.projects);
    REQUIRE(runSetupCheck(settings, options).find(kCheckProjects)->state == CheckState::Missing);
}

TEST_CASE("firstRun and updated come from the settings", "[core][setup]") {
    plynth::test::TempDir tmp("setup-flags");
    MachineSettings settings = goodFolders(tmp.path());
    SetupCheckOptions options = optionsFor({});

    options.settingsExist = false;
    REQUIRE(runSetupCheck(settings, options).firstRun);
    REQUIRE_FALSE(runSetupCheck(settings, options).updated);

    options.settingsExist = true;
    settings.setupCheckedVersion = std::string(version());
    SetupReport report = runSetupCheck(settings, options);
    REQUIRE_FALSE(report.firstRun);
    REQUIRE_FALSE(report.updated);
    settings.setupCheckedVersion = "0.0.0-older";
    REQUIRE(runSetupCheck(settings, options).updated);
}

TEST_CASE("setupNeeded", "[core][setup]") {
    plynth::test::TempDir tmp("setup-needed");
    const auto fake = plynth::test::makeFakeBlender(tmp.path() / "b" / "Blender 5.2", "5.2.0");
    MachineSettings settings = goodFolders(tmp.path());
    settings.blender = fake;
    settings.setupCheckedVersion = std::string(version());

    SetupReport quick = quickSetupCheck(settings, true);
    REQUIRE(quick.ready);
    REQUIRE(quick.find(kCheckBlender)->state == CheckState::Found);
    REQUIRE(quick.find(kCheckBlenderVersion)->state == CheckState::NotChecked);
    REQUIRE(quick.find(kCheckArchimesh)->state == CheckState::NotChecked);
    REQUIRE_FALSE(setupNeeded(quick));
    requireValidReport(quick);

    REQUIRE(setupNeeded(quickSetupCheck(settings, false)));

    MachineSettings other = settings;
    other.setupCheckedVersion = "0.0.0-older";
    REQUIRE(setupNeeded(quickSetupCheck(other, true)));

    MachineSettings noLibrary = settings;
    noLibrary.library.reset();
    const SetupReport missingLibrary = quickSetupCheck(noLibrary, true);
    REQUIRE_FALSE(missingLibrary.ready);
    REQUIRE(setupNeeded(missingLibrary));

    MachineSettings goneBlender = settings;
    goneBlender.blender = tmp.path() / "gone" / "blender.exe";
    const SetupReport quickGone = quickSetupCheck(goneBlender, true);
    REQUIRE(quickGone.find(kCheckBlender)->path != goneBlender.blender);
}

TEST_CASE("known add-ons", "[core][setup]") {
    const auto& addons = knownAddons();
    REQUIRE(addons.size() == 6);
    REQUIRE(addons[0].checkId == kCheckBlenderKit);
    REQUIRE(addons[0].neededForImport);
    REQUIRE(addons[1].checkId == kCheckArchimesh);
    REQUIRE(addons[2].checkId == kCheckHomeBuilder);
    REQUIRE(addons[3].modules == std::vector<std::string_view>{"bool_tool", "object_boolean_tools"});
    REQUIRE(addons[5].checkId == kCheckBagapie);
    REQUIRE(checkStateName(CheckState::NeedsAttention) == "needs_attention");
    REQUIRE(checkStateName(CheckState::NotChecked) == "not_checked");
}

TEST_CASE("setupReportToJson lists the installs", "[core][setup]") {
    plynth::test::TempDir tmp("setup-json");
    plynth::test::makeFakeBlender(tmp.path() / "b" / "Blender 5.2", "5.2.0");
    plynth::test::makeFakeBlender(tmp.path() / "b" / "Blender 4.1", "4.1.0");
    const SetupReport report = runSetupCheck(goodFolders(tmp.path()), optionsFor(tmp.path() / "b"));
    const Json json = setupReportToJson(report);
    REQUIRE(json["ready"].get<bool>());
    REQUIRE(json["items"].size() == 11);
    REQUIRE(json["items"][0]["id"] == "blender");
    REQUIRE(json["items"][4]["id"] == "graphics_driver");
    REQUIRE(json["items"][4]["state"] == "not_checked");
    REQUIRE(json["items"][4]["version"].is_null());
    REQUIRE(json["blender_installs"].size() == 2);
    REQUIRE(json["blender_installs"][0]["selected"].get<bool>());
    REQUIRE_FALSE(json["blender_installs"][1]["selected"].get<bool>());
    REQUIRE(json["blender_installs"][1]["support"] == "too_old");
    REQUIRE(json["blender_installs"][0]["sources"][0] == "extra");
    REQUIRE(json["blender_installs"][0]["problem"].is_null());
    requireValidReport(report);
}

TEST_CASE("the setup check runs against the real Blender", "[.blender]") {
    plynth::test::IsolatedUser user("setup-real-user");
    plynth::test::TempDir tmp("setup-real");
    const SetupReport report = runSetupCheck(goodFolders(tmp.path()), SetupCheckOptions{});
    REQUIRE(report.find(kCheckBlender)->state == CheckState::Found);
    REQUIRE(report.probed);
    REQUIRE(report.ready);
    requireValidReport(report);
}
