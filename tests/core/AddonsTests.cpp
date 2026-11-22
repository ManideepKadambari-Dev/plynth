#include "FakeBlender.h"
#include "SchemaSupport.h"

#include <plynth/core/Addons.h>
#include <plynth/core/Blender.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <stop_token>
#include <string>

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
    bad(R"({"format": "other", "version": 1, "blender": "b", "blender_version": "5", "checked_at": "2026-11-22T10:15:00Z", "enabled": []})");
    bad(R"({"format": "plynth-addons", "version": 2, "blender": "b", "blender_version": "5", "checked_at": "2026-11-22T10:15:00Z", "enabled": []})");
    bad(R"({"format": "plynth-addons", "version": 1, "blender": "b", "blender_version": "5", "checked_at": "yesterday", "enabled": []})");
    bad(R"({"format": "plynth-addons", "version": 1, "blender": "b", "blender_version": "5", "checked_at": "2026-11-22T10:15:00Z", "enabled": [{"module": "m"}]})");
    bad(R"({"format": "plynth-addons", "version": 1, "blender": "b", "blender_version": "5", "checked_at": "2026-11-22T10:15:00Z", "enabled": [], "extra": 1})");
}

TEST_CASE("probeAddons writes the cache from the probe", "[addons]")
{
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
