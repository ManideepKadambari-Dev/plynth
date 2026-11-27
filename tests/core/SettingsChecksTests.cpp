#include "FakeBlender.h"
#include "TestSupport.h"

#include <plynth/core/Library.h>
#include <plynth/core/ProjectFiles.h>
#include <plynth/core/SettingsChecks.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <limits>
#include <stop_token>
#include <string>

using namespace plynth;
namespace fs = std::filesystem;

TEST_CASE("a library choice must be a Plynth library", "[core][settingschecks]")
{
    plynth::test::TempDir tmp("settings-library");
    const fs::path library = tmp.path() / "Library";
    plynth::test::requireOk(initLibrary(library));
    plynth::test::requireOk(checkLibraryChoice(library));

    const fs::path empty = tmp.path() / "Empty";
    fs::create_directories(empty);
    const Status notLibrary = checkLibraryChoice(empty);
    REQUIRE(notLibrary.code == ErrorCode::NoLibrary);
    REQUIRE(notLibrary.message ==
            pathToUtf8(empty) + " is not a Plynth library. Run plynth library init there, or pick another folder.");

    const fs::path missing = tmp.path() / "Missing";
    REQUIRE(checkLibraryChoice(missing).code == ErrorCode::NoLibrary);
    // A folder inside a library is not the library.
    REQUIRE(checkLibraryChoice(library / "assets").code == ErrorCode::NoLibrary);

    // After the app makes one there, the same folder passes.
    plynth::test::requireOk(initLibrary(empty));
    plynth::test::requireOk(checkLibraryChoice(empty));
}

TEST_CASE("a projects folder must exist and be a folder", "[core][settingschecks]")
{
    plynth::test::TempDir tmp("settings-projects");
    plynth::test::requireOk(checkProjectsChoice(tmp.path()));

    const fs::path missing = tmp.path() / "Missing";
    const Status gone = checkProjectsChoice(missing);
    REQUIRE(gone.code == ErrorCode::NotFound);
    REQUIRE(gone.message == "The projects folder " + pathToUtf8(missing) + " does not exist.");

    const fs::path file = tmp.path() / "notes.txt";
    std::ofstream(file) << "x";
    REQUIRE(checkProjectsChoice(file).code == ErrorCode::NotFound);
    REQUIRE(checkProjectsChoice(fs::path()).code == ErrorCode::NotFound);
}

TEST_CASE("a Blender choice must be a usable blender.exe of a supported version", "[core][settingschecks]")
{
    plynth::test::TempDir tmp("settings-blender");

    SECTION("a supported Blender passes and is inspected") {
        const fs::path exe = plynth::test::makeFakeBlender(tmp.path() / "good", "5.2.0");
        const BlenderPickCheck check = checkBlenderChoice(exe);
        plynth::test::requireOk(check.status);
        REQUIRE(check.install.usable());
        REQUIRE(check.install.version);
        REQUIRE(formatBlenderVersion(*check.install.version) == "5.2.0");
        REQUIRE(check.install.executable == exe);
    }
    SECTION("the oldest supported version passes") {
        const fs::path exe = plynth::test::makeFakeBlender(tmp.path() / "min", formatBlenderVersion(kMinBlenderVersion));
        plynth::test::requireOk(checkBlenderChoice(exe).status);
    }
    SECTION("an older Blender is refused and says the lowest version") {
        const fs::path exe = plynth::test::makeFakeBlender(tmp.path() / "old", "4.1.0");
        const BlenderPickCheck check = checkBlenderChoice(exe);
        REQUIRE(check.status.code == ErrorCode::BlenderNotFound);
        REQUIRE(check.status.message ==
                pathToUtf8(exe) + " cannot be used: it is older than Blender " + formatBlenderVersion(kMinBlenderVersion) + ".");
        REQUIRE(check.install.version);
    }
    SECTION("a missing file is refused without starting anything") {
        const fs::path exe = tmp.path() / "nothing" / "blender.exe";
        const BlenderPickCheck check = checkBlenderChoice(exe);
        REQUIRE(check.status.code == ErrorCode::BlenderNotFound);
        REQUIRE(check.status.message == pathToUtf8(exe) + " is not a file.");
        REQUIRE(check.install.executable.empty());
    }
    SECTION("a folder is not a file") {
        const BlenderPickCheck check = checkBlenderChoice(tmp.path());
        REQUIRE(check.status.code == ErrorCode::BlenderNotFound);
        REQUIRE(check.status.message == pathToUtf8(tmp.path()) + " is not a file.");
    }
    SECTION("a program that is not Blender cannot be used") {
        const fs::path exe = fs::path(PLYNTH_ECHO_LINES);
        const BlenderPickCheck check = checkBlenderChoice(exe);
        REQUIRE(check.status.code == ErrorCode::BlenderNotFound);
        REQUIRE(check.status.message.rfind(pathToUtf8(exe) + " cannot be used: ", 0) == 0);
        REQUIRE_FALSE(check.install.usable());
    }
    SECTION("a check that was asked to stop is Cancelled") {
        const fs::path exe = plynth::test::makeFakeBlender(tmp.path() / "stop", "5.2.0", "hang_version");
        std::stop_source source;
        source.request_stop();
        const BlenderPickCheck check = checkBlenderChoice(exe, source.get_token());
        REQUIRE(check.status.code == ErrorCode::Cancelled);
        REQUIRE(check.status.message == "The Blender check was cancelled.");
    }
}

TEST_CASE("the triangle warning must be at least 1", "[core][settingschecks]")
{
    plynth::test::requireOk(checkTriangleWarning(1));
    plynth::test::requireOk(checkTriangleWarning(500000));
    plynth::test::requireOk(checkTriangleWarning((std::numeric_limits<long long>::max)()));
    for (const long long bad : {0LL, -1LL, (std::numeric_limits<long long>::min)()}) {
        const Status s = checkTriangleWarning(bad);
        REQUIRE(s.code == ErrorCode::InvalidValue);
        REQUIRE(s.message == "The triangle warning must be at least 1.");
    }
}
