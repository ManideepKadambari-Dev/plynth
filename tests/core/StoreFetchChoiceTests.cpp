#include "FetchTestSupport.h"

#include "../../core/src/stores/StoreFetchFolder.h"

#include <aclapi.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace plynth;
using plynth::detail::FetchFolderInputs;
using plynth::test::FetchRig;
using plynth::test::ScopedEnv;
namespace fs = std::filesystem;

namespace {

const std::string kNoShortFolder = "The folder for store downloads would need a path of at most 95 characters. Set "
                                   "PLYNTH_FETCH_FOLDER to a short folder, for example C:/PlynthFetch.";

// A made-up LOCALAPPDATA so long that Plynth/fetch below it is over the limit. It does not exist.
fs::path longLocal()
{
    return fs::path(L"C:\\Users\\" + std::wstring(100, L'u') + L"\\AppData\\Local");
}

// Values for the choice: no variable, the given LOCALAPPDATA, no short 8.3 form and a test folder in place
// of the system drive, so nothing real is ever looked at or made.
FetchFolderInputs inputsFor(const fs::path& local, const fs::path& root)
{
    FetchFolderInputs in;
    in.localAppData = local;
    in.shortRoot = root;
    in.userId = "1a2b3c4d";
    in.shortPath = [](const fs::path& path) { return path; };
    return in;
}

// A short folder below the real temp folder that stands in for the system drive, removed at the end.
// Made before any fetch rig, so it is under the short real temp folder.
struct ShortRoot {
    fs::path path = fs::temp_directory_path() / ("pr" + std::to_string(GetCurrentProcessId()));
    ShortRoot()
    {
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path, ec);
    }
    ~ShortRoot()
    {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

// The published 64 bit FNV-1a hash of some text, written out here so the test does not trust core's copy.
std::uint64_t fnv(const std::string& text)
{
    std::uint64_t hash = 14695981039346656037ULL;
    for (const char c : text) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}

// One fetch of the made-up model in an already chosen folder.
FetchRun fetchIn(const FetchRig& rig, const FetchFolderChoice& choice, const fs::path& work)
{
    return detail::runImportFetchIn(choice, makeBlenderKitFetch(plynth::test::kFetchItem, kFetchKindModel, BlenderKitOptions{}),
                                    rig.blender, rig.base, work);
}

}

TEST_CASE("a short LOCALAPPDATA gives Plynth/fetch below it", "[core][fetch][fetch_choice]")
{
    ShortRoot root;
    const FetchFolderChoice choice = detail::chooseFetchFolder(inputsFor("C:/Users/ann/AppData/Local", root.path));
    plynth::test::requireOk(choice.status);
    CHECK(choice.used == 2);
    CHECK(choice.folder == fs::path(L"C:\\Users\\ann\\AppData\\Local\\Plynth\\fetch"));
}

TEST_CASE("the fetch folder variable comes first, and a long one is refused without trying the others", "[core][fetch][fetch_choice]")
{
    ShortRoot root;
    FetchFolderInputs in = inputsFor("C:/Users/ann/AppData/Local", root.path);
    in.variable = "D:/f/";
    FetchFolderChoice choice = detail::chooseFetchFolder(in);
    plynth::test::requireOk(choice.status);
    CHECK(choice.used == 1);
    CHECK(choice.folder == fs::path(L"D:\\f"));

    const std::string tail(100, 'v');
    in.variable = fs::path("D:/") / tail;
    choice = detail::chooseFetchFolder(in);
    CHECK(choice.status.code == ErrorCode::CannotWrite);
    CHECK(choice.status.message.find("PLYNTH_FETCH_FOLDER") != std::string::npos);
    CHECK(choice.status.message.find(tail) == std::string::npos);
    CHECK(choice.used == 0);
    CHECK(choice.folder.empty());
}

TEST_CASE("Plynth/fetch of exactly 95 bytes is used and one of 96 is not", "[core][fetch][fetch_choice]")
{
    ShortRoot root;
    const std::string ending = "\\Plynth\\fetch";
    for (const std::size_t bytes : {std::size_t(95), std::size_t(96)}) {
        INFO(bytes);
        const std::string local = "C:\\" + std::string(bytes - ending.size() - 3, 'x');
        const FetchFolderChoice choice = detail::chooseFetchFolder(inputsFor(pathFromUtf8(local), root.path));
        plynth::test::requireOk(choice.status);
        if (bytes == 95) {
            CHECK(choice.used == 2);
            CHECK(pathToUtf8(choice.folder) == local + ending);
            CHECK(pathToUtf8(choice.folder).size() == 95);
        } else {
            CHECK(choice.used == 4);
        }
    }
}

TEST_CASE("a long LOCALAPPDATA uses its short 8.3 path when Windows has one", "[core][fetch][fetch_choice]")
{
    ShortRoot root;
    FetchFolderInputs in = inputsFor(longLocal(), root.path);
    fs::path asked;
    in.shortPath = [&](const fs::path& path) {
        asked = path;
        return fs::path(L"C:\\Users\\UUUUUU~1\\AppData\\Local");
    };
    const FetchFolderChoice choice = detail::chooseFetchFolder(in);
    plynth::test::requireOk(choice.status);
    CHECK(choice.used == 3);
    CHECK(choice.folder == fs::path(L"C:\\Users\\UUUUUU~1\\AppData\\Local\\Plynth\\fetch"));
    CHECK(asked == longLocal());
}

TEST_CASE("a long LOCALAPPDATA without a short path falls back to PlynthFetch for this user and makes nothing", "[core][fetch][fetch_choice]")
{
    ShortRoot root;
    for (const fs::path& local : {longLocal(), fs::path()}) {
        INFO(pathToUtf8(local));
        const FetchFolderChoice choice = detail::chooseFetchFolder(inputsFor(local, root.path));
        plynth::test::requireOk(choice.status);
        CHECK(choice.used == 4);
        CHECK(choice.folder == root.path / "PlynthFetch" / "1a2b3c4d");
        CHECK_FALSE(fs::exists(root.path / "PlynthFetch"));
    }
}

TEST_CASE("when even PlynthFetch is too long the choice gives the sentence", "[core][fetch][fetch_choice]")
{
    ShortRoot root;
    const fs::path tooLong = root.path / std::string(100, 'r');
    const FetchFolderChoice choice = detail::chooseFetchFolder(inputsFor(longLocal(), tooLong));
    CHECK(choice.status.code == ErrorCode::CannotWrite);
    CHECK(choice.status.message == kNoShortFolder);
    CHECK(choice.used == 0);
    CHECK(choice.folder.empty());
    CHECK_FALSE(fs::exists(tooLong));
}

TEST_CASE("the user's folder name is the first 8 hex digits of the FNV hash of the SID", "[core][fetch][fetch_choice]")
{
    const std::string sid = "S-1-5-21-111-222-333-1001";
    char full[17] = {};
    std::snprintf(full, sizeof full, "%016llx", static_cast<unsigned long long>(fnv(sid)));
    CHECK(detail::fetchUserId(sid) == std::string(full, 8));
    CHECK(detail::fetchUserId(sid) != detail::fetchUserId("S-1-5-21-111-222-333-1002"));

    const FetchFolderInputs real = detail::systemFetchFolderInputs();
    REQUIRE(real.userId.size() == 8);
    CHECK(real.userId.find_first_not_of("0123456789abcdef") == std::string::npos);
    CHECK(static_cast<bool>(real.shortPath));
}

TEST_CASE("the last resort folder is made for this user only, labelled low, made fresh and removed", "[core][fetch][fetch_choice]")
{
    ShortRoot root;
    const fs::path folder = root.path / "PlynthFetch" / "1a2b3c4d";
    plynth::test::requireOk(detail::makeShortFetchFolder(folder));
    plynth::test::requireOk(checkLowIntegrityFolder(folder));

    PSID owner = nullptr;
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    std::wstring name = folder.wstring();
    REQUIRE(GetNamedSecurityInfoW(name.data(), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner,
                                  nullptr, &dacl, nullptr, &descriptor) == ERROR_SUCCESS);
    std::vector<unsigned char> user = detail::currentUserSid();
    REQUIRE_FALSE(user.empty());
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    REQUIRE(GetSecurityDescriptorControl(descriptor, &control, &revision));
    CHECK((control & SE_DACL_PROTECTED) != 0);
    CHECK(EqualSid(owner, user.data()));
    REQUIRE(dacl != nullptr);
    REQUIRE(dacl->AceCount == 1);
    void* ace = nullptr;
    REQUIRE(GetAce(dacl, 0, &ace));
    auto* allowed = static_cast<ACCESS_ALLOWED_ACE*>(ace);
    CHECK(allowed->Header.AceType == ACCESS_ALLOWED_ACE_TYPE);
    CHECK(EqualSid(&allowed->SidStart, user.data()));
    LocalFree(descriptor);

    plynth::test::writeBytes(folder / "client" / "left.txt", "left");
    plynth::test::requireOk(detail::makeShortFetchFolder(folder));
    CHECK(fs::is_empty(folder));

    CHECK(detail::removeShortFetchFolder(folder));
    CHECK_FALSE(fs::exists(folder));
    CHECK_FALSE(fs::exists(root.path / "PlynthFetch"));
}

TEST_CASE("a junction or a file in place of PlynthFetch is refused with the sentence and never followed", "[core][fetch][fetch_choice]")
{
    ShortRoot root;
    const fs::path parent = root.path / "PlynthFetch";
    const fs::path elsewhere = root.path / "elsewhere";
    fs::create_directories(elsewhere);
    REQUIRE(plynth::test::makeJunction(parent, elsewhere));
    Status made = detail::makeShortFetchFolder(parent / "1a2b3c4d");
    CHECK(made.code == ErrorCode::CannotWrite);
    CHECK(made.message == kNoShortFolder);
    CHECK(fs::is_empty(elsewhere));
    REQUIRE(RemoveDirectoryW(parent.c_str()));

    plynth::test::writeBytes(parent, "not a folder");
    made = detail::makeShortFetchFolder(parent / "1a2b3c4d");
    CHECK(made.code == ErrorCode::CannotWrite);
    CHECK(made.message == kNoShortFolder);
    CHECK(plynth::test::readTextFile(parent) == "not a folder");
}

TEST_CASE("a fetch in the last resort folder runs there and removes it after a good and a failed end", "[core][fetch][fetch_choice]")
{
    ShortRoot root;
    FetchRig rig("pc");
    const FetchFolderChoice choice = detail::chooseFetchFolder(inputsFor(longLocal(), root.path));
    REQUIRE(choice.used == 4);
    for (const std::string kit : {"", "download_failed"}) {
        INFO(kit);
        const ScopedEnv mode("FAKE_BLENDERKIT", kit);
        const FetchRun run = fetchIn(rig, choice, rig.work / (kit.empty() ? "good" : "failed"));
        if (kit.empty()) {
            plynth::test::requireOk(run.status);
            CHECK(fs::is_regular_file(run.file));
        } else {
            CHECK(run.status.code == ErrorCode::BlenderFailed);
        }
        CHECK_FALSE(fs::exists(choice.folder));
        CHECK_FALSE(fs::exists(root.path / "PlynthFetch"));
    }
    const auto runs = rig.runs();
    REQUIRE(runs.size() == 2);
    for (const nlohmann::json& seen : runs) {
        CHECK(pathFromUtf8(seen.at("private_folder").get<std::string>()).lexically_normal() == choice.folder);
    }
}

TEST_CASE("a fetch with no short folder, or a last resort folder that cannot be made, starts nothing", "[core][fetch][fetch_choice]")
{
    ShortRoot root;
    FetchRig rig("pn");
    const FetchFolderChoice none = detail::chooseFetchFolder(inputsFor(longLocal(), root.path / std::string(100, 'r')));
    FetchRun run = fetchIn(rig, none, rig.work / "none");
    CHECK(run.status.code == ErrorCode::CannotWrite);
    CHECK(run.status.message == kNoShortFolder);

    plynth::test::writeBytes(root.path / "PlynthFetch", "not a folder");
    const FetchFolderChoice blocked = detail::chooseFetchFolder(inputsFor(longLocal(), root.path));
    REQUIRE(blocked.used == 4);
    run = fetchIn(rig, blocked, rig.work / "blocked");
    CHECK(run.status.code == ErrorCode::CannotWrite);
    CHECK(run.status.message == kNoShortFolder);
    CHECK(rig.starts() == 0);
}
