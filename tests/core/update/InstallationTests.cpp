#include "UpdateTestSupport.h"

#include <plynth/core/Version.h>

#include <catch2/catch_test_macros.hpp>

#include <cwctype>

using namespace plynth;
using namespace plynth::test::update;
using namespace plynth::update_detail;

namespace {

// The package.json of a staged release.
std::string packageText(const std::string& version, bool trial = false, const std::string& platform = "windows-x64")
{
    nlohmann::json json = {{"format", "plynth-package"}, {"version", 1}, {"app_version", version},
                           {"commit", "0123456789abcdef"}, {"platform", platform}, {"qt", "6.8.3"},
                           {"runtime_minimum", "14.44"}, {"trial", trial},
                           {"files", nlohmann::json::array({{{"path", "plynth.exe"}, {"sha256", "00"}}})}};
    return json.dump();
}

// A folder that looks like a staged or installed Plynth.
fs::path makeCopy(const fs::path& folder, const std::string& package)
{
    writeBytes(folder / "plynth.exe", "program");
    if (!package.empty()) writeBytes(folder / "package.json", package);
    return folder;
}

// A probe of a program in a folder, beside a real install in installed.
InstallationProbe probeOf(const fs::path& program, const fs::path& installed, std::optional<std::wstring> location)
{
    return InstallationProbe{program, "0.1.0", installed, std::move(location)};
}

}

TEST_CASE("an installed release is known by its package, the fixed folder and the installer's entry", "[update][install]") {
    plynth::test::TempDir tmp("update-install-yes");
    const fs::path installed = makeCopy(tmp.path() / "Programs" / "Plynth", packageText("0.1.0"));
    const Installation copy = detectInstallation(probeOf(installed, installed, installed.wstring() + L"\\"));
    CHECK(copy.kind == InstallKind::Installed);
    CHECK(copy.version == "0.1.0");
    CHECK(copy.folder == installed);

    std::wstring shouting = installed.wstring();
    for (wchar_t& c : shouting) c = static_cast<wchar_t>(std::towupper(c));
    CHECK(detectInstallation(probeOf(installed, installed, shouting)).kind == InstallKind::Installed);
    CHECK(detectInstallation(probeOf(installed / ".", installed, installed.wstring())).kind == InstallKind::Installed);
}

TEST_CASE("every other copy is not installed, also beside a real install", "[update][install]") {
    plynth::test::TempDir tmp("update-install-no");
    const fs::path installed = makeCopy(tmp.path() / "Programs" / "Plynth", packageText("0.1.0"));
    const std::wstring location = installed.wstring() + L"\\";

    for (const bool beside : {true, false}) {
        INFO("beside a real install: " << beside);
        const std::optional<std::wstring> entry = beside ? std::optional<std::wstring>(location) : std::nullopt;
        const fs::path build = makeCopy(tmp.path() / "out" / "build" / "debug" / "app", "");
        CHECK(detectInstallation(probeOf(build, installed, entry)).kind == InstallKind::NotInstalled);
        const fs::path unpacked = makeCopy(tmp.path() / "Downloads" / "plynth-dev", packageText("0.1.0"));
        CHECK(detectInstallation(probeOf(unpacked, installed, entry)).kind == InstallKind::NotInstalled);
        const fs::path trial = makeCopy(tmp.path() / "trial-stage", packageText("0.1.0", true));
        CHECK(detectInstallation(probeOf(trial, installed, entry)).kind == InstallKind::NotInstalled);
        const fs::path stage = makeCopy(tmp.path() / "release-stage", packageText("0.1.0"));
        CHECK(detectInstallation(probeOf(stage, installed, entry)).kind == InstallKind::NotInstalled);
        const fs::path copied = makeCopy(tmp.path() / "Copy of Plynth", packageText("0.1.0"));
        CHECK(detectInstallation(probeOf(copied, installed, entry)).kind == InstallKind::NotInstalled);
    }

    // The fixed folder itself, without the installer's entry or with an entry somewhere else.
    CHECK(detectInstallation(probeOf(installed, installed, std::nullopt)).kind == InstallKind::NotInstalled);
    CHECK(detectInstallation(probeOf(installed, installed, (tmp.path() / "elsewhere").wstring())).kind ==
          InstallKind::NotInstalled);
    // A trial or another version placed in the fixed folder.
    const fs::path trialThere = makeCopy(tmp.path() / "trial" / "Plynth", packageText("0.1.0", true));
    CHECK(detectInstallation(probeOf(trialThere, trialThere, trialThere.wstring())).kind == InstallKind::NotInstalled);
    const fs::path older = makeCopy(tmp.path() / "older" / "Plynth", packageText("0.0.9"));
    CHECK(detectInstallation(probeOf(older, older, older.wstring())).kind == InstallKind::NotInstalled);
    CHECK(detectInstallation(InstallationProbe{}).kind == InstallKind::NotInstalled);
}

TEST_CASE("only a release package of the running version counts", "[update][install]") {
    CHECK(isReleasePackage(packageText("0.1.0"), "0.1.0"));
    CHECK_FALSE(isReleasePackage(packageText("0.1.0"), "0.2.0"));
    CHECK_FALSE(isReleasePackage(packageText("0.1.0", true), "0.1.0"));
    CHECK_FALSE(isReleasePackage(packageText("0.1.0", false, "windows-arm64"), "0.1.0"));
    CHECK_FALSE(isReleasePackage("", "0.1.0"));
    CHECK_FALSE(isReleasePackage("not json", "0.1.0"));
    CHECK_FALSE(isReleasePackage("[]", "0.1.0"));
    nlohmann::json json = nlohmann::json::parse(packageText("0.1.0"));
    json.erase("trial");
    CHECK_FALSE(isReleasePackage(json.dump(), "0.1.0"));
    json = nlohmann::json::parse(packageText("0.1.0"));
    json["version"] = 2;
    CHECK_FALSE(isReleasePackage(json.dump(), "0.1.0"));
    json = nlohmann::json::parse(packageText("0.1.0"));
    json["format"] = "plynth-stage";
    CHECK_FALSE(isReleasePackage(json.dump(), "0.1.0"));
}

TEST_CASE("two folders are the same ignoring case and a trailing separator", "[update][install]") {
    CHECK(sameFolder("C:/Users/A/AppData/Local/Programs/Plynth", "c:\\users\\a\\appdata\\local\\programs\\plynth\\"));
    CHECK(sameFolder("C:/A/B/../Plynth", "C:/A/Plynth"));
    CHECK_FALSE(sameFolder("C:/A/Plynth", "C:/A/Plynth2"));
    CHECK_FALSE(sameFolder("", ""));
}

TEST_CASE("the test program itself is not an installed copy", "[update][install]") {
    const InstallationProbe probe = currentProbe();
    CHECK_FALSE(probe.programFolder.empty());
    CHECK(probe.runningVersion == std::string(plynth::version()));
    CHECK(currentInstallation().kind == InstallKind::NotInstalled);
}
