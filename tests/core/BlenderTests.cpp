#include "FakeBlender.h"

#include <plynth/core/Blender.h>
#include <plynth/core/ProjectFiles.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

using namespace plynth;
namespace fs = std::filesystem;

namespace {

// A search that looks only in the given folders.
BlenderSearch onlyFolders(std::vector<fs::path> folders)
{
    BlenderSearch search;
    search.environment = false;
    search.programFolders = false;
    search.registry = false;
    search.store = false;
    search.steam = false;
    search.systemPath = false;
    search.downloaded = false;
    search.extraFolders = std::move(folders);
    return search;
}

}

TEST_CASE("parseBlenderVersion", "[core][blender]") {
    REQUIRE(parseBlenderVersion("Blender 5.2.0 LTS") == BlenderVersion{5, 2, 0});
    REQUIRE(parseBlenderVersion("Blender 4.2") == BlenderVersion{4, 2, 0});
    REQUIRE(parseBlenderVersion("Blender 4.2.13 (hash abc)") == BlenderVersion{4, 2, 13});
    REQUIRE_FALSE(parseBlenderVersion("Blender"));
    REQUIRE_FALSE(parseBlenderVersion(""));
    REQUIRE_FALSE(parseBlenderVersion("Blender 5"));
    REQUIRE(formatBlenderVersion({5, 2, 0}) == "5.2.0");
}

TEST_CASE("blenderSupport", "[core][blender]") {
    REQUIRE(blenderSupport({4, 1, 1}) == BlenderSupport::TooOld);
    REQUIRE(blenderSupport({4, 2, 0}) == BlenderSupport::Supported);
    REQUIRE(blenderSupport({5, 2, 0}) == BlenderSupport::Supported);
    REQUIRE(blenderSupport({5, 9, 3}) == BlenderSupport::Supported);
    REQUIRE(blenderSupport({6, 0, 0}) == BlenderSupport::Untested);
    REQUIRE(blenderSupportName(BlenderSupport::TooOld) == "too_old");
    REQUIRE(blenderSupportName(BlenderSupport::Untested) == "untested");
    REQUIRE(blenderSourceName(BlenderSource::SystemPath) == "path");
    REQUIRE(blenderSourceName(BlenderSource::ProgramFolder) == "program_folder");
}

TEST_CASE("installs are sorted newest first and the newest is preferred", "[core][blender]") {
    plynth::test::TempDir tmp("blender-sort");
    const auto v41 = plynth::test::makeFakeBlender(tmp.path() / "Blender Foundation" / "Blender 4.1", "4.1.0");
    const auto v52 = plynth::test::makeFakeBlender(tmp.path() / "Blender Foundation" / "Blender 5.2", "5.2.0");
    const auto v45 = plynth::test::makeFakeBlender(tmp.path() / "Blender Foundation" / "Blender 4.5", "4.5.0");

    const auto installs = findBlenderInstalls(onlyFolders({tmp.path() / "Blender Foundation"}));
    REQUIRE(installs.size() == 3);
    REQUIRE(installs[0].version == BlenderVersion{5, 2, 0});
    REQUIRE(installs[1].version == BlenderVersion{4, 5, 0});
    REQUIRE(installs[2].version == BlenderVersion{4, 1, 0});
    REQUIRE(installs[0].versionText == "Blender 5.2.0 LTS");
    REQUIRE(installs[0].usable());
    REQUIRE_FALSE(installs[2].usable());
    REQUIRE(installs[2].support == BlenderSupport::TooOld);
    REQUIRE(installs[0].sources == std::vector<BlenderSource>{BlenderSource::Extra});

    REQUIRE(preferredBlender(installs, std::nullopt) == 0);
    REQUIRE(preferredBlender(installs, v45) == 1);
    REQUIRE(preferredBlender(installs, v41) == 0);
    REQUIRE(preferredBlender(installs, tmp.path() / "nothing" / "blender.exe") == 0);
    REQUIRE(preferredBlender({}, std::nullopt) == std::nullopt);
    (void)v52;
}

TEST_CASE("an untested newer Blender is used only when no supported one exists", "[core][blender]") {
    plynth::test::TempDir tmp("blender-untested");
    plynth::test::makeFakeBlender(tmp.path() / "a" / "Blender 6.0", "6.0.0");
    plynth::test::makeFakeBlender(tmp.path() / "a" / "Blender 5.2", "5.2.0");
    auto installs = findBlenderInstalls(onlyFolders({tmp.path() / "a"}));
    REQUIRE(installs[0].version == BlenderVersion{6, 0, 0});
    REQUIRE(installs[0].usable());
    REQUIRE(installs[0].support == BlenderSupport::Untested);
    REQUIRE(preferredBlender(installs, std::nullopt) == 1);

    plynth::test::makeFakeBlender(tmp.path() / "b" / "Blender 6.0", "6.0.0");
    installs = findBlenderInstalls(onlyFolders({tmp.path() / "b"}));
    REQUIRE(preferredBlender(installs, std::nullopt) == 0);
}

TEST_CASE("the same exe found twice is listed once with both sources", "[core][blender]") {
    plynth::test::TempDir tmp("blender-merge");
    const auto exe = plynth::test::makeFakeBlender(tmp.path() / "Blender 5.2", "5.2.0");
    BlenderSearch search = onlyFolders({tmp.path()});
    search.chosen = exe;
    const auto candidates = findBlenderCandidates(search);
    REQUIRE(candidates.size() == 1);
    REQUIRE(candidates[0].sources == std::vector<BlenderSource>{BlenderSource::Settings, BlenderSource::Extra});
}

TEST_CASE("a chosen path that does not exist is left out", "[core][blender]") {
    plynth::test::TempDir tmp("blender-gone");
    BlenderSearch search = onlyFolders({});
    search.chosen = tmp.path() / "gone" / "blender.exe";
    REQUIRE(findBlenderCandidates(search).empty());
}

TEST_CASE("the environment variable names a Blender", "[core][blender]") {
    plynth::test::TempDir tmp("blender-env");
    const auto exe = plynth::test::makeFakeBlender(tmp.path() / "x", "5.2.0");
    plynth::test::ScopedEnv env("PLYNTH_BLENDER", pathToUtf8(exe));
    BlenderSearch search = onlyFolders({});
    search.environment = true;
    const auto candidates = findBlenderCandidates(search);
    REQUIRE(candidates.size() == 1);
    REQUIRE(candidates[0].sources == std::vector<BlenderSource>{BlenderSource::Environment});
}

TEST_CASE("a Blender that does not answer --version in time is broken", "[core][blender]") {
    plynth::test::TempDir tmp("blender-hang");
    const auto exe = plynth::test::makeFakeBlender(tmp.path() / "Blender 5.2", "5.2.0", "hang_version");
    plynth::test::ScopedEnv timeout("PLYNTH_BLENDER_VERSION_TIMEOUT_MS", "500");
    const BlenderInstall install = inspectBlender(exe);
    REQUIRE(install.status.code == ErrorCode::BlenderFailed);
    REQUIRE_FALSE(install.version);
    REQUIRE_FALSE(install.usable());
}

TEST_CASE("inspectBlender on a program that is not Blender", "[core][blender]") {
    const BlenderInstall install = inspectBlender(PLYNTH_ECHO_LINES);
    REQUIRE(install.status.code == ErrorCode::BlenderFailed);
    REQUIRE_FALSE(install.usable());
    REQUIRE(inspectBlender("C:/no/such/blender.exe").status.code == ErrorCode::BlenderFailed);
}

TEST_CASE("installs that cannot be read come last", "[core][blender]") {
    plynth::test::TempDir tmp("blender-broken-last");
    plynth::test::makeFakeBlender(tmp.path() / "a" / "Blender 4.2", "4.2.0");
    plynth::test::makeFakeBlender(tmp.path() / "a" / "Blender 5.2", "5.2.0", "hang_version");
    plynth::test::ScopedEnv timeout("PLYNTH_BLENDER_VERSION_TIMEOUT_MS", "500");
    const auto installs = findBlenderInstalls(onlyFolders({tmp.path() / "a"}));
    REQUIRE(installs.size() == 2);
    REQUIRE(installs[0].version == BlenderVersion{4, 2, 0});
    REQUIRE_FALSE(installs[1].version);
}

TEST_CASE("chooseBlender", "[core][blender]") {
    plynth::test::TempDir tmp("blender-choose");
    const auto good = plynth::test::makeFakeBlender(tmp.path() / "good", "5.2.0");
    const auto old = plynth::test::makeFakeBlender(tmp.path() / "old", "4.1.0");
    plynth::test::ScopedEnv noEnv("PLYNTH_BLENDER", "");

    SECTION("an option that does not exist") {
        const BlenderChoice c = chooseBlender(tmp.path() / "none" / "blender.exe", MachineSettings{});
        REQUIRE(c.status.code == ErrorCode::BlenderNotFound);
    }
    SECTION("an option that is too old") {
        const BlenderChoice c = chooseBlender(old, MachineSettings{});
        REQUIRE(c.status.code == ErrorCode::BlenderNotFound);
        REQUIRE(c.status.message.find("4.1") != std::string::npos);
    }
    SECTION("a good option") {
        const BlenderChoice c = chooseBlender(good, MachineSettings{});
        plynth::test::requireOk(c.status);
        REQUIRE(c.install.version == BlenderVersion{5, 2, 0});
    }
    SECTION("the option wins over the settings") {
        MachineSettings s;
        s.blender = old;
        plynth::test::requireOk(chooseBlender(good, s).status);
    }
    SECTION("the environment variable comes before the settings") {
        plynth::test::ScopedEnv env("PLYNTH_BLENDER", pathToUtf8(good));
        MachineSettings s;
        s.blender = old;
        const BlenderChoice c = chooseBlender(std::nullopt, s);
        plynth::test::requireOk(c.status);
        REQUIRE(fs::equivalent(c.install.executable, good));
    }
    SECTION("the settings are used when nothing else is given") {
        MachineSettings s;
        s.blender = good;
        const BlenderChoice c = chooseBlender(std::nullopt, s);
        plynth::test::requireOk(c.status);
        REQUIRE(c.install.version == BlenderVersion{5, 2, 0});
    }
}

namespace {

// True when the fake in this folder was ever started.
bool wasStarted(const fs::path& exe)
{
    return fs::exists(exe.parent_path() / "fake_env.log");
}

}

TEST_CASE("a named Blender is the only one started", "[core][blender][choice]") {
    plynth::test::TempDir tmp("blender-bound");
    plynth::test::IsolatedUser user("blender-bound-user");
    plynth::test::ScopedEnv shortTimeout("PLYNTH_BLENDER_VERSION_TIMEOUT_MS", "2000");
    const auto good = plynth::test::makeFakeBlender(tmp.path() / "good", "5.2.0");
    const auto old = plynth::test::makeFakeBlender(tmp.path() / "old", "4.1.0");
    const auto hung = plynth::test::makeFakeBlender(tmp.path() / "hung", "5.2.0", "hang_version");
    const auto other = plynth::test::makeFakeBlender(tmp.path() / "other", "5.2.0");
    const fs::path missing = tmp.path() / "none" / "blender.exe";
    MachineSettings settings;
    settings.blender = other;

    SECTION("the variable names a missing file") {
        plynth::test::ScopedEnv env("PLYNTH_BLENDER", pathToUtf8(missing));
        const BlenderChoice c = chooseBlender(std::nullopt, settings);
        REQUIRE(c.status.code == ErrorCode::BlenderNotFound);
        REQUIRE(c.status.message.find("none") != std::string::npos);
    }
    SECTION("the variable names a Blender that is too old") {
        plynth::test::ScopedEnv env("PLYNTH_BLENDER", pathToUtf8(old));
        const BlenderChoice c = chooseBlender(std::nullopt, settings);
        REQUIRE(c.status.code == ErrorCode::BlenderNotFound);
        REQUIRE(c.status.message.find("4.1") != std::string::npos);
    }
    SECTION("the variable names a Blender that does not answer") {
        plynth::test::ScopedEnv env("PLYNTH_BLENDER", pathToUtf8(hung));
        REQUIRE(chooseBlender(std::nullopt, settings).status.code == ErrorCode::BlenderNotFound);
    }
    SECTION("the option beats the variable") {
        plynth::test::ScopedEnv env("PLYNTH_BLENDER", pathToUtf8(old));
        const BlenderChoice c = chooseBlender(good, settings);
        plynth::test::requireOk(c.status);
        REQUIRE(fs::equivalent(c.install.executable, good));
        REQUIRE_FALSE(wasStarted(old));
    }
    SECTION("the variable beats the settings") {
        plynth::test::ScopedEnv env("PLYNTH_BLENDER", pathToUtf8(good));
        plynth::test::requireOk(chooseBlender(std::nullopt, settings).status);
    }
    SECTION("a bad option starts no other Blender") {
        for (const fs::path& bad : {missing, old, hung}) {
            plynth::test::ScopedEnv env("PLYNTH_BLENDER", pathToUtf8(good));
            REQUIRE(chooseBlender(bad, settings).status.code == ErrorCode::BlenderNotFound);
        }
        REQUIRE_FALSE(wasStarted(good));
    }
    REQUIRE_FALSE(wasStarted(other));
}

TEST_CASE("blenderScriptsFolder", "[core][blender]") {
    plynth::test::TempDir tmp("blender-scripts");
    SECTION("the environment variable wins when it holds the worker") {
        const auto scripts = plynth::test::makeFakeScripts(tmp.path() / "s");
        plynth::test::ScopedEnv env("PLYNTH_BLENDER_SCRIPTS", pathToUtf8(scripts));
        REQUIRE(blenderScriptsFolder() == scripts);
    }
    SECTION("a folder without the worker is skipped") {
        fs::create_directories(tmp.path() / "empty");
        plynth::test::ScopedEnv env("PLYNTH_BLENDER_SCRIPTS", pathToUtf8(tmp.path() / "empty"));
        const fs::path found = blenderScriptsFolder();
        REQUIRE(found != tmp.path() / "empty");
        if (!found.empty()) REQUIRE(fs::exists(found / "plynth_worker.py"));
    }
}

TEST_CASE("machine search finds a usable Blender", "[.blender]") {
    BlenderSearch search;
    const auto installs = findBlenderInstalls(search);
    REQUIRE_FALSE(installs.empty());
    bool usable = false;
    for (const BlenderInstall& i : installs) {
        INFO(pathToUtf8(i.executable) << " " << i.versionText << " " << i.status.message);
        usable = usable || i.usable();
    }
    REQUIRE(usable);
    REQUIRE(preferredBlender(installs, std::nullopt).has_value());
}
