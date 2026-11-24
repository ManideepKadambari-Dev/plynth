#include "FakeBlender.h"
#include "SchemaSupport.h"

#include <plynth/core/Addons.h>
#include <plynth/core/Blender.h>
#include <plynth/core/ProjectFiles.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

using namespace plynth;
namespace fs = std::filesystem;

namespace {

AddonCache sampleCache(const fs::path& blender)
{
    AddonCache cache;
    cache.blender = blender;
    cache.blenderVersion = "5.2.0";
    cache.checkedAt = parseSessionTime("2026-11-22T10:15:00Z").value();
    cache.enabled.push_back({"bl_ext.blender_org.archimesh", "Archimesh", "1.2.5"});
    cache.enabled.push_back({"bl_ext.blender_org.Bagapie", "Bagapie", "2.0.1"});
    return cache;
}

BlenderInstall installAt(const fs::path& exe, int major, int minor, int patch)
{
    BlenderInstall install;
    install.executable = exe;
    install.version = BlenderVersion{major, minor, patch};
    return install;
}

void writeText(const fs::path& file, const std::string& text)
{
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

// The lines of a log the fake Blender wrote next to its program.
std::vector<std::string> logLines(const fs::path& exe, const char* name)
{
    std::vector<std::string> lines;
    std::ifstream in(exe.parent_path() / name, std::ios::binary);
    for (std::string line; std::getline(in, line);) lines.push_back(line);
    return lines;
}

}

TEST_CASE("add-on short names are the last dotted part in lower case", "[addons]")
{
    REQUIRE(addonShortName("bl_ext.blender_org.Bagapie") == "bagapie");
    REQUIRE(addonShortName("object_boolean_tools") == "object_boolean_tools");
    REQUIRE(addonShortName("bl_ext.blender_org.home_builder_5") == "home_builder_5");

    const AddonCache cache = sampleCache("C:/Blender/blender.exe");
    REQUIRE(addonEnabled(cache, "bagapie"));
    REQUIRE(addonEnabled(cache, "Archimesh"));
    REQUIRE_FALSE(addonEnabled(cache, "bool_tool"));

    REQUIRE(addonDisplayName("home_builder_5") == "Home Builder");
    REQUIRE(addonDisplayName("archimesh") == "Archimesh");
    REQUIRE(addonDisplayName("object_boolean_tools") == "Bool Tool");
    REQUIRE(addonDisplayName("something_else") == "something_else");
}

TEST_CASE("a cache matches only the Blender that made it", "[addons]")
{
    const AddonCache cache = sampleCache("C:/Blender/blender.exe");
    REQUIRE(addonCacheMatches(cache, installAt("C:/Blender/blender.exe", 5, 2, 0)));
    REQUIRE(addonCacheMatches(cache, installAt("c:\\blender\\blender.exe", 5, 2, 0)));
    REQUIRE_FALSE(addonCacheMatches(cache, installAt("C:/Other/blender.exe", 5, 2, 0)));
    REQUIRE_FALSE(addonCacheMatches(cache, installAt("C:/Blender/blender.exe", 5, 3, 0)));
    BlenderInstall noVersion;
    noVersion.executable = "C:/Blender/blender.exe";
    REQUIRE_FALSE(addonCacheMatches(cache, noVersion));
}

TEST_CASE("a cache is written, read back and follows the schema", "[addons]")
{
    plynth::test::TempDir tmp("addon-cache");
    const fs::path file = tmp.path() / "settings" / "addons.json";
    const AddonCache cache = sampleCache(tmp.path() / "blender.exe");

    const Json json = addonCacheToJson(cache);
    REQUIRE(json["format"] == "plynth-addons");
    REQUIRE(json["checked_at"] == "2026-11-22T10:15:00Z");
    REQUIRE(plynth::test::schemaProblem("addon-cache.schema.json", nlohmann::json::parse(json.dump())).empty());

    plynth::test::requireOk(writeAddonCache(file, cache));
    REQUIRE(plynth::test::schemaProblemForText("addon-cache.schema.json", plynth::test::readTextFile(file)).empty());
    const AddonCacheRead read = readAddonCache(file);
    plynth::test::requireOk(read.status);
    REQUIRE(read.cache.has_value());
    REQUIRE(addonCacheToJson(*read.cache) == json);
    REQUIRE(read.cache->enabled.size() == 2);
    REQUIRE(read.cache->enabled[0].module == "bl_ext.blender_org.Bagapie");
}

TEST_CASE("a missing cache file is fine and a broken one is refused", "[addons]")
{
    plynth::test::TempDir tmp("addon-cache-bad");
    const AddonCacheRead missing = readAddonCache(tmp.path() / "none.json");
    plynth::test::requireOk(missing.status);
    REQUIRE_FALSE(missing.cache.has_value());

    const auto bad = [&](const std::string& text) {
        writeText(tmp.path() / "addons.json", text);
        const AddonCacheRead read = readAddonCache(tmp.path() / "addons.json");
        INFO(text);
        REQUIRE(read.status.code == ErrorCode::BadFile);
        REQUIRE_FALSE(read.cache.has_value());
    };
    bad("{ not json");
    bad("[]");
    bad(R"({"format": "other", "version": 2, "blender": "b", "blender_version": "5", "checked_at": "2026-11-22T10:15:00Z", "enabled": []})");
    bad(R"({"format": "plynth-addons", "version": 3, "blender": "b", "blender_version": "5", "checked_at": "2026-11-22T10:15:00Z", "enabled": []})");
    bad(R"({"format": "plynth-addons", "version": 0, "blender": "b", "blender_version": "5", "checked_at": "2026-11-22T10:15:00Z", "enabled": []})");
    bad(R"({"format": "plynth-addons", "version": 2, "blender": "b", "blender_version": "5", "checked_at": "yesterday", "enabled": []})");
    bad(R"({"format": "plynth-addons", "version": 2, "blender": "b", "blender_version": "5", "checked_at": "2026-11-22T10:15:00Z", "enabled": [{"module": "m"}]})");
    bad(R"({"format": "plynth-addons", "version": 2, "blender": "b", "blender_version": "5", "checked_at": "2026-11-22T10:15:00Z", "enabled": [], "extra": 1})");
}

TEST_CASE("a cache of an older version is not used and the next probe replaces it", "[addons]")
{
    plynth::test::IsolatedUser user("addon-cache-old-user");
    plynth::test::TempDir tmp("addon-cache-old");
    const fs::path exe = plynth::test::makeFakeBlender(tmp.path() / "blender", "5.2.0");
    const fs::path file = tmp.path() / "addons.json";
    writeText(file, R"({"format": "plynth-addons", "version": 1, "blender": "b", "blender_version": "5.2.0", "checked_at": "2026-11-22T10:15:00Z", "enabled": []})");

    const AddonCacheRead old = readAddonCache(file);
    plynth::test::requireOk(old.status);
    REQUIRE_FALSE(old.cache.has_value());

    BlenderRunOptions run;
    run.scriptsFolder = plynth::test::makeFakeScripts(tmp.path() / "scripts");
    plynth::test::requireOk(probeAddons(inspectBlender(exe), file, run).status);
    const AddonCacheRead replaced = readAddonCache(file);
    plynth::test::requireOk(replaced.status);
    REQUIRE(replaced.cache.has_value());
    REQUIRE(plynth::test::schemaProblemForText("addon-cache.schema.json", plynth::test::readTextFile(file)).empty());
}

TEST_CASE("probeAddons writes the cache from the probe", "[addons]")
{
    plynth::test::IsolatedUser user("addon-probe-user");
    plynth::test::TempDir tmp("addon-probe");
    const fs::path exe = plynth::test::makeFakeBlender(tmp.path() / "blender", "5.2.0");
    const fs::path scripts = plynth::test::makeFakeScripts(tmp.path() / "scripts");
    const BlenderInstall install = inspectBlender(exe);
    REQUIRE(install.usable());
    plynth::test::ScopedEnv addons("FAKE_BLENDER_ADDONS", "bl_ext.blender_org.archimesh");
    const fs::path file = tmp.path() / "settings" / "addons.json";

    BlenderRunOptions run;
    run.scriptsFolder = scripts;
    const AddonProbe probe = probeAddons(install, file, run);
    plynth::test::requireOk(probe.status);
    REQUIRE(probe.cache.enabled.size() == 1);
    REQUIRE(probe.cache.enabled[0].module == "bl_ext.blender_org.archimesh");
    REQUIRE(addonEnabled(probe.cache, "archimesh"));
    REQUIRE(addonCacheMatches(probe.cache, install));

    const AddonCacheRead read = readAddonCache(file);
    plynth::test::requireOk(read.status);
    REQUIRE(read.cache.has_value());
    REQUIRE(addonCacheToJson(*read.cache) == addonCacheToJson(probe.cache));
    REQUIRE(addonCacheMatches(*read.cache, install));
}

TEST_CASE("a failing probe leaves the old cache as it was", "[addons]")
{
    plynth::test::IsolatedUser user("addon-probe-fail-user");
    plynth::test::TempDir tmp("addon-probe-fail");
    const fs::path exe = plynth::test::makeFakeBlender(tmp.path() / "blender", "5.2.0", "fail");
    const fs::path scripts = plynth::test::makeFakeScripts(tmp.path() / "scripts");
    const BlenderInstall install = inspectBlender(exe);
    REQUIRE(install.usable());
    const fs::path file = tmp.path() / "addons.json";
    plynth::test::requireOk(writeAddonCache(file, sampleCache(exe)));
    const std::string before = plynth::test::readTextFile(file);

    BlenderRunOptions run;
    run.scriptsFolder = scripts;
    const AddonProbe probe = probeAddons(install, file, run);
    REQUIRE(probe.status.code == ErrorCode::BlenderFailed);
    REQUIRE(plynth::test::readTextFile(file) == before);
}

TEST_CASE("a stopped probe is cancelled", "[addons]")
{
    plynth::test::IsolatedUser user("addon-probe-stop-user");
    plynth::test::TempDir tmp("addon-probe-stop");
    const fs::path exe = plynth::test::makeFakeBlender(tmp.path() / "blender", "5.2.0");
    const fs::path scripts = plynth::test::makeFakeScripts(tmp.path() / "scripts");
    const BlenderInstall install = inspectBlender(exe);
    std::stop_source stop;
    stop.request_stop();
    BlenderRunOptions run;
    run.scriptsFolder = scripts;
    const AddonProbe probe = probeAddons(install, tmp.path() / "addons.json", run, stop.get_token());
    REQUIRE(probe.status.code == ErrorCode::Cancelled);
    REQUIRE_FALSE(fs::exists(tmp.path() / "addons.json"));
}

TEST_CASE("probeAddons leaves the user's folders alone however the run ends", "[addons][safety]")
{
    struct Outcome {
        const char* mode;
        ErrorCode code;
    };
    for (const Outcome& outcome : {Outcome{"write_user", ErrorCode::None}, Outcome{"write_user+fail", ErrorCode::BlenderFailed},
                                   Outcome{"write_user+unreadable", ErrorCode::BlenderFailed}, Outcome{"write_user+crash", ErrorCode::BlenderFailed},
                                   Outcome{"write_user+hang", ErrorCode::BlenderFailed}, Outcome{"write_user+hang", ErrorCode::Cancelled}}) {
        DYNAMIC_SECTION(outcome.mode << " " << errorCodeName(outcome.code)) {
            plynth::test::TempDir tmp("addon-safe");
            plynth::test::IsolatedUser user("addon-safe-user");
            const fs::path exe = plynth::test::makeFakeBlender(tmp.path() / "blender", "5.2.0", outcome.mode);
            const fs::path file = tmp.path() / "addons.json";
            const BlenderInstall install = inspectBlender(exe);
            REQUIRE(install.usable());
            const auto before = user.fingerprint();

            BlenderRunOptions run;
            run.factoryStartup = false;
            run.scriptsFolder = plynth::test::makeFakeScripts(tmp.path() / "scripts");
            run.timeout = std::chrono::seconds(1);
            std::stop_source stop;
            std::thread stopper;
            if (outcome.code == ErrorCode::Cancelled) {
                run.timeout = std::chrono::seconds(60);
                stopper = std::thread([&] {
                    std::this_thread::sleep_for(std::chrono::milliseconds(800));
                    stop.request_stop();
                });
            }
            const AddonProbe probe = probeAddons(install, file, run, stop.get_token());
            if (stopper.joinable()) stopper.join();

            REQUIRE(probe.status.code == outcome.code);
            REQUIRE(fs::exists(file) == (outcome.code == ErrorCode::None));
            REQUIRE(user.fingerprint() == before);
            REQUIRE(fs::is_empty(user.tempFolder));
            // The marker says a folder holds prepared add-on copies, which the shared private folder never does.
            REQUIRE_FALSE(fs::exists(user.tmp.path() / "private" / "extensions" / ".plynth-prepared"));

            const auto args = logLines(exe, "fake_args.log");
            REQUIRE(args.size() >= 2);
            for (const std::string& line : args) REQUIRE(line.find("--factory-startup") != std::string::npos);
            const std::string priv = pathToUtf8(user.tmp.path() / "private");
            for (const std::string& line : logLines(exe, "fake_env.log")) {
                REQUIRE(line.find("BLENDER_USER_RESOURCES=" + priv + "|") != std::string::npos);
                REQUIRE(line.find("BLENDER_USER_CONFIG=" + priv + "/config|") != std::string::npos);
                REQUIRE(line.find("BLENDER_USER_SCRIPTS=" + priv + "/scripts|") != std::string::npos);
                REQUIRE(line.find("BLENDER_USER_EXTENSIONS=" + priv + "/extensions|") != std::string::npos);
                REQUIRE(line.find("BLENDER_USER_DATAFILES=" + priv + "/datafiles") != std::string::npos);
            }

            // The fake logs a job it was given, also when the run then fails.
            const auto jobs = logLines(exe, "fake_jobs.log");
            REQUIRE(jobs.size() == 1);
            const nlohmann::json job = nlohmann::json::parse(jobs.front());
            REQUIRE(plynth::test::schemaProblem("blender-job.schema.json", job).empty());
            const fs::path config = pathFromUtf8(job["params"]["user_folders"]["config"].get<std::string>());
            REQUIRE(fs::equivalent(config, user.userRoot / "config"));
        }
    }
}
